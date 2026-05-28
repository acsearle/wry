//
//  filesystem.cpp
//  client
//
//  Created by Antony Searle on 24/12/2023.
//

#include "filesystem.hpp"

namespace wry {

    ContiguousDeque<byte> bytes_from_file(const std::filesystem::path& v) {
        FILE* f = fopen(v.c_str(), "rb");
        assert(f);
        int result = fseek(f, 0, SEEK_END);
        assert(result == 0);
        long n = ftell(f);
        assert(n != -1);
        result = fseek(f, 0, SEEK_SET);
        assert(result == 0);
        ContiguousDeque<byte> out;
        out.may_write_back(n + 1);
        size_t m = fread(out.data(), 1, n, f);
        assert(m == (size_t)n);
        result = fclose(f);
        assert(result == 0);
        out.did_write_back(m);
        return out;
    }

    String string_from_file(const std::filesystem::path& v) {
        ContiguousDeque<byte> raw = bytes_from_file(v);
        // Reinterpret the byte buffer as a char buffer.  This is the one
        // boundary cast we accept: bytes that pass UTF-8 validation enter
        // the "validated text" world as chars.
        ContiguousDeque<char> chars(
            (char*)std::exchange(raw._allocation_begin, nullptr),
            (char*)std::exchange(raw._begin, nullptr),
            (char*)std::exchange(raw._end, nullptr),
            (char*)std::exchange(raw._allocation_end, nullptr)
        );
        return String(std::move(chars));  // validates; throws on invalid UTF-8
    }

} // namespace wry
