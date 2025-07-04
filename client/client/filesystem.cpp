//
//  filesystem.cpp
//  client
//
//  Created by Antony Searle on 24/12/2023.
//

#include "filesystem.hpp"

namespace wry {
    
    String string_from_file(const std::filesystem::path& v) {
        FILE* f = fopen(v.c_str(), "rb");
        assert(f);
        int result = fseek(f, 0, SEEK_END);
        assert(result == 0);
        long n = ftell(f);
        assert(n != -1);
        result = fseek(f, 0, SEEK_SET);
        assert(result == 0);
        String s;
        s.chars.may_write_back(n + 1);
        size_t m = fread(s.chars.data(), 1, n, f);
        assert(m == n);
        result = fclose(f);
        assert(result == 0);
        s.chars.did_write_back(m);
        s.chars.push_back('\0');
        s.chars.pop_back();
        return s;
    }
    
} // namespace wry
