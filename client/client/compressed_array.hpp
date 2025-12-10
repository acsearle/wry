//
//  compressed_array.hpp
//  client
//
//  Created by Antony Searle on 5/11/2025.
//

#ifndef compressed_array_hpp
#define compressed_array_hpp

#include "algorithm.hpp"
#include "bit.hpp"
#include "type_traits.hpp"

namespace wry {

    using bit::popcount;
    
#pragma mark Mutable compressed array tools
    
    template<typename BITMAP>
    BITMAP bitmask_for_index(int index) {
        return (BITMAP)1 << index;
    }
    
    template<typename BITMAP>
    BITMAP bitmask_below_index(int index) {
        return ~(~(BITMAP)0 << index);
    }
    
    template<typename BITMAP>
    BITMAP bitmask_above_index(int index) {
        return ~(BITMAP)1 << index;
    }
    
    template<typename BITMAP>
    bool bitmap_get_for_index(BITMAP bitmap, int index) {
        return bitmap & bitmask_for_index<BITMAP>(index);
    }
    
    
    template<typename BITMAP>
    void bitmap_set_for_index(BITMAP& bitmap, int index) {
        bitmap |= bitmask_for_index<BITMAP>(index);
    }
    
    template<typename BITMAP>
    void bitmap_clear_for_index(BITMAP& bitmap, int index) {
        bitmap &= ~bitmask_for_index<BITMAP>(index);
    }
    
    // A compressed array is a bitmap and an array of T that compactly
    // represents std::array<std::optional<T>, 64>
    
    // The T for a given index, if it exists, is located in the underlying
    // array at the compressed_index = popcount(bitmap & ~(~0 << index))
    
    // Typically they are embedded in larger structures and use flexible
    // member arrays.  We cannot rely on them being consecutive in memory
    // so we don't reify the concept, instead passing it as arguments to
    // free functions.
    
    // Though public AMT nodes immutable, it can be useful to mutate newly
    // constructed nodes through intermediate states.  Internal methods
    // often follow a pattern of clone-and-modify.
    
    template<typename BITMAP>
    bool compressed_array_contains_for_index(BITMAP bitmap, int index) {
        return bitmap_get_for_index(bitmap, index);
    }
    
    template<typename BITMAP>
    int compressed_array_get_compressed_index_for_index(BITMAP bitmap, int index) {
        return popcount(bitmap & bitmask_below_index<BITMAP>(index));
    }
    
    template<typename BITMAP, typename T>
    bool compressed_array_try_get_for_index(BITMAP bitmap,
                                            T* _Nonnull array,
                                            int index,
                                            std::remove_const_t<T>& victim) {
        bool result = compressed_array_contains_for_index(bitmap, index);
        if (result) {
            victim = array[compressed_array_get_compressed_index_for_index(bitmap,
                                                                           index)];
        }
        return result;
    }
    
    template<typename BITMAP>
    int compressed_array_get_compressed_size(BITMAP bitmap) {
        return popcount(bitmap);
    }
    
    template<typename BITMAP, typename T>
    void compressed_array_insert_for_index(size_t debug_capacity,
                                           BITMAP& bitmap,
                                           T* _Nonnull array,
                                           int index,
                                           std::type_identity_t<T> value) {
        assert(!compressed_array_contains_for_index(bitmap, index));
        int compressed_index = compressed_array_get_compressed_index_for_index(bitmap, index);
        int compressed_size = compressed_array_get_compressed_size(bitmap);
        assert(debug_capacity > compressed_size);
        std::copy_backward(array + compressed_index,
                           array + compressed_size,
                           array + compressed_size + 1);
        bitmap_set_for_index(bitmap, index);
        array[compressed_index] = std::move(value);
    }
    
    template<typename BITMAP, typename T>
    T compressed_array_exchange_for_index(BITMAP& bitmap,
                                          T* _Nonnull array,
                                          int index,
                                          std::type_identity_t<T> value) {
        assert(compressed_array_contains_for_index(bitmap, index));
        int compressed_index = compressed_array_get_compressed_index_for_index(bitmap, index);
        return std::exchange(array[compressed_index], std::move(value));
    }
    
    template<typename BITMAP, typename T>
    bool compressed_array_insert_or_exchange_for_index(size_t debug_capacity,
                                                       BITMAP& bitmap,
                                                       T* _Nonnull array,
                                                       int index,
                                                       std::type_identity_t<T> value,
                                                       std::type_identity_t<T>& victim) {
        bool was_found = compressed_array_contains_for_index(bitmap, index);
        int compressed_index = compressed_array_get_compressed_index_for_index(bitmap, index);
        if (was_found) {
            // Preserve the old value
            victim = std::move(array[compressed_index]);
        } else {
            // Make a hole
            int compressed_size = compressed_array_get_compressed_size(bitmap);
            assert(debug_capacity > compressed_size);
            std::copy_backward(array + compressed_index,
                               array + compressed_size,
                               array + compressed_size + 1);
            bitmap_set_for_index(bitmap, index);
        }
        array[compressed_index] = std::move(value);
        return was_found;
    }
    
    template<typename BITMAP, typename T>
    void compressed_array_erase_for_index(BITMAP& bitmap,
                                          T* _Nonnull array,
                                          int index,
                                          std::type_identity_t<T>& victim) {
        int compressed_index = compressed_array_get_compressed_index_for_index(bitmap, index);
        int compressed_size = compressed_array_get_compressed_size(bitmap);
        victim = std::move(array[compressed_index]);
        std::copy(array + compressed_index + 1,
                  array + compressed_size,
                  array + compressed_index);
        bitmap_clear_for_index(bitmap, index);
    }
    
    
    template<typename BITMAP, typename T>
    bool compressed_array_try_erase_for_index(BITMAP& bitmap,
                                              T* _Nonnull array,
                                              int index,
                                              std::type_identity_t<T>& victim) {
        bool was_found = compressed_array_contains_for_index(bitmap, index);
        if (was_found) {
            int compressed_index = compressed_array_get_compressed_index_for_index(bitmap, index);
            int compressed_size = compressed_array_get_compressed_size(bitmap);
            victim = std::move(array[compressed_index]);
            std::copy(array + compressed_index + 1,
                      array + compressed_size,
                      array + compressed_index);
            bitmap_clear_for_index(bitmap, index);
        }
        return was_found;
    }
    
    template<typename BITMAP, typename T, typename U, typename V, typename F>
    void transform_compressed_arrays(BITMAP b1,
                                     BITMAP b2,
                                     T const* _Nonnull v1,
                                     U const* _Nonnull v2,
                                     V* _Nonnull v3,
                                     const F& f) {
        abort();
        uint64_t common = b1 | b2;
        while (common) {
            uint64_t next = common - 1;
            uint64_t select = common & ~next;
            *v3++ = f((b1 & select) ? v1++ : nullptr,
                      (b2 & select) ? v2++ : nullptr);
            common = next;
        }
    }
        
} // namespace wry

#endif /* compressed_array_hpp */
