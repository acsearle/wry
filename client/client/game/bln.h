/* bln.h — band-limited, isotropic, integer-exact 2D noise on a lazy chunk
 * grid. Reference implementation & derivation: bandlimited_noise.py.
 *
 * Contract: every output cell is a pure function of (params, world x, y).
 * Chunks generated independently agree BITWISE on shared cells, on any
 * CPU/GPU/compiler (integer arithmetic only). No allocation, no globals,
 * no threads: caller supplies output and scratch.
 *
 * weights[k] is octave k's amplitude, k=0 the finest (= output resolution);
 * for fBm make weights grow by 2^H per octave toward the coarse end.
 * Output is the int64 field rounding-shifted by out_shift into int32.
 * Hard-safe: out_shift >= ceil(log2(sum of weights)); real fBm content sits
 * ~6 sigma below that bound, so smaller shifts are usable once validated.
 */
#ifndef BLN_H
#define BLN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLN_MAX_LEVELS 16
#define BLN_MAX_PASSES 6

typedef struct bln_params {
    uint32_t seed;
    int32_t  levels;     /* octave count, 1..BLN_MAX_LEVELS                  */
    int32_t  passes;     /* [1 2 1] passes per axis, 1..BLN_MAX_PASSES:      */
                         /* kernel B_{2*passes}, sigma = sqrt(passes/2) cells */
    int32_t  out_shift;  /* final rounding right-shift into int32, 0..31     */
    uint16_t weights[BLN_MAX_LEVELS];  /* per-octave amplitude, [0] = finest */
} bln_params;

/* Scratch bytes needed for a w x h request. Position-independent upper
 * bound (actual use can be slightly less depending on origin parity).
 * Returns 0 if the params or dimensions are invalid. */
size_t bln_scratch_size(const bln_params *p, int32_t w, int32_t h);

/* Fill out[y*out_stride + x], 0 <= x < w, 0 <= y < h, with the field over
 * the world rect [x0, x0+w) x [y0, y0+h). out_stride is in elements.
 * scratch: at least bln_scratch_size(p, w, h) bytes, 8-byte aligned;
 * contents are trashed. Reentrant for distinct out/scratch.
 * Returns 0 on success, -1 on invalid arguments. */
int bln_generate(const bln_params *p, int64_t x0, int64_t y0,
                 int32_t w, int32_t h,
                 int32_t *out, ptrdiff_t out_stride, void *scratch);

#ifdef __cplusplus
}
#endif

#endif /* BLN_H */
