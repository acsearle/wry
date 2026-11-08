//
//  xml.hpp
//  client
//
//  Created by Antony Searle on 8/3/2026.
//

#ifndef xml_hpp
#define xml_hpp

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wry::xml {

    // Rudimentary XML parsing to support SVG.
    //
    // Supported: elements, attributes (single- or double-quoted), empty-
    // element tags, character data, comments, CDATA sections, processing
    // instructions including the XML declaration (skipped), the predefined
    // entities and numeric character references.
    //
    // Not supported: DTDs (a <!DOCTYPE ...> is skipped, internal subset
    // and all, and its entity declarations are not honoured), namespaces
    // beyond keeping the prefix as part of the name, encodings other than
    // UTF-8.
    //
    // Names are views into the source text, which must outlive the tree.
    // Attribute values and text are decoded copies.

    struct Element {
        std::string_view name;
        std::vector<std::pair<std::string_view, std::string>> attributes;
        std::vector<Element> children;
        // The element's character data, concatenated (child elements'
        // text is not included).
        std::string text;

        // The attribute's value, or nullptr if absent.
        std::string const* attribute(std::string_view key) const;
    };

    struct Document {
        // A synthetic element with an empty name holding the top-level
        // elements (well-formed XML has exactly one).
        Element root;
        // Empty on success.  On failure, what went wrong and where; the
        // tree holds what was parsed before the error.
        std::string error;
        std::size_t error_offset = 0;

        bool ok() const { return error.empty(); }
    };

    Document parse(std::string_view source);

    void print(Element const& a, int depth = 0);

} // namespace wry::xml

#endif /* xml_hpp */
