//
//  assert.hpp
//  client
//
//  Created by Antony Searle on 9/10/2023.
//

#ifndef assert_hpp
#define assert_hpp

#include <cassert>
#include <cstdlib>

// We wrap <cassert> and implement some related macros in terms of the platform
// assert

// TODO: CHECK needs a better name
#define CHECK(x) do { if(!(x)) [[unlikely]] std::abort(); } while(0)

// Contracts

#define precondition CHECK
#define postcondition CHECK

#define precondition_debug assert
#define postcondition_debug assert


#endif /* assert_hpp */
