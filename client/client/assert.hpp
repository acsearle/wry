//
//  assert.hpp
//  client
//
//  Created by Antony Searle on 9/10/2023.
//

#ifndef assert_hpp
#define assert_hpp

#include <cassert>

#define assert_false(X) assert(!(X))
#define assert_eq(X, Y) assert((X) == (Y))
#define assert_ne(X, Y) assert((X) != (Y))

#define precondition assert
#define postcondition assert

#endif /* assert_hpp */
