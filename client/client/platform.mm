//
//  platform.mm
//  client
//
//  Created by Antony Searle on 27/6/2023.
//

#include "platform.hpp"
#include "string_view.hpp"
#include "string.hpp"

#import <Foundation/Foundation.h>

namespace wry {
    
    string path_for_resource(string_view name, string_view ext) {
        return [[[NSBundle mainBundle] pathForResource:[NSString stringWithUTF8String:string(name).c_str()]
                                                ofType:[NSString stringWithUTF8String:string(ext).c_str()]] UTF8String];
    }

}
