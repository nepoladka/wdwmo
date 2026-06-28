#pragma once

namespace wdwmo::types {
    using i8_t = __int8;
    using ui8_t = unsigned __int8;
    using byte_t = ui8_t;
    using i32_t = __int32;
    using ui32_t = unsigned __int32;
    using ui64_t = unsigned __int64;
    using address_t = void*;
    using size_t = ui64_t;

    struct executable_image_info_t {
        ui32_t timestamp = { };
        ui32_t size = { };
        ui32_t checksum = { };

        __forceinline operator bool() const noexcept {
            return timestamp && size && checksum;
        }

        __forceinline bool operator==(const executable_image_info_t& other) const noexcept {
            return
                timestamp == other.timestamp &&
                size == other.size &&
                checksum == other.checksum;
        }
    };

    struct rect_t {
        i32_t left = 0;
        i32_t top = 0;
        i32_t right = 0;
        i32_t bottom = 0;

        __forceinline i32_t width() const noexcept { return right - left; }
        __forceinline i32_t height() const noexcept { return bottom - top; }
    };

    union luid_t {
        ui64_t ui64;
        struct {
            ui32_t low, high;
        };
    };

    using rect_p = rect_t*;
    using luid_p = luid_t*;

    using message_callback_t = size_t(*)(const char* message, size_t length); //debug
}