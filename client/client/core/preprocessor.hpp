//
//  preprocessor.hpp
//  client
//
//  Created by Antony Searle on 16/2/2024.
//

#ifndef preprocessor_hpp
#define preprocessor_hpp

#define WRY_CONCATENATE_TOKENS(A, B) WRY_CONCATENATE_TOKENS_AGAIN(A, B)
#define WRY_CONCATENATE_TOKENS_AGAIN(A, B) A##B

#endif /* preprocessor_hpp */
