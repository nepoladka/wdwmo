#pragma once
#include <windows.h>

#include <string>

//for compatibility with outdated code, instead of this better use ncore::strings::compatible_string::make_u8();
//#define u16tou8(SRC, SRCLEN, DST, DSTLEN) strcpy(DST, ncore::strings::compatible_string::make_u8(SRC).c_str())
//for compatibility with outdated code, instead of this better use ncore::strings::compatible_string::make_u8();
//#define u8tou16(SRC, SRCLEN, DST, DSTLEN) wcscpy(DST, ncore::strings::compatible_string::make_u16(SRC).c_str())

namespace ncore {
    namespace strings {
        class compatible_string {
        private:
            std::string _u8;
            std::wstring _u16;

        public:
            static std::string make_u8(const std::wstring& u16) noexcept {
                if (u16.empty()) return {};

                auto required = WideCharToMultiByte(CP_UTF8, 0, u16.c_str(), -1, nullptr, 0, nullptr, nullptr);
                auto result = std::string(required - 1, '\0');

                WideCharToMultiByte(CP_UTF8, 0, u16.c_str(), -1, result.data(), required, nullptr, nullptr);

                return result;
            }

            static __forceinline std::wstring make_u16(const std::string& u8) noexcept {
                if (u8.empty()) return { };

                auto required = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
                auto result = std::wstring(required - 1, L'\0');

                MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, result.data(), required);

                return result;
            }

        public:
            __forceinline compatible_string() = default;

            __forceinline compatible_string(const void* data, const size_t size, bool wide) {
                if (wide) {
                    _u16 = std::wstring((const wchar_t*)data, size / 2);
                    _u8 = make_u8(_u16);
                }
                else {
                    _u8 = std::string((const char*)data, size);
                    _u16 = make_u16(_u8);
                }
            }

            __forceinline compatible_string(const std::string& u8) noexcept {
                _u8 = u8;
                _u16 = make_u16(u8);
            }

            __forceinline compatible_string(const char* u8) noexcept {
                _u8 = u8;
                _u16 = make_u16(u8);
            }

            __forceinline compatible_string(const std::wstring& u16) noexcept {
                _u8 = make_u8(u16);
                _u16 = u16;
            }

            __forceinline compatible_string(const wchar_t* u16) noexcept {
                _u8 = make_u8(u16);
                _u16 = u16;
            }

            __forceinline compatible_string(const std::string& u8, const std::wstring& u16) noexcept {
                _u8 = u8;
                _u16 = u16;
            }

            __forceinline constexpr auto& string() const noexcept {
                return _u8;
            }

            __forceinline constexpr auto& wstring() const noexcept {
                return _u16;
            }
        };
    
        static __forceinline constexpr std::string string_to_lower(const std::string& data) noexcept {
            auto result = std::string(data);

            for (auto& letter : result) {
                letter = std::tolower(letter);
            }

            return result;
        }
	}

    using namespace strings;
}
