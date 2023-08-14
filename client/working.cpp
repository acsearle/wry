//
//  working.cpp
//  client
//
//  Created by Antony Searle on 5/8/2023.
//

#include "test.hpp"

namespace wry {
    
    define_test("projection") {
        
        double c = 1.0 / 2.0;
        double s = sqrt(3.0) / 2.0;
        
        double b = 1.0;
        
        for (int x = 1; x != 10; ++x) {
            for (int y = 0; y <= x; ++y) {
                
                double fx2 = x * c - y * s;
                double fy2 = x * s + y * c;
                
                int x2 = (int) round(fx2);
                int y2 = (int) round(fy2);
                
                double d1 = hypot(x, y);
                double d2 = hypot(x2, y2);
                double d3 = hypot(x2 - x, y2 - y);
                
                double d = (d1 + d2 + d3) / 3;
                double m = pow(d1/d-1, 2) + pow(d2/d-1, 2) + pow(d3/d-1, 2);
                
                printf("%d %d -> %g\t\t%g %g", x, y, m, fx2, fy2);
                
                if (m <= b) {
                    printf(" <---- best (%g %g %g)", d1, d2, d3);
                    b = m;
                }
                printf("\n");

            }
            
            
            //
            //      +
            //      o o o o + o
            //      o o o o o o
            //      o o o o o o
            //      o + o o o o
            //      o o o o o +
            //
            //

            // (3, 3) -> (-1, 4) -> (4, -1)
            // (6, 0) -> (3, 5)
            // (6, 6) -> (-2, 8)
            // (8, 0) -> (4, 7)
            // (8, 8) -> (-3, 11)
            
            // sqrt(50)/2 vs sqrt(18)
            // 3.53 vs 4.24 = 0.8333 vs 0.866
            
            // sqrt(50/18)/2
            // sqrt(25*25*2/(2*3*3))/2 = 5/6
            
            // so, scale by
            // sqrt(3) / 2 / (5 / 6)
            // 3 * sqrt(3) / 5
            
            
        }
        
    };
    
    define_test("fraction approximation") {
        
        double b = 1.0;
        double q = 1.0 / sqrt(3.0);
        for (int i = 0; i != 64; ++i) {
            double p = i * q;
            double r = round(p);
            double m = pow(r/p-1,2);
            printf("%g/%d -> %g", r,i,m);
            if (m < b) {
                printf(" <-- best");
                b = m;
            }
            printf("\n");
        }
    };
    
    define_test("shadow") {
        
        // 256
        // 8
        
        double* p = (double*) calloc(256*256, 8);
        double z2 = 8;
                
        for (int i = 0; i != 256; ++i) {
            double y = i - 127.5;
            for (int j = 0; j != 256; ++j) {
                double x = j - 127.5;
                double a = x*x + y*y + 8.0;
                double b = 1.0 / (a*a);
                p[j + i*256] = b;
            }
        }
        
        
    };
    
}
