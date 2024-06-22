//
//  wry/gc/HeapString.hpp
//  client
//
//  Created by Antony Searle on 22/6/2024.
//

#ifndef wry_gc_HeapString_hpp
#define wry_gc_HeapString_hpp

#include <string_view>

#include "object.hpp"

namespace wry::gc {
    
    struct HeapString : Object {
        std::size_t _hash;
        std::size_t _size;
        char _bytes[0];
        static void* operator new(std::size_t count, std::size_t extra);
        static HeapString* make(std::size_t hash, std::string_view view);
        static HeapString* make(std::string_view view);
        std::string_view as_string_view() const;
        HeapString();
    }; // struct HeapString
    
}

#endif /* wry_gc_HeapString_hpp */
