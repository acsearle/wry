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
    
    matrix<RGBA8Unorm_sRGB> from_png(string_view v) {
        png_image a;
        memset(&a, 0, sizeof(a));
        a.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_file(&a, string(v).c_str())) {
            printf("png_image_begin_read_from_file -> \"%s\"\n", a.message);
            abort();
        }
        a.format = PNG_FORMAT_RGBA;
        matrix<RGBA8Unorm_sRGB> result(a.height, a.width);
        if (!png_image_finish_read(&a, nullptr, result.data(), (png_int_32) result.bytes_per_row(), nullptr)) {
            printf("png_image_finish_read -> \"%s\"\n", a.message);
            abort();
        }
        png_image_free(&a);
        return result;
    }
    
    matrix<simd_float4> multiply_alpha(const matrix<RGBA8Unorm_sRGB>& source) {
        matrix<simd_float4> result(source.get_minor(), source.get_major());
        for (std::size_t i = 0; i != source.get_minor(); ++i) {
            for (std::size_t j = 0; j != source.get_major(); ++j) {
                const RGBA8Unorm_sRGB& x = source(i, j);
                float alpha = x.a;
                result(i, j) = simd_make_float4(x.r * alpha,
                                                x.g * alpha,
                                                x.b * alpha,
                                                alpha);
            }
        }
        return result;
    }
    
    void multiply_alpha_inplace(matrix<RGBA8Unorm_sRGB>& target) {
        for (auto&& row : target)
            for (auto&& x : row) {
                uchar* p = _multiply_alpha_table[x.a._];
                x.r._ = p[x.r._];
                x.g._ = p[x.g._];
                x.b._ = p[x.b._];
            }
    }
    
    void to_png(const matrix<RGBA8Unorm_sRGB>& source, string_view filename) {
        png_image a = {};
        a.format = PNG_FORMAT_RGBA;
        a.height = (png_uint_32) source.get_minor();
        a.version =  PNG_IMAGE_VERSION;
        a.width = (png_uint_32) source.get_major();
        string filename0(filename);
        png_image_write_to_file(&a, filename0.c_str(), 0, source.data(),
                                (png_int_32) source.bytes_per_row(), nullptr);
        std::cout << a.message << std::endl;
        png_image_free(&a);
    }
    
   
    
    /*
    void divide_alpha(image& img) {
        assert(false); // this is broken for sRGB
        for (auto&& row : img)
            for (auto&& px : row) {
                px.r = _divide_alpha_table[px.a][px.r];
                px.g = _divide_alpha_table[px.a][px.g];
                px.b = _divide_alpha_table[px.a][px.b];
            }
    }
    */
    
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
    
    /*
    template<typename T>
    void draw_rect(image<T>& img, ptrdiff_t x, ptrdiff_t y, ptrdiff_t width, ptrdiff_t height, T c) {
        for (auto j = y; j != y + height; ++j)
            for (auto i = 0; i != x + width; ++i)
                img(i, j) = c;
    }
     */
    
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
    
    /*
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
    
    void halve(imagef& a) {
        imagef b(a.height() >> 1, a.width() >> 1);
        for (std::size_t i = 0; i != a.height(); ++i) {
            for (std::size_t j = 0; j != a.width(); ++j) {
                b(i >> 1, j >> 1) += a(i, j) * 0.25f;
            }
        }
        using std::swap;
        swap(a, b);
    }
    
    void inflate(image& a) {
        image b(a.height() << 1, a.width() << 1);
        for (std::size_t i = 0; i != b.width(); ++i)
            for (std::size_t j = 0; j != b.width(); ++j)
                b(i, j) = a(i >> 1, j >> 1);
        swap(a, b);

    }
    
    image to_RGB8Unorm_sRGB(const imagef& a) {
        image b(a.height(), a.width());
        for (std::size_t i = 0; i != a.height(); ++i) {
            for (std::size_t j = 0; j != a.width(); ++j) {
                b(i, j) = simd_uchar(simd::round(to_sRGB(a(i, j))));
            }
        }
        return b;
    }
     */

        
}

