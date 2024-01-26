//
//  enum.hpp
//  client
//
//  Created by Antony Searle on 12/10/2023.
//

#ifndef enum_hpp
#define enum_hpp

#include <optional>

#include "parse.hpp"

namespace wry {
    
    // enum tools
    
    // Can we introspect enums by parsing (a copy of) the same header files?
    // enum [class|struct] [NAME] [: TYPE] { NAME [ = EXPRESSION ]*[, NAME][,] } [NAME];
    
    auto enum_parse_definition(auto& x) {
        return [&x](auto& v) -> bool {
            String identifier;
            int64_t value;
            if (match_and(match_spaces(),
                          parse_identifier(identifier),
                          match_spaces(),
                          match_optional(match_character(u8'='),
                                         match_spaces(),
                                         parse_number(value)),
                          match_spaces())(v)) {
                x.emplace_back(std::move(identifier), std::move(value));
                return true;
            }
            return false;
                      
        };
    }
    
    struct enum_parse_result {
        String name;
        String type;
        String instance;
        array<std::pair<String, int64_t>> values;
    };
    
    auto enum_parse_declaration(enum_parse_result& x) {
        return [&x](auto& v) -> bool {
            return match_and(match_spaces(),
                             match_string(u8"enum"),
                             match_spaces(),
                             match_optional(parse_identifier(x.name),
                                            match_spaces(),
                                            match_character(u8':'),
                                            match_spaces(),
                                            parse_identifier(x.type)),
                             match_spaces(),
                             match_character(u8'{'),
                             match_spaces(),
                             match_delimited(enum_parse_definition(x.values),
                                             match_character(u8',')),
                             match_spaces(),
                             match_character(u8'}'),
                             match_spaces(),
                             match_optional(parse_identifier(x.instance)),
                             match_spaces(),
                             match_character(u8';')
                             )(v);
            
        };
    }
    
    
    
} // namespace wry

#endif /* enum_hpp */
