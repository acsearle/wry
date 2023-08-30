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
        string s("/Users/antony/Desktop/assets/");
        s.append(name);
        s.append(".");
        s.append(ext);
        return s;
    }

}
