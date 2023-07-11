//
//  model.hpp
//  client
//
//  Created by Antony Searle on 8/7/2023.
//

#ifndef model_hpp
#define model_hpp

#include <memory>
#include <mutex>

#include "array.hpp"
#include "string.hpp"

namespace wry {

    struct model {
        
        std::mutex _mutex;
        array<string> _console;
        
        model() {
            _console.emplace_back("\"Behold, a [console]!\"");
            _console.emplace_back("");
        }
        
        ~model() {
            printf("~model\n");
        }
        
    };
    
} // namespace wry

#endif /* model_hpp */
