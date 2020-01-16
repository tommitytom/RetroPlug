#pragma once

#include <string>

namespace base64 {
    static const int base64_dec_table[256] = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62, 63, 62, 62, 63, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
    7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,
    0,  0,  0, 63,  0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51 };

    std::string decode(const void* data, const size_t len) {
        unsigned char* p = (unsigned char*)data;
        int pad = len > 0 && (len % 4 || p[len - 1] == '=');
        const size_t length = ((len + 3) / 4 - pad) * 4;
        std::string str(length / 4 * 3 + pad, '\0');

        for (size_t i = 0, j = 0; i < length; i += 4) {
            int n = base64_dec_table[p[i]] << 18 | base64_dec_table[p[i + 1]] << 12 | base64_dec_table[p[i + 2]] << 6 | base64_dec_table[p[i + 3]];
            str[j++] = n >> 16;
            str[j++] = n >> 8 & 0xFF;
            str[j++] = n & 0xFF;
        }

        if (pad) {
            int n = base64_dec_table[p[length]] << 18 | base64_dec_table[p[length + 1]] << 12;
            str[str.size() - 1] = n >> 16;

            if (len > length + 2 && p[length + 2] != '=') {
                n |= base64_dec_table[p[length + 2]] << 6;
                str.push_back(n >> 8 & 0xFF);
            }
        }

        return str;
    }
}
