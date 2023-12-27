//
//  model.cpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#include "model.hpp"

namespace wry {
    
    
    void model::_regenerate_uniforms() {
        
        // camera setup
        
        // rotate eye location to Z axis
        assert(_uniforms.camera_position_world.w == 1);
        float3 p = _uniforms.camera_position_world.xyz;
        quatf q(simd_normalize(p), simd_make_float3(0, 0, 1));
        float4x4 V = simd_matrix_translate(0, 0, -simd_length(p)) *
                              float4x4(q);
        
        simd_float4x4 F = simd_mul(matrix_perspective_right_hand(M_PI_2, 1, 5, 50),
                                   simd_matrix_scale(1, 1, -1, 1));
        
        float aspect_ratio = _viewport_size.x / _viewport_size.y;
        simd_float4x4 P = simd_mul(simd_matrix_scale(2, 2 * aspect_ratio, 1),
                                   F);
        
        _uniforms.view_transform = V;
        _uniforms.viewprojection_transform = simd_mul(P, V);
        
        _uniforms.inverse_view_transform = simd_inverse(_uniforms.view_transform);
        _uniforms.inverse_viewprojection_transform = simd_inverse(_uniforms.viewprojection_transform);

        // sun setup
        
        p = simd_normalize(simd_make_float3(2, -1, 4));
        //p = simd_normalize(simd_make_float3(-1, 0, 1)); // Factorio lol
        _uniforms.light_direction = p;
        _uniforms.radiance = 2.0f;

        
        // we have a lot of freedom in our choice of the shadow map projection;
        // it must map light_direction to clip_space Z, and it must not be
        // degenerate; everything else affects how shadow map pixels relate
        // to screen pixels
        
        // our typical view is of the ground from above, with most shadows cast
        // on the plane Z=0, and relatively weak perspective effects at work
        //
        // we can cast pixel-perfect shadows on this plane, and good quality
        // shadows near it, by choosing the shadow mapping which maps the
        // x,y,0,1 plane to pixels u/w,v/w
        
        // first, we shear the world to move the light source to 0,0,-1,0
        // - the ground plane is unaltered
        // - all shadows are now cast down the z axis
        // - the output x,y,w is the projection of a point on to the ground
        // - the output z is the distance above the plane of the point
        
        simd_float4x4 A = simd_matrix(make<float4>(1.0f, 0.0f, 0.0, 0.0f),
                                      make<float4>(0.0f, 1.0f, 0.0, 0.0f),
                                      make<float4>(-p.x / p.z,
                                                       -p.y / p.z,
                                                       -1.0f, 0.0f),
                                      make<float4>(0.0f, 0.0f, 0.0f, 1.0f));
        // :todo: for a light source not at infinity, we'll have to also
        // put in w = 1.0f - z / light_position.z or something; for a light
        // source inside / near the camera frustum, we need to go to a cube
        // map and investigate skewing the cube map in some alternative fashion
        
        // second, we apply the the camera's viewrojection_transform to x,y,w
        // but pass through z unchanged.
        // note that the clip space z will still be divided through by w, but
        // that will not affect its relative ordering
        simd_float4x4 B = simd_matrix(_uniforms.viewprojection_transform.columns[0],
                                      _uniforms.viewprojection_transform.columns[1],
                                      make<float4>(0.0f, 0.0f, 1.0f, 0.0f),
                                      _uniforms.viewprojection_transform.columns[3]);
        // :todo: ensure that result fits in the clip space z range though
        
        // finally, we rescale the projection to fit in the larger shadow map
        // texture.
        
        // :todo: what if the viewport is odd-sized?
        // :todo:
        
        simd_float4x4 C = simd_matrix_scale(_viewport_size.x / 2048.0f,
                                            _viewport_size.y / 2048.0f,
                                            1.0f);
        
        
        _uniforms.light_viewprojection_transform = simd_mul(C , simd_mul(B, A));
        _uniforms.light_viewprojectiontexture_transform = simd_mul(matrix_ndc_to_tc_float4x4,
                                                                   _uniforms.light_viewprojection_transform);

        // Though all shadow lookups on ground plane will be inside
        // the camera viewport-sized middle of the texture, geometry above the
        // ground plane can sample outside this region.  If we can comprehend
        // the camera frustum's projection onto the shadow texture, we may
        // be able to define a shadow viewport and reduce rendering.  For
        // example, in our standard view, the camera view is an irregular
        // pyramid, and the shadow map is an irregular column; both have the
        // same base, and we only need to extend the shadow map along the sides
        // of the column that the pyramid leans out of; when the light source
        // is approxmiately behind the camera, we don't need to extend the
        // shadow map at all
        
        
        
    }

} // namespace wry
