#pragma once
#include "types.hpp"

#include <string>

namespace ncore::base64 {
    static __forceinline std::string encode(const void* in, size_t in_len) noexcept {
        static constexpr const char const __encodingTable[] = {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
            'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
            'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
            'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
            'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
            'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
            'w', 'x', 'y', 'z', '0', '1', '2', '3',
            '4', '5', '6', '7', '8', '9', '+', '/'
        };

        if (!in) return std::string();

        size_t out_len = 4 * ((in_len + 2) / 3);
        std::string ret(out_len, '\0');
        char* p = ret.data();
        byte_t* data = (byte_t*)in;

        size_t i;
        for (i = 0; i < in_len - 2; i += 3) {
            *p++ = __encodingTable[(data[i] >> 2) & 0x3F];
            *p++ = __encodingTable[((data[i] & 0x3) << 4) | ((int)(data[i + 1] & 0xF0) >> 4)];
            *p++ = __encodingTable[((data[i + 1] & 0xF) << 2) | ((int)(data[i + 2] & 0xC0) >> 6)];
            *p++ = __encodingTable[data[i + 2] & 0x3F];
        }

        if (i < in_len) {
            *p++ = __encodingTable[(data[i] >> 2) & 0x3F];

            if (i == (in_len - 1)) {
                *p++ = __encodingTable[((data[i] & 0x3) << 4)];
                *p++ = '=';
            }
            else {
                *p++ = __encodingTable[((data[i] & 0x3) << 4) | ((int)(data[i + 1] & 0xF0) >> 4)];
                *p++ = __encodingTable[((data[i + 1] & 0xF) << 2)];
            }

            *p++ = '=';
        }

        return ret;
    }

    static __forceinline std::string decode(const std::string& input) noexcept {
        static constexpr const unsigned char const __decodingTable[] = {
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
            64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
            64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
        };

        std::string out;

        size_t in_len = input.size();
        size_t tail = in_len % 4;
        if (tail != 0 && tail != 2) {
            return std::string();
        }

        size_t out_len = (in_len + tail) / 4 * 3;
        if (input[in_len - 1] == '=') {
            out_len--;
        }

        if (input[in_len - 2] == '=') {
            out_len--;
        }
        out_len -= tail;

        out.resize(out_len);

        for (size_t i = 0, j = 0; i < in_len;) {
            ui32_t a = input[i] == '=' ? 0 & i++ : __decodingTable[static_cast<int>(input[i++])];
            ui32_t b = input[i] == '=' ? 0 & i++ : __decodingTable[static_cast<int>(input[i++])];
            ui32_t c = input[i] == '=' ? 0 & i++ : __decodingTable[static_cast<int>(input[i++])];
            ui32_t d = input[i] == '=' ? 0 & i++ : __decodingTable[static_cast<int>(input[i++])];

            ui32_t triple = (a << 3 * 6) + (b << 2 * 6) + (c << 1 * 6) + (d << 0 * 6);

            if (j < out_len) out[j++] = (triple >> 2 * 8) & 0xFF;
            if (j < out_len) out[j++] = (triple >> 1 * 8) & 0xFF;
            if (j < out_len) out[j++] = (triple >> 0 * 8) & 0xFF;
        }

        return out;
    }
}
