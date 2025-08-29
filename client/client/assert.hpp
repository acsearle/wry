//
//  assert.hpp
//  client
//
//  Created by Antony Searle on 9/10/2023.
//

#ifndef assert_hpp
#define assert_hpp

#include <cassert>

// We wrap <cassert> and implement some related macros in terms of the platform
// assert

// Rust

#define assert_eq(X, Y) assert((X) == (Y))
#define assert_ne(X, Y) assert((X) != (Y))

#define debug_assert assert(X)
#define debug_assert_eq(X, Y) debug_assert((X) == (Y))
#define debug_assert_ne(X, Y) debug_assert((X) != (Y))

// Contracts

#define precondition assert
#define postcondition assert

#endif /* assert_hpp */
