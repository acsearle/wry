//
//  xml.hpp
//  client
//
//  Created by Antony Searle on 8/3/2026.
//

#ifndef xml_hpp
#define xml_hpp

#include <utility>
#include <vector>

#include "string_view.hpp"

namespace wry::xml {
    
    // Rudimentary XML parsing to support SVG
    
    struct Content {
        enum { TEXT, ELEMENT } tag;
        StringView text;
        StringView name;
        std::vector<std::pair<StringView, StringView>> attributes;
        std::vector<Content> content;
    };
    
    void print(Content const& a);
    
    std::vector<Content> parse(StringView& v);
    
    
} // wry

#endif /* xml_hpp */
