#version 3.7;

#include "functions.inc"

global_settings {
    assumed_gamma 1.0
    radiosity {
      pretrace_start 0.128
      pretrace_end   0.002
      count 500
      nearest_count 20
      error_bound 0.5
      recursion_limit 2
      low_error_factor 1.0
      gray_threshold 0.0
      minimum_reuse 0.005
      maximum_reuse 0.1
      brightness 1
      adc_bailout 0.005
    }
}

camera {
    orthographic
    direction <0, 0, -1>
    right <-1, 0, 0>
    up <0, 1, 0>
    location < 0, 0, 1 >
    scale 8
    // rotate <-90, 0, 0>
    
    rotate <-35.264, 0, 0>
    rotate <0, 45, 0>
    
    // location <0, 1/2, -sqrt(3/4)>
    // look_at <0, 0.0, 0>
}

background { color rgbt <0,0,0,1> }

sphere {
    <0, 0, 0>, 1000
    pigment {
        function { select(y, 0, 1) }
        color_map {
            [ 0 color <0, 0, 0> ]
            [ 1 color <1, 1, 1> * 2 ]
        }
    }
    finish {
        diffuse 0
        emission 1
    }
    no_image
    // rotate <90, 0, 0>
}

/*
light_source {
    <0,0,1>*8, <1,1,1>
    parallel
    point_at <0,0,0>
    rotate <-35.264, 0, 0>
    rotate <0, 135, 0>

}
*/

union {
    plane { <0, 1, 0>, -1 }
    // box { <-1, -1, -1>, <1,1,1> }
    // cylinder { <-1, 0, 0>, <1,0,0>, 1 }
    // box { <-1, 0, -1>, <1, 1, 1> }
    sphere { 0, 1 }
    pigment { color rgb <1,1,1>/2 }
    finish { diffuse 1 emission 0 }
}



/*
// basic finish
#default {
    texture {
        pigment {
            color <1,1,1>/2
        }
        finish {
            diffuse 1
            emission 0
        }
        /*
        pigment {
            average
            pigment_map {
                [
                gradient <0, 0, 1>
                phase 0.5
                color_map {
                    [ 0 color <0, 0, 0>]
                    [ 1 color <0, 0, 1>]
                }
                ]
                [
                gradient <0, 1, 0>
                phase 0.5
                color_map {
                    [ 0 color <0, 0, 0>]
                    [ 1 color <0, 1, 0>]
                }
                ]
                [
                gradient <1, 0, 0>
                phase 0.5
                color_map {
                    [ 0 color <0, 0, 0>]
                    [ 1 color <1, 0, 0>]
                }
                ]
            }
        }
        finish {
            diffuse 0
            emission 1
        }
        */
    }
}
*/


/*
// ground plane
object {
    intersection {
        union {
            plane {
                <0, 1, 0>, 0
                pigment { color <1,1,1> / 8 }
            }
            
            /*
            difference {
                plane { <0, 1, 0>, 1 }
                cylinder { <0, -2, 0>, <0, 2, 0>, 1 }
                pigment { color <1,1,1> / 4 }
                scale <sqrt(3)/2, 1/32, sqrt(3)/2>
                translate <0, 0, -1>
            }
            */
            difference {
                plane { <0, 1, 0>, 1 }
                plane { <1, 0, 0>, 0 }
                pigment { color <1,1,1> / 4 }
                rotate <0, -120, 0>
                scale <sqrt(3)/2, 1/32, sqrt(3)/2>
            }
            
            union {
                cylinder { <0, 0, 0>, <0, 1, 0>, 1 }
                pigment { color <1,1,1> / 2 }
                scale <sqrt(3)/2, 2/32, sqrt(3)/2>
                translate <sqrt(3)/2, 0, 0.5>
            }
            rotate <0, 120, 0 >
        }
        intersection {
            // plane { <0, 0, 1>, 0.5 }
            plane { <0, 0, 1>, 0.5 rotate <0, 120, 0> }
            plane { <0, 0, 1>, 0.5 rotate <0, -120, 0> }
            pigment { color <0,0,0> }
        }
    }
    rotate <0, 180+15, 0 >
}
*/


