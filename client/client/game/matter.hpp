//
//  matter.hpp
//  client
//
//  Created by Antony Searle on 5/7/2026.
//

#ifndef matter_hpp
#define matter_hpp

#include <cstdint>
#include <utility>
#include <cstring>

namespace wry {

    // Matter codes: the payload of an ENUMERATION(meta=MATTER) Term,
    // identifying a kind of physical object.
    //
    // Matter Terms are conserved.  They are fungible values (all
    // shipping containers are the same bit pattern); what is conserved
    // is the count of each kind.  Machines move matter -- LOAD takes it
    // from a cell, STORE waits for the destination and places it, DROP
    // puts it down ahead, EXCHANGE swaps -- but never copy or destroy
    // it; DUPLICATE and OVER refuse matter, and arithmetic ignores it.
    // Only Source entities create matter and only Sink entities destroy
    // it.  See the gates in machine.cpp.
    //
    // Codes are save-format constants: append only, never renumber.

#define WRY_MATTER_X \
X(MATTER_SHIPPING_CONTAINER),\

    enum MATTER
    : int32_t {

#define X(Y) Y

        WRY_MATTER_X

#undef X

    };

    constexpr std::pair<MATTER, const char*> MATTER_NAMES[] = {

#define X(Y) { Y, #Y }

        WRY_MATTER_X

#undef X

    };

#undef WRY_MATTER_X

    constexpr MATTER MATTER_from_name(const char* name) {
        for (std::size_t i = 0; i != std::size(MATTER_NAMES); ++i) {
            if (!std::strcmp(MATTER_NAMES[i].second, name)) {
                return MATTER_NAMES[i].first;
            }
        }
        return MATTER{-1};
    }

    constexpr const char* name_from_MATTER(MATTER value) {
        for (std::size_t i = 0; i != std::size(MATTER_NAMES); ++i) {
            if (MATTER_NAMES[i].first == value) {
                return MATTER_NAMES[i].second;
            }
        }
        return nullptr;
    }

} // namespace wry

#endif /* matter_hpp */
