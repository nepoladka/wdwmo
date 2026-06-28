#pragma once
#include "types.hpp"
#include "configuration.hpp"

#include <string>

//windows dwmcore dumper
namespace wdwmcd {
    using namespace wdwmo;
    using namespace wdwmo::types;

    struct status_t {
        enum enum_t : byte_t {
            none = 0,
            success = 0,
            error = 1,
            aborted,
            invalid_argument_passed,
            wrong_pe_header,
            pdb_info_not_found,
            pdb_parsing_failed,
            symbol_not_found,
            unsupported_pdb,

            invalid_process,
            module_not_found,
            target_not_found,

            _total_count
        } value;

        struct extended_t {
            enum_t status;
            ui64_t additional;
        };

        __forceinline status_t(byte_t value = {}) noexcept : value(enum_t(value)) { return; }
        __forceinline status_t(enum_t value) noexcept : value(value) { return; }
        __forceinline operator byte_t() const noexcept { return value; }

        __forceinline const char* c_str() const noexcept {
            static constexpr const char* const __values[] = {
                "success",
                "error",
                "aborted",
                "invalid_argument_passed",
                "wrong_pe_header",
                "pdb_info_not_found",
                "pdb_parsing_failed",
                "symbol_not_found",
                "unsupported_pdb",

                "invalid_process",
                "module_not_found",
                "target_not_found"
            };

            return (value < success || value >= _total_count) ?
                "unknown" :
                __values[value];
        }
    };

    struct target_t {
        struct {
            const void* data = { };
            size_t size = { };
        }image = { }, pdb = {};

        __forceinline constexpr operator bool() const noexcept {
            return image.data && image.size && pdb.data && pdb.size;
        }
    };

    //microsoft_symbol_host std::string(microsoft_symbol_root) + "/" + pdb_name + "/" + guid_age + "/" + pdb_name;
    //buffers allocated by malloc(...)
    status_t get_target_pdb_info(
        const void* dll_file_data,
        size_t dll_file_size,
        std::string& _guid_age,
        std::string& _pdb_name) noexcept;

    //https:// msdl.microsoft.com /download/symbols/ pdb_name / guid_age / pdb_name
    status_t get_microsoft_server_pdb_url(
        const void* dll_file_data,
        size_t dll_file_size,
        std::string& _result) noexcept;

    status_t detect_configuration(
        const target_t& dwmcore_target,
        const target_t& dxgi_target,
        configuration_t& _result) noexcept;

    namespace experimental {
        //tries to detect runtime configuration offsets in alive dwm.exe
        status_t try_detect_runtime_offsets_externally(
            const target_t& dwmcore_target,
            void* dwm_process_handle, //HANDLE
            configuration_t& configuration) noexcept;
    }

    namespace debug {
        void set_message_callback(message_callback_t callback) noexcept;
        message_callback_t get_message_callback() noexcept;

        void set_console_output(void* handle) noexcept;
        void* get_console_output() noexcept;

        int conlogf(const char* fmt, ...) noexcept;
    }
}