#declare wheel = object {
    union {
        difference {
            union {
                cylinder { <-24, 24, 0 >, <- 8, 24, 0>, 24 }
                cylinder { <  8, 24, 0 >, < 24, 24, 0>, 24 }
            }
            cylinder { <-32, 24, 0 >, < 32, 24, 0>, 12 }
            // pigment { color <1,1,1>/8 }
        }
        cylinder { <-28, 24,  0>, < 28, 24, 0>,  8 }
        cylinder { <- 6, 40,-16>, <  6, 40, -16>, 8 }
        cylinder { <- 6, 40, 16>, <  6, 40, +16>, 8 }
        cylinder { <-28, 56,  0>, < 28, 56,   0>, 8 }
        cylinder { <  0, 56,  0>, <  0, 62,   0>, 32 }
        translate <0, -4, 0>
        scale <1, 1, 1> / 512
        rotate <0, 30, 0 >
    }
}

#declare modular_transporter = union {
    object { wheel translate < +0.0625, 0, 0 > }
    object { wheel translate < +0.0625, 0, -0.125 > }
    object { wheel translate < +0.0625, 0, +0.125 > }
    object { wheel translate < -0.0625, 0, 0 > }
    object { wheel translate < -0.0625, 0, -0.125 > }
    object { wheel translate < -0.0625, 0, +0.125 > }
    object { box { < -63, 58, -95 >, <63, 64, 95 > } translate <0, -4, 0> scale <1, 1, 1> / 512 }
    // pigment { color < 0.5, 0.15625, 0.0 > }

}

/*
union {
    object { modular_transporter translate <-0.125, 0, -0.1875> }
    object { modular_transporter translate <-0.125, 0, +0.1875> }
    object { modular_transporter translate <+0.125, 0, -0.1875> }
    object { modular_transporter translate <+0.125, 0, +0.1875> }
    rotate <0, 30, 0>
}
*/



/*

union {
    sphere { <-0.3, 0.2, -0.3>, 0.2 }
    sphere { <-0.3, 0.2, +0.3>, 0.2 }
    sphere { <+0.3, 0.2, -0.3>, 0.2 }
    sphere { <+0.3, 0.2, +0.3>, 0.2 }
    box { <-0.5, 0.4, -0.5>, <0.5, 0.5, 0.5> }
}

*/

/*
cylinder {
    <0, 0, 0>, <0, 1, 0>, 0.5
}
*/


// box { <-0.4, 0.1, -0.4>, <+0.4, 0.2, +0.4> rotate <0, 30, 0> }


//sphere { <0, 0.5, 0>, 0.4 }


/*
difference {
    union {
        cylinder { <-0.5, 0.5, 0.0 >, <+0.5, 0.5, 0.0>, 0.5 }
        cylinder { <0.0, 0.5, -0.5 >, <0.0, 0.5, +0.5>, 0.5 }
    }
    union {
        cylinder { <-0.6, 0.5, 0.0 >, <+0.6, 0.5, 0.0>, 0.4 }
        cylinder { <0.0, 0.5, -0.6 >, <0.0, 0.5, +0.6>, 0.4 }
    }
    rotate <0, 45, 0 >
}
*/

//difference {
   // plane {
    //    <0, 1, 0>, 0
   // }
    // cylinder { < 0, -0.1, 0>, <0, 0.1, 0>, 0.4 }
//    box { < -0.4, -0.1, -0.4 >, <+0.4, +0.1, +0.4> }
//}

/*

union {
    cylinder { < -0.5, +0.2, -0.3 >, < -0.4, +0.2, -0.3 >, 0.2 }
    cylinder { < -0.5, +0.2, +0.3 >, < -0.3, +0.2, +0.3 >, 0.2 }
    cylinder { < +0.5, +0.2, -0.3 >, < +0.4, +0.2, -0.3 >, 0.2 }
    cylinder { < +0.5, +0.2, +0.3 >, < +0.3, +0.2, +0.3 >, 0.2 }
    cylinder { < -0.4, +0.2, +0.3 >, < +0.4, +0.2, +0.3 >, 0.1 }
    box { < -0.3, +0.2, -0.5 >, < +0.3, +0.5, +0.0 > }
    difference {
        union {
            box { < -0.5, +0.5, -0.5 >, < +0.5, +0.6, +0.5 > }
            box { < -0.2, +0.2, 0.0 >, < +0.2, +0.55, +0.5 > }
        }
        box { < -0.1, +0.3, 0.1 >, <+0.1, +0.65, +0.55 > }
    }
    rotate < 0, +45, 0 >
}
*/



/*
isosurface {
    function { f_noise3d(x * 10, y * 10, z * 10) * 0.1 + y }
    // function { f_ridged_mf(x * 10, y * 10, z * 10) * 0.1 + y }
    threshold 0.05
    max_gradient 2
    contained_by { box { <-0.5, -0.5, -0.5>, <+0.5, 0.5, +0.5> } }
}
*/
