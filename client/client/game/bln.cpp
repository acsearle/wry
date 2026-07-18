/* bln.cpp — implementation. Mostly-C style; bit-identical to
 * bandlimited_noise.py (see its docstring for the derivation and the
 * overflow/rounding analysis).
 *
 * Arithmetic contract (the whole determinism story):
 *   - hashing: uint32 mul/add/xor/shift only (pcg3d, Jarzynski & Olano 2020)
 *   - filtering: int64 add/mul + arithmetic right shifts with explicit
 *     rounding constants; shift schedule is static, never data-dependent
 *   - level coordinate mapping: FLOOR division via arithmetic shift
 * Headroom proof: |white| < 2^31, weights <= 2^16, so a weighted white is
 * < 2^47; injection multiplies coarse content by 4 and every filter pass is
 * DC-normalized with all-positive taps (max-norm non-expansive), so the
 * pre-filter accumulator stays < 2^31 * 4 * sum(weights) <= 2^53 for 16
 * octaves; the in-pass convolution transient adds 4 bits per axis (< 2^61),
 * inside int64 for every legal parameter set — no runtime clamping needed.
 */
#include "bln.h"

static_assert((-1 >> 1) == -1, "arithmetic right shift on signed required");
static_assert(sizeof(size_t) >= 8, "64-bit size_t assumed by scratch sizing");

/* ------------------------------------------------------------------ hash */

static inline uint32_t bln_pcg3d_x(uint32_t x, uint32_t yl, uint32_t zl)
{
    /* yl, zl are the y/z lanes AFTER their first LCG step (hoisted by the
     * caller: they depend only on row / level). Order matches the reference
     * pcg3d exactly; only the x lane of the second round is needed. */
    x = x * 1664525u + 1013904223u;
    x += yl * zl;
    yl += zl * x;
    zl += x * yl;
    x ^= x >> 16;
    yl ^= yl >> 16;
    zl ^= zl >> 16;
    x += yl * zl;
    return x;
}

/* --------------------------------------------------------------- plan */

typedef struct {
    int64_t px0, py0;      /* pre-filter rect origin (world, this level)  */
    int32_t pw, ph;        /* pre-filter rect size                        */
    int64_t qx0, qy0;      /* requested rect origin                       */
    int32_t qw, qh;        /* requested rect size (= pw,ph - 2*passes)    */
    size_t  off;           /* element offset of this level's scratch slot */
} bln_level;

static int bln_check(const bln_params *p, int32_t w, int32_t h)
{
    if (!p) return -1;
    if (p->levels < 1 || p->levels > BLN_MAX_LEVELS) return -1;
    if (p->passes < 1 || p->passes > BLN_MAX_PASSES) return -1;
    if (p->out_shift < 0 || p->out_shift > 31) return -1;
    if (w < 1 || h < 1 || w > (1 << 20) || h > (1 << 20)) return -1;
    return 0;
}

/* Position-independent slot bound: a window of length n holds at most
 * (n >> 1) + 1 even coordinates, so bound(k+1) = (bound(k) >> 1) + 1 + 2r.
 * The actual per-level rects (computed in bln_generate from x0/y0 parity)
 * are always <= these bounds. */
static size_t bln_plan_slots(const bln_params *p, int32_t w, int32_t h,
                             size_t off[BLN_MAX_LEVELS])
{
    if (bln_check(p, w, h) != 0) return 0;
    const int32_t r = p->passes;
    size_t bw = (size_t)w + 2 * (size_t)r;
    size_t bh = (size_t)h + 2 * (size_t)r;
    size_t total = 0;
    for (int32_t k = 0; k < p->levels; k++) {
        off[k] = total;
        total += bw * bh;
        bw = (bw >> 1) + 1 + 2 * (size_t)r;
        bh = (bh >> 1) + 1 + 2 * (size_t)r;
    }
    return total;
}

size_t bln_scratch_size(const bln_params *p, int32_t w, int32_t h)
{
    size_t off[BLN_MAX_LEVELS];
    return bln_plan_slots(p, w, h, off) * sizeof(int64_t);
}

/* ------------------------------------------------------------ pipeline */

static void bln_fill_white(int64_t *buf, int32_t bw, int32_t bh,
                           int64_t wx0, int64_t wy0, uint32_t salt,
                           int64_t weight)
{
    const uint32_t zl = salt * 1664525u + 1013904223u;
    for (int32_t j = 0; j < bh; j++) {
        const uint32_t yl = (uint32_t)(wy0 + j) * 1664525u + 1013904223u;
        int64_t *row = buf + (size_t)j * (size_t)bw;
        for (int32_t i = 0; i < bw; i++) {
            const uint32_t hv = bln_pcg3d_x((uint32_t)(wx0 + i), yl, zl);
            row[i] = ((int64_t)hv - 2147483648LL) * weight;
        }
    }
}

/* Add 4x the coarse field onto the even world coordinates of the fine
 * pre-filter rect (x4 restores the DC gain lost to 2D zero-stuffing; the
 * fine level's own filter then acts as the interpolator). */
