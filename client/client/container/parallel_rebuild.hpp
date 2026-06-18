//
//  parallel_rebuild.hpp
//  client
//
//  Created by Antony Searle on 13/6/2026.
//

#ifndef parallel_rebuild_hpp
#define parallel_rebuild_hpp

#include <cstdlib>

#include <optional>

namespace wry {

    template<typename T>
    struct ParallelRebuildAction {
        enum {
            NONE = 0,
            WRITE_VALUE,
            CLEAR_VALUE,
            MERGE_VALUE,
        } tag;
        T value;
    };

    // Combine for a plain value map: WRITE replaces, CLEAR erases, NONE keeps;
    // MERGE is not meaningful (no value-level union).  The combine's `old` arg is
    // the read in a read-modify-write -- a map whose values are themselves
    // mergeable (e.g. a set) supplies a combine that uses it (see WaitableMap).
    template<typename T>
    struct ParallelRebuildValueCombine {
        std::optional<T> operator()(const T* old, const ParallelRebuildAction<T>& a) const {
            using A = ParallelRebuildAction<T>;
            switch (a.tag) {
                case A::WRITE_VALUE: return a.value;
                case A::CLEAR_VALUE: return std::nullopt;
                case A::MERGE_VALUE: abort(); // not meaningful for a plain map
                case A::NONE:        break;   // callers drop NONE before here
            }
            return old ? std::optional<T>(*old) : std::nullopt;
        }
    };

}

#endif /* parallel_rebuild_hpp */
