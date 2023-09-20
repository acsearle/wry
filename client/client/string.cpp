//
//  string.cpp
//  client
//
//  Created by Antony Searle on 26/6/2023.
//

#include "string.hpp"

namespace wry {
        
    string string_from_file(string_view v) {
        // todo: filesystem for better length?
        string s(v);
        FILE* f = fopen(s.c_str(), "rb");
        assert(f);
        s.clear();
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        s._bytes.may_write_back(n + 1);
        size_t m = fread(s._bytes.data(), 1, n, f);
        fclose(f);
        s._bytes.did_write_back(m);
        s._bytes.push_back(0);
        s._bytes.pop_back();
        return s;
    }
    
}