static void bln_inject(int64_t *fine, int32_t fstride,
                       int64_t fx0, int64_t fy0,
                       const int64_t *coarse, int32_t cstride,
                       int32_t cw, int32_t ch, int64_t cx0, int64_t cy0)
{
    const int32_t ox = (int32_t)(cx0 * 2 - fx0);   /* 0 or 1 */
    const int32_t oy = (int32_t)(cy0 * 2 - fy0);
    for (int32_t j = 0; j < ch; j++) {
        int64_t *frow = fine + (size_t)(oy + 2 * j) * (size_t)fstride + ox;
        const int64_t *crow = coarse + (size_t)j * (size_t)cstride;
        for (int32_t i = 0; i < cw; i++)
            frow[2 * i] += crow[i] * 4;
    }
}

/* `passes` x ([1 2 1] horizontal, [1 2 1] vertical, rounding >> 4), in
 * place, valid region: shrinks by 1 cell per side per pass. Rows stay at
 * the original stride; only the logical width/height shrink. */
static void bln_filter(int64_t *buf, int32_t stride,
                       int32_t *w_io, int32_t *h_io, int32_t passes)
{
    int32_t w = *w_io, h = *h_io;
    for (int32_t pass = 0; pass < passes; pass++) {
        for (int32_t j = 0; j < h; j++) {
            int64_t *row = buf + (size_t)j * (size_t)stride;
            for (int32_t i = 0; i < w - 2; i++)
                row[i] = row[i] + 2 * row[i + 1] + row[i + 2];
        }
        w -= 2;
        for (int32_t j = 0; j < h - 2; j++) {
            int64_t *r0 = buf + (size_t)j * (size_t)stride;
            const int64_t *r1 = r0 + stride;
            const int64_t *r2 = r1 + stride;
            for (int32_t i = 0; i < w; i++)
                r0[i] = (r0[i] + 2 * r1[i] + r2[i] + 8) >> 4;
        }
        h -= 2;
    }
    *w_io = w;
    *h_io = h;
}

int bln_generate(const bln_params *p, int64_t x0, int64_t y0,
                 int32_t w, int32_t h,
                 int32_t *out, ptrdiff_t out_stride, void *scratch)
{
    size_t off[BLN_MAX_LEVELS];
    if (bln_plan_slots(p, w, h, off) == 0) return -1;
    if (!out || !scratch) return -1;
    if (((uintptr_t)scratch & 7u) != 0) return -1;
    int64_t *const base = (int64_t *)scratch;
    const int32_t r = p->passes;
    const int32_t L = p->levels;

    /* exact per-level rects, fine -> coarse (parity via floor shifts) */
    bln_level lv[BLN_MAX_LEVELS];
    lv[0].qx0 = x0; lv[0].qy0 = y0; lv[0].qw = w; lv[0].qh = h;
    for (int32_t k = 0; k < L; k++) {
        lv[k].px0 = lv[k].qx0 - r;
        lv[k].py0 = lv[k].qy0 - r;
        lv[k].pw  = lv[k].qw + 2 * r;
        lv[k].ph  = lv[k].qh + 2 * r;
        lv[k].off = off[k];
        if (k + 1 < L) {
            const int64_t cx0 = (lv[k].px0 + 1) >> 1;                   /* ceil  */
            const int64_t cy0 = (lv[k].py0 + 1) >> 1;
            const int64_t cx1 = (lv[k].px0 + lv[k].pw - 1) >> 1;        /* floor */
            const int64_t cy1 = (lv[k].py0 + lv[k].ph - 1) >> 1;
            lv[k + 1].qx0 = cx0; lv[k + 1].qy0 = cy0;
            lv[k + 1].qw = (int32_t)(cx1 - cx0 + 1);
            lv[k + 1].qh = (int32_t)(cy1 - cy0 + 1);
        }
    }

    /* synthesize coarse -> fine */
    for (int32_t k = L - 1; k >= 0; k--) {
        int64_t *buf = base + lv[k].off;
        const uint32_t salt = p->seed ^ ((uint32_t)k * 0x9E3779B9u);
        bln_fill_white(buf, lv[k].pw, lv[k].ph, lv[k].px0, lv[k].py0,
                       salt, (int64_t)p->weights[k]);
        if (k + 1 < L)
            bln_inject(buf, lv[k].pw, lv[k].px0, lv[k].py0,
                       base + lv[k + 1].off, lv[k + 1].pw,
                       lv[k + 1].qw, lv[k + 1].qh,
                       lv[k + 1].qx0, lv[k + 1].qy0);
        int32_t cw = lv[k].pw, ch = lv[k].ph;
        bln_filter(buf, lv[k].pw, &cw, &ch, r);
        /* now buf holds the qw x qh field at row stride pw */
    }

    /* emit: rounding shift into int32 (caller chose out_shift to fit) */
    const int64_t *f = base + lv[0].off;
    const int32_t fs = lv[0].pw;
    const int32_t sh = p->out_shift;
    const int64_t half = (sh > 0) ? ((int64_t)1 << (sh - 1)) : 0;
    for (int32_t j = 0; j < h; j++) {
        int32_t *orow = out + (size_t)j * (size_t)out_stride;
        const int64_t *frow = f + (size_t)j * (size_t)fs;
        for (int32_t i = 0; i < w; i++)
            orow[i] = (int32_t)((frow[i] + half) >> sh);
    }
    return 0;
}
