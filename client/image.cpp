//
//  image.cpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#include <numeric>

#include <mach/mach_time.h>
#include <png.h>

#include "debug.hpp"
#include "image.hpp"

namespace wry {
    
    // PNG stores its RGB in sRGB color space, with non-premultiplied linear alpha.
    //
    // Consider a white/black checkerboard
    // Cells are (255, 255, 255, 255) and (0, 0, 0, 255)
    // Zoomed out, each makes a 50% contrubution
    // Which is to_sRGB(from_sRGB(255/255.0) * 0.5 + from_sRGB(0/255.0) * 0.5)
    // Result is (188, 188, 188, 255)
    
    // OpenGL pixel shaders work with linear color.  Textures are assumed linear
    // unless we specify GL_SRGB8_ALPHA8; then the samplers will perform from_sRGB
    // before filtering and returning a linear value to the shader.
    
    // sRGB is good, in that it concentrates bits in dark colours which are
    // perceptually more distinguishable.  GPU makes it free.  Makes meddling
    // with the images hard though.
    
    image from_png(string_view v) {
        png_image a;
        memset(&a, 0, sizeof(a));
        a.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_file(&a, string(v).c_str())) {
            printf("png_image_begin_read_from_file -> \"%s\"\n", a.message);
            abort();
        }
        a.format = PNG_FORMAT_RGBA;
        image c(a.height, a.width);
        if (!png_image_finish_read(&a, nullptr, c.data(), (png_int_32) c.stride() * sizeof(pixel), nullptr)) {
            printf("png_image_finish_read -> \"%s\"\n", a.message);
            abort();
        }
        png_image_free(&a);
        return c;
    }
    
    image from_png_and_multiply_alpha(string_view v) {
        // timer _((const char*) v.a._ptr);
        image c = from_png(v);
        multiply_alpha(c);
        return c;
    }
    
    void to_png(const image& img, const char* filename) {
        
        image img2(img);
        
        // divide alpha?
        
        png_image a;
        memset(&a, 0, sizeof(a));
        a.format = PNG_FORMAT_RGBA;
        a.height = (png_uint_32) img2.rows();
        a.version =  PNG_IMAGE_VERSION;
        a.width = (png_uint_32) img2.columns();
        png_image_write_to_file(&a, filename, 0, img2.data(), (png_int_32) img2.stride() * sizeof(pixel), nullptr);
        std::cout << a.message << std::endl;
        png_image_free(&a);
    }
    
    
    // Use a table to perform premultiplication in sRGB color space.  Lots faster
    // in debug mode, at least
    u8 (*_multiply_alpha_table)[256] = []() {
        u8 (*p)[256] = (u8 (*)[256]) malloc(256 * 256);
        // We expect to have long blocks of alpha = 0 or alpha = 255, so to be
        // cache friendly, we make color be the minor index
        
        // 64k table same size as modern L1 cache?
        u8 alpha = 0;
        do {
            u8 color = 0;
            do {
                // perf: we can compute from_sRGB(u8) via a table
                // perf: we can compute from_sRGB(u8) without the / 255.0 by
                //       adjusting the other constants
                // perf: we can transpose the loop and compute from_sRGB(color / 255.0) / 255.0 once for all alpha
                p[alpha][color] = round(to_sRGB(from_sRGB(color / 255.0f) * alpha / 255.0f) * 255.0f);
            } while (++color);
        } while (++alpha);
        
        return p;
    }();
    
    f32* _from_sRGB_table = []() {
        f32* p = (f32*) malloc(256 * 4);
        u8 color = 0;
        do {
            p[color] = from_sRGB(color / 255.0f);
        } while (++color);
        return p;
    }();
    
    void multiply_alpha(image& img) {
        // To premultiply sRGB data we have to convert the color channel from
        // sRGB to linear, multiply by alpha, and then convert back to sRGB, which
        // is a whole lotta floating-point math
        
        for (auto&& row : img)
            for (auto&& px : row) {
                px.r = _multiply_alpha_table[px.a][px.r];
                px.g = _multiply_alpha_table[px.a][px.g];
                px.b = _multiply_alpha_table[px.a][px.b];
                
            }
        
        // sprintf(s, "%.2f ms | %lu quads\n%lux%lu\nf%d", (new_t - old_t) * 1e-6, n, _width, _height, frame);
        
    }
    
    u8 (*_divide_alpha_table)[256] = []() {
        u8 (*p)[256] = (u8 (*)[256]) malloc(256 * 256);
        // We expect to have long blocks of alpha = 0 or alpha = 255, so to be
        // cache friendly, we make color be the minor index
        
        std::memset(p, 0, 256);
        u8 alpha = 1;
        do {
            u8 color = 0;
            do {
                // perf: we can compute from_sRGB(u8) via a table
                // perf: we can compute from_sRGB(u8) without the / 255.0 by
                //       adjusting the other constants
                // perf: we can transpose the loop and compute from_sRGB(color / 255.0) / 255.0 once for all alpha
                p[alpha][color] = std::clamp<float>(round(to_sRGB(from_sRGB(color / 255.0f) / (alpha / 255.0f)) * 255.0f), 0, 255);
            } while ((u8) ++color);
        } while ((u8) ++alpha);
        
        return p;
    }();
    
    
    void divide_alpha(image& img) {
        assert(false); // this is broken for sRGB
        for (auto&& row : img)
            for (auto&& px : row) {
                px.r = _divide_alpha_table[px.a][px.r];
                px.g = _divide_alpha_table[px.a][px.g];
                px.b = _divide_alpha_table[px.a][px.b];
            }
    }
    
    
    /*
     void halve(image& img) {
     ptrdiff_t w = img.columns() / 2;
     ptrdiff_t h = img.rows() / 2;
     pixel* d = img._store.data();
     for (ptrdiff_t i = 0; i != h; ++i)
     for (ptrdiff_t j = 0; j != w; ++j)
     *d++ = (  *(_data + i * 2 * _stride + j * 2)
     + *(_data + (i * 2 + 1) * _stride + j * 2)
     + *(_data + i * 2 * _stride + j * 2 + 1)
     + *(_data + (i * 2 + 1) * _stride + j * 2 + 1)) / 4;
     _data = _allocation;
     _width = w;
     _height = h;
     _stride = w;
     }*/
    
    /*
     void image::bevel() {
     for (ptrdiff_t j = 0; j != _height; ++j) {
     double a = sin(j * M_PI / _height);
     a *= a;
     for (ptrdiff_t i = 0; i != _width; ++i) {
     double b = sin(i * M_PI / _width);
     b *= b * a;
     pixel& p = operator()(i, j);
     p.rgb = p.rgb * b;
     p.a = 0;
     }
     }
     }*/
    
    void draw_rect(image& img, ptrdiff_t x, ptrdiff_t y, ptrdiff_t width, ptrdiff_t height, pixel c) {
        for (auto j = y; j != y + height; ++j)
            for (auto i = 0; i != x + width; ++i)
                img(i, j) = c;
    }
    
    /*
     void image::clear(pixel c) {
     draw_rect(0, 0, _width, _height, c);
     }
     
     image image::with_size(ptrdiff_t width, ptrdiff_t height) {
     image a;
     a._allocation = static_cast<pixel*>(malloc(sizeof(pixel) * width * height));
     a._data = a._allocation;
     a._height = height;
     a._stride = width;
     a._width = width;
     return a;
     }
     */
    
    
    // All these image filtering operations are broken.  It is not enough that
    // the
    
    void blur(matrix_view<pixel> a, const_matrix_view<pixel> b) {
        assert(false); // sRGB
        std::vector<double> c(5, 0.0);
        double* d = c.data() + 2;
        for (ptrdiff_t k = -2; k != 3; ++k)
            c[k + 2] = exp(-0.5 * k * k);
        double e = std::accumulate(c.begin(), c.end(), 0.0);
        for (ptrdiff_t i = 0; i != b.rows(); ++i)
            for (ptrdiff_t j = 0; j != b.columns(); ++j) {
                simd_double4 f = {};
                for (ptrdiff_t k = 0; k != 5; ++k)
                    f += simd_double(b(i, j + k)) * d[k];
                a(i, j) = simd_uchar_sat(f / e);
            }
    }
    
    bool is_blank(const_matrix_view<pixel> v) {
        for (i64 i = 0; i != v.rows(); ++i) {
            for (i64 j = 0; j != v.columns(); ++j) {
                if (v(i, j).a)
                    return false;
            }
        }
        return true;
    }
    
    simd_long2 prune(matrix_view<pixel>& v) {
        simd_long2 o = {};
        while (v.rows() && is_blank(v.sub(0, 0, 1, v.columns()))) {
            ++o.y;
            v = v.sub(1, 0, v.rows() - 1, v.columns());
        }
        while (v.rows() && is_blank(v.sub(v.rows() - 1, 0, 1, v.columns())))
            v = v.sub(0, 0, v.rows() - 1, v.columns());
        while (v.columns() && is_blank(v.sub(0, 0, v.rows(), 1))) {
            ++o.x;
            v = v.sub(0, 1, v.rows(), v.columns() - 1);
        }
        while (v.columns() && is_blank(v.sub(0, v.columns() - 1, v.rows(), 1)))
            v = v.sub(0, 0, v.rows(), v.columns() - 1);
        return o;
    }
    
    void dilate(image& a) {
        assert(false); // sRGB
        DUMP(a.rows());
        image b(a.rows() + 4, a.columns() + 4);
        b.sub(2, 2, a.rows(), a.columns()) = a;
        swap(a, b);
        double m[3][3] = {{0.6,0.9,0.6},{0.9,1.0,0.9},{0.6,0.9,0.6}};
        b = a.sub(1, 1, a.rows() - 2, a.columns() - 2);
        for (ptrdiff_t i = 0; i != b.rows(); ++i)
            for (ptrdiff_t j = 0; j != b.columns(); ++j) {
                double c = 0;
                for (ptrdiff_t k = 0; k != 3; ++k)
                    for (ptrdiff_t l = 0; l != 3; ++l) {
                        c = std::max(c, a(i + k, j + l).a * m[k][l]);
                    }
                b(i, j).a = c;
            }
        swap(a, b);
        DUMP(a.rows());
    }
    
    // compositing and filtering need to be performed on linear values
    pixel compose(pixel a, pixel b) {
        assert(false); // broken for sRGB
        pixel c;
        auto o = 255 - b.a;
        c.r = (a.r * o + b.r * 255) / 255;
        c.g = (a.g * o + b.g * 255) / 255;
        c.b = (a.b * o + b.b * 255) / 255;
        c.a = (a.a * o + b.a * 255) / 255;
        return c;
    }
    
    void compose(matrix_view<pixel> background, const_matrix_view<pixel> foreground) {
        assert(false); // broken for sRGB
        for (ptrdiff_t i = 0; i != background.rows(); ++i)
            for (ptrdiff_t j = 0; j != background.columns(); ++j)
                background(i, j) = compose(background(i, j), foreground(i, j));
    }
    
}

