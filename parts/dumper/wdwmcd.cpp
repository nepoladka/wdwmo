#include "wdwmcd.hpp"

#include "../../../ncore/source/utils.hpp"
#include "../../../ncore/source/disassembly.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <raw_pdb/PDB.h>
#include <raw_pdb/PDB_RawFile.h>
#include <raw_pdb/PDB_DBIStream.h>
#include <raw_pdb/PDB_InfoStream.h>
#include <raw_pdb/PDB_ImageSectionStream.h>
#include <raw_pdb/PDB_PublicSymbolStream.h>
#include <raw_pdb/PDB_GlobalSymbolStream.h>
#include <raw_pdb/PDB_ModuleInfoStream.h>
#include <raw_pdb/PDB_ModuleSymbolStream.h>
#include <raw_pdb/PDB_DBITypes.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#ifdef DEBUG
#define conlog debug::conlogf
#else
#define conlog(...)
#endif

#pragma comment(lib, "beaengine.lib")

namespace wdwmcd {
    using namespace ncore::types;

    namespace debug {
        static HANDLE g_console_out = nullptr;

        void set_console_output(void* handle) noexcept { g_console_out = (HANDLE)handle; }
        void* get_console_output() noexcept { return g_console_out; }

        int conlogf(const char* fmt, ...) noexcept {
            if (!g_console_out || g_console_out == INVALID_HANDLE_VALUE) return 0;

            char buffer[4096] = {};
            va_list args;
            va_start(args, fmt);
            int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
            va_end(args);

            if (len <= 0) return 0;
            if (len >= int(sizeof(buffer))) len = int(sizeof(buffer)) - 1;

            DWORD written = 0;
            WriteConsoleA(g_console_out, buffer, DWORD(len), &written, nullptr);
            return int(written);
        }
    }

    namespace utils {
        std::string_view strip_module_prefix(std::string_view name) noexcept {
            auto bang = name.find('!');
            return bang == std::string_view::npos ? name : name.substr(bang + 1);
        }

        std::string format_guid_age(const GUID& guid, ui32_t age) {
            char buffer[128] = {};
            snprintf(
                buffer,
                sizeof(buffer),
                "%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X",
                guid.Data1,
                guid.Data2,
                guid.Data3,
                guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7],
                age);
            return buffer;
        }

        std::string basename_of(const std::string& path) {
            auto pos = path.find_last_of("\\/");
            return pos == std::string::npos ? path : path.substr(pos + 1);
        }

        ui32_t rva_to_file_offset(const void* image, size_t size, ui32_t rva) noexcept {
            if (size < sizeof(IMAGE_DOS_HEADER)) return 0;
            auto dos = (const IMAGE_DOS_HEADER*)image;
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
            if (dos->e_lfanew <= 0 || size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > size) return 0;

            auto nt = (const IMAGE_NT_HEADERS64*)(ui64_t(image) + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

            auto section = IMAGE_FIRST_SECTION(nt);
            for (ui32_t i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
                auto va = section->VirtualAddress;
                auto vs = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
                if (rva >= va && rva < va + vs) {
                    return section->PointerToRawData + (rva - va);
                }
            }
            return rva;
        }
    }

    namespace details {
        using namespace utils;

        struct cv_info_pdb70_t {
            DWORD signature;
            GUID guid;
            DWORD age;
            char pdb_file_name[1];
        };

        struct pe_info_t : executable_image_info_t {
            ui64_t image_base = 0;
            GUID pdb_guid = {};
            ui32_t pdb_age = 0;
            std::string pdb_name;
        };

#pragma pack(push, 1)
        // CodeView S_PROCREF/S_LPROCREF layout. raw_pdb exposes the enum,
        // but not the union fields, while Microsoft's PDBs often keep
        // procedures in the global stream as references into a module stream.
        struct proc_ref_record_t {
            ui32_t name_checksum;
            ui32_t symbol_offset;
            uint16_t module_index;
            char name[1];
        };
#pragma pack(pop)

        struct symbol_match_key_t {
            std::string exact_undecorated;
            std::string decorated_prefix;
            bool expected_is_decorated = false;
        };

        struct symbol_resolution_t {
            ui32_t rva = 0;
            bool ambiguous = false;
            ui32_t matches = 0;
        };


        status_t parse_pe_info(const void* image, size_t size, pe_info_t& out) noexcept {
            if (size < sizeof(IMAGE_DOS_HEADER)) return status_t::wrong_pe_header;

            auto dos = (const IMAGE_DOS_HEADER*)image;
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return status_t::wrong_pe_header;
            if (dos->e_lfanew <= 0 || size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > size) return status_t::wrong_pe_header;

            auto nt32 = (const IMAGE_NT_HEADERS32*)(ui64_t(image) + dos->e_lfanew);
            if (nt32->Signature != IMAGE_NT_SIGNATURE) return status_t::wrong_pe_header;

            out = {};
            out.timestamp = nt32->FileHeader.TimeDateStamp;

            IMAGE_DATA_DIRECTORY debug_dir = {};
            if (nt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                auto nt = (const IMAGE_NT_HEADERS64*)nt32;
                out.size = nt->OptionalHeader.SizeOfImage;
                out.checksum = nt->OptionalHeader.CheckSum;
                out.image_base = ui64_t(nt->OptionalHeader.ImageBase);
                debug_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
            }
            else if (nt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                out.size = nt32->OptionalHeader.SizeOfImage;
                out.checksum = nt32->OptionalHeader.CheckSum;
                out.image_base = ui64_t(nt32->OptionalHeader.ImageBase);
                debug_dir = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
            }
            else return status_t::wrong_pe_header;

            if (!debug_dir.VirtualAddress || !debug_dir.Size) return status_t::pdb_info_not_found;

            auto debug_offset = rva_to_file_offset(image, size, debug_dir.VirtualAddress);
            if (!debug_offset || debug_offset + debug_dir.Size > size) return status_t::pdb_info_not_found;

            auto debug = (const IMAGE_DEBUG_DIRECTORY*)(ui64_t(image) + debug_offset);
            auto count = debug_dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
            for (ui32_t i = 0; i < count; ++i) {
                if (debug[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW) continue;

                auto cv_offset = debug[i].PointerToRawData;
                if (!cv_offset || cv_offset + sizeof(cv_info_pdb70_t) > size) continue;

                auto cv = (const cv_info_pdb70_t*)(ui64_t(image) + cv_offset);
                if (cv->signature != 0x53445352) continue; // RSDS little-endian

                out.pdb_guid = cv->guid;
                out.pdb_age = cv->age;
                out.pdb_name = cv->pdb_file_name;
                return status_t::success;
            }

            return status_t::pdb_info_not_found;
        }

        std::string make_msvc_decorated_needle(std::string_view qualified) {
            // "COverlayContext::Present" -> "?Present@COverlayContext@@"
            // "A::B::Func"              -> "?Func@B@A@@"
            // "ScheduleCompositionPass" -> "?ScheduleCompositionPass@@"
            std::vector<std::string_view> parts;
            size_t start = 0;
            for (;;) {
                auto pos = qualified.find("::", start);
                if (pos == std::string_view::npos) {
                    parts.emplace_back(qualified.substr(start));
                    break;
                }
                parts.emplace_back(qualified.substr(start, pos - start));
                start = pos + 2;
            }

            if (parts.empty()) return {};

            std::string out;
            out.reserve(qualified.size() + 4);
            out += '?';
            out.append(parts.back().data(), parts.back().size());
            out += '@';

            if (parts.size() == 1) {
                out += '@';
                return out;
            }

            for (size_t i = parts.size() - 1; i-- > 0;) {
                out.append(parts[i].data(), parts[i].size());
                out += '@';
            }
            out += '@';
            return out;
        }

        symbol_match_key_t make_symbol_match_key(const char* expected) {
            symbol_match_key_t key = {};
            auto stripped = strip_module_prefix(expected);
            if (stripped.empty()) return key;

            if (stripped.front() == '?') {
                key.decorated_prefix.assign(stripped.data(), stripped.size());
                key.expected_is_decorated = true;
                return key;
            }

            key.exact_undecorated.assign(stripped.data(), stripped.size());
            key.decorated_prefix = make_msvc_decorated_needle(stripped);
            return key;
        }

        bool symbol_name_matches(std::string_view name, const symbol_match_key_t& key, bool allow_adjustor_thunks = false) noexcept {
            name = strip_module_prefix(name);
            if (name.empty()) return false;

            if (key.expected_is_decorated) {
                return name == key.decorated_prefix ||
                    (allow_adjustor_thunks && !key.decorated_prefix.empty() && name.starts_with(key.decorated_prefix));
            }

            if (!key.exact_undecorated.empty() && name == key.exact_undecorated) {
                return true;
            }

            if (key.decorated_prefix.empty() || !name.starts_with(key.decorated_prefix)) {
                return false;
            }

            // MSVC vtordisp/adjustor symbols insert "$..." immediately after the
            // class scope, for example:
            // ?PresentMPO@CLegacySwapChain@@$4PPPPPPPM@A@EAA...
            // These are thunks, not the function body. For plain function RVA
            // lookups we reject them; for vftable slot lookup they are valid
            // because vftables often point exactly to those adjustor thunks.
            if (!allow_adjustor_thunks && name.size() > key.decorated_prefix.size() && name[key.decorated_prefix.size()] == '$') {
                return false;
            }

            return true;
        }

        bool is_proc_record(const PDB::CodeView::DBI::Record* record) noexcept {
            if (!record) return false;
            using kind_t = PDB::CodeView::DBI::SymbolRecordKind;
            switch (record->header.kind) {
            case kind_t::S_LPROC32:
            case kind_t::S_GPROC32:
            case kind_t::S_LPROC32_ID:
            case kind_t::S_GPROC32_ID:
            case kind_t::S_LPROC32_DPC:
            case kind_t::S_LPROC32_DPC_ID:
            case kind_t::S_THUNK32:
                return true;
            default:
                return false;
            }
        }

        const char* get_symbol_record_name(const PDB::CodeView::DBI::Record* record) noexcept {
            if (!record) return nullptr;

            using kind_t = PDB::CodeView::DBI::SymbolRecordKind;
            switch (record->header.kind) {
            case kind_t::S_PUB32: return record->data.S_PUB32.name;
            case kind_t::S_GDATA32: return record->data.S_GDATA32.name;
            case kind_t::S_LDATA32: return record->data.S_LDATA32.name;
            case kind_t::S_GPROC32: return record->data.S_GPROC32.name;
            case kind_t::S_LPROC32: return record->data.S_LPROC32.name;
            case kind_t::S_GPROC32_ID: return record->data.S_GPROC32_ID.name;
            case kind_t::S_LPROC32_ID: return record->data.S_LPROC32_ID.name;
            case kind_t::S_LPROC32_DPC: return record->data.S_LPROC32_DPC.name;
            case kind_t::S_LPROC32_DPC_ID: return record->data.S_LPROC32_DPC_ID.name;
            case kind_t::S_THUNK32: return record->data.S_THUNK32.name;
            case kind_t::S_PROCREF:
            case kind_t::S_LPROCREF:
                return ((const proc_ref_record_t*)&record->data)->name;
            default:
                return nullptr;
            }
        }

        bool get_symbol_record_address(
            const PDB::ImageSectionStream& image_sections,
            const PDB::CodeView::DBI::Record* record,
            ui32_t& rva) noexcept {
            rva = 0;
            if (!record) return false;

            using kind_t = PDB::CodeView::DBI::SymbolRecordKind;

            ui32_t offset = 0;
            uint16_t section = 0;
            switch (record->header.kind) {
            case kind_t::S_PUB32:
                section = record->data.S_PUB32.section;
                offset = record->data.S_PUB32.offset;
                break;
            case kind_t::S_GDATA32:
                section = record->data.S_GDATA32.section;
                offset = record->data.S_GDATA32.offset;
                break;
            case kind_t::S_LDATA32:
                section = record->data.S_LDATA32.section;
                offset = record->data.S_LDATA32.offset;
                break;
            case kind_t::S_GPROC32:
                section = record->data.S_GPROC32.section;
                offset = record->data.S_GPROC32.offset;
                break;
            case kind_t::S_LPROC32:
                section = record->data.S_LPROC32.section;
                offset = record->data.S_LPROC32.offset;
                break;
            case kind_t::S_GPROC32_ID:
                section = record->data.S_GPROC32_ID.section;
                offset = record->data.S_GPROC32_ID.offset;
                break;
            case kind_t::S_LPROC32_ID:
                section = record->data.S_LPROC32_ID.section;
                offset = record->data.S_LPROC32_ID.offset;
                break;
            case kind_t::S_LPROC32_DPC:
                section = record->data.S_LPROC32_DPC.section;
                offset = record->data.S_LPROC32_DPC.offset;
                break;
            case kind_t::S_LPROC32_DPC_ID:
                section = record->data.S_LPROC32_DPC_ID.section;
                offset = record->data.S_LPROC32_DPC_ID.offset;
                break;
            case kind_t::S_THUNK32:
                section = record->data.S_THUNK32.section;
                offset = record->data.S_THUNK32.offset;
                break;
            default:
                return false;
            }

            rva = image_sections.ConvertSectionOffsetToRVA(section, offset);
            return rva != 0;
        }

        void add_resolved_symbol_rva(symbol_resolution_t& result, ui32_t rva) noexcept {
            if (!rva) return;
            ++result.matches;

            if (!result.rva) {
                result.rva = rva;
                return;
            }

            if (result.rva != rva) {
                result.ambiguous = true;
            }
        }

        ui32_t resolve_proc_ref_rva(
            const PDB::RawFile& raw,
            const PDB::ModuleInfoStream& modules,
            const PDB::ImageSectionStream& image_sections,
            const PDB::CodeView::DBI::Record* ref_record,
            const symbol_match_key_t& key,
            bool allow_adjustor_thunks = false) noexcept {
            using kind_t = PDB::CodeView::DBI::SymbolRecordKind;
            if (!ref_record || (ref_record->header.kind != kind_t::S_PROCREF && ref_record->header.kind != kind_t::S_LPROCREF)) return 0;

            auto ref = (const proc_ref_record_t*)(&ref_record->data);
            auto module_view = modules.GetModules();
            const ui32_t module_count = ui32_t(module_view.GetLength());
            symbol_resolution_t result = {};

            auto try_record = [&](const PDB::CodeView::DBI::Record* record) {
                if (!is_proc_record(record)) return;

                auto name = get_symbol_record_name(record);
                if (!name || !symbol_name_matches(std::string_view(name), key, allow_adjustor_thunks)) return;

                ui32_t rva = 0;
                if (get_symbol_record_address(image_sections, record, rva)) {
                    add_resolved_symbol_rva(result, rva);
                }
            };

            // CodeView imod in S_PROCREF/S_LPROCREF is one-based.
            if (ref->module_index) {
                const ui32_t module_index = ui32_t(ref->module_index - 1);
                if (module_index < module_count) {
                    const auto& module = module_view[module_index];
                    if (module.HasSymbolStream()) {
                        auto module_stream = module.CreateSymbolStream(raw);
                        try_record(module_stream.GetRecordAtOffset(ref->symbol_offset));
                    }
                }
            }

            if (result.ambiguous) return 0;
            if (result.rva) return result.rva;

            // Deterministic fallback: the reference name matched exactly but the
            // referenced offset did not resolve with raw_pdb on this PDB. Search
            // module procedure records by the same exact symbol key and require a
            // single RVA.
            for (const auto& module : module_view) {
                if (!module.HasSymbolStream()) continue;

                auto module_stream = module.CreateSymbolStream(raw);
                module_stream.ForEachSymbol([&](const PDB::CodeView::DBI::Record* record) {
                    if (!result.ambiguous) try_record(record);
                });

                if (result.ambiguous) return 0;
            }

            return result.rva;
        }

        ui32_t find_symbol_rva(const PDB::RawFile& raw, const PDB::DBIStream& dbi, const char* expected, bool allow_adjustor_thunks = false) noexcept {
            auto key = make_symbol_match_key(expected);
            if (key.exact_undecorated.empty() && key.decorated_prefix.empty()) return 0;

            auto image_sections = dbi.CreateImageSectionStream(raw);
            auto symbol_record_stream = dbi.CreateSymbolRecordStream(raw);
            auto modules = dbi.CreateModuleInfoStream(raw);
            symbol_resolution_t result = {};

            auto try_record = [&](const PDB::CodeView::DBI::Record* record, const char* source) {
                if (!record || result.ambiguous) return;

                auto name = get_symbol_record_name(record);
                if (!name || !symbol_name_matches(std::string_view(name), key, allow_adjustor_thunks)) return;

                ui32_t rva = 0;
                if (!get_symbol_record_address(image_sections, record, rva)) {
                    using kind_t = PDB::CodeView::DBI::SymbolRecordKind;
                    if (record->header.kind == kind_t::S_PROCREF || record->header.kind == kind_t::S_LPROCREF) {
                        rva = resolve_proc_ref_rva(raw, modules, image_sections, record, key);
                    }
                }

                if (rva) {
                    conlog("[d] exact symbol candidate %-8s kind=%#x rva=%#x name=%s\n", source, ui32_t(record->header.kind), rva, name);
                    add_resolved_symbol_rva(result, rva);
                }
            };

            {
                auto public_stream = dbi.CreatePublicSymbolStream(raw);
                for (const auto& hash : public_stream.GetRecords()) {
                    try_record(public_stream.GetRecord(symbol_record_stream, hash), "public");
                    if (result.ambiguous) break;
                }
            }

            if (!result.ambiguous) {
                auto global_stream = dbi.CreateGlobalSymbolStream(raw);
                for (const auto& hash : global_stream.GetRecords()) {
                    try_record(global_stream.GetRecord(symbol_record_stream, hash), "global");
                    if (result.ambiguous) break;
                }
            }

            if (!result.ambiguous) {
                for (const auto& module : modules.GetModules()) {
                    if (!module.HasSymbolStream()) continue;

                    auto module_stream = module.CreateSymbolStream(raw);
                    module_stream.ForEachSymbol([&](const PDB::CodeView::DBI::Record* record) {
                        try_record(record, "module");
                    });

                    if (result.ambiguous) break;
                }
            }

            if (result.ambiguous) {
                conlog(
                    "[d] symbol %-32s -> 0 [ambiguous exact lookup; undecorated='%s', decorated-prefix='%s']\n",
                    expected ? expected : "<null>",
                    key.exact_undecorated.c_str(),
                    key.decorated_prefix.c_str());
                return 0;
            }

            if (!result.rva) {
                conlog(
                    "[d] symbol %-32s -> 0 [not found by exact lookup; undecorated='%s', decorated-prefix='%s']\n",
                    expected ? expected : "<null>",
                    key.exact_undecorated.c_str(),
                    key.decorated_prefix.c_str());
                return 0;
            }

            return result.rva;
        }

        void add_unique_rva(std::vector<ui32_t>& rvas, ui32_t rva) noexcept {
            if (!rva) return;
            if (std::find(rvas.begin(), rvas.end(), rva) != rvas.end()) return;
            rvas.emplace_back(rva);
        }

        size_t collect_symbol_rvas(
            const PDB::RawFile& raw,
            const PDB::DBIStream& dbi,
            const char* expected,
            bool allow_adjustor_thunks,
            std::vector<ui32_t>& rvas) noexcept {
            auto key = make_symbol_match_key(expected);
            if (key.exact_undecorated.empty() && key.decorated_prefix.empty()) return 0;

            auto count = size_t(0);

            auto image_sections = dbi.CreateImageSectionStream(raw);
            auto symbol_record_stream = dbi.CreateSymbolRecordStream(raw);
            auto modules = dbi.CreateModuleInfoStream(raw);

            auto try_record = [&](const PDB::CodeView::DBI::Record* record) {
                if (!record) return false;

                auto name = get_symbol_record_name(record);
                if (!name || !symbol_name_matches(std::string_view(name), key, allow_adjustor_thunks)) return false;

                ui32_t rva = 0;
                if (!get_symbol_record_address(image_sections, record, rva)) {
                    using kind_t = PDB::CodeView::DBI::SymbolRecordKind;
                    if (record->header.kind == kind_t::S_PROCREF || record->header.kind == kind_t::S_LPROCREF) {
                        rva = resolve_proc_ref_rva(raw, modules, image_sections, record, key, allow_adjustor_thunks);
                    }
                }

                add_unique_rva(rvas, rva);

                return true;
            };

            {
                auto public_stream = dbi.CreatePublicSymbolStream(raw);
                for (const auto& hash : public_stream.GetRecords()) {
                    count += try_record(public_stream.GetRecord(symbol_record_stream, hash));
                }
            }

            {
                auto global_stream = dbi.CreateGlobalSymbolStream(raw);
                for (const auto& hash : global_stream.GetRecords()) {
                    count += try_record(global_stream.GetRecord(symbol_record_stream, hash));
                }
            }

            for (const auto& module : modules.GetModules()) {
                if (!module.HasSymbolStream()) continue;

                auto module_stream = module.CreateSymbolStream(raw);
                module_stream.ForEachSymbol([&](const PDB::CodeView::DBI::Record* record) {
                    count+=try_record(record);
                });
            }

            return count;
        }

        bool symbol_name_starts_with(std::string_view name, std::string_view prefix) noexcept {
            name = strip_module_prefix(name);
            return !name.empty() && !prefix.empty() && name.starts_with(prefix);
        }

        void collect_vftable_rvas_by_prefix(
            const PDB::RawFile& raw,
            const PDB::DBIStream& dbi,
            const char* decorated_vftable_prefix,
            std::vector<ui32_t>& rvas) noexcept {
            if (!decorated_vftable_prefix || !*decorated_vftable_prefix) return;

            auto prefix = std::string_view(decorated_vftable_prefix);
            auto image_sections = dbi.CreateImageSectionStream(raw);
            auto symbol_record_stream = dbi.CreateSymbolRecordStream(raw);
            auto modules = dbi.CreateModuleInfoStream(raw);

            auto try_record = [&](const PDB::CodeView::DBI::Record* record) {
                if (!record) return;

                auto name = get_symbol_record_name(record);
                if (!name || !symbol_name_starts_with(std::string_view(name), prefix)) return;

                ui32_t rva = 0;
                if (get_symbol_record_address(image_sections, record, rva)) {
                    add_unique_rva(rvas, rva);
                }
            };

            {
                auto public_stream = dbi.CreatePublicSymbolStream(raw);
                for (const auto& hash : public_stream.GetRecords()) {
                    try_record(public_stream.GetRecord(symbol_record_stream, hash));
                }
            }

            {
                auto global_stream = dbi.CreateGlobalSymbolStream(raw);
                for (const auto& hash : global_stream.GetRecords()) {
                    try_record(global_stream.GetRecord(symbol_record_stream, hash));
                }
            }

            for (const auto& module : modules.GetModules()) {
                if (!module.HasSymbolStream()) continue;

                auto module_stream = module.CreateSymbolStream(raw);
                module_stream.ForEachSymbol([&](const PDB::CodeView::DBI::Record* record) {
                    try_record(record);
                });
            }
        }

        bool vftable_entry_matches_rva(ui64_t entry_va, const pe_info_t& pe, const std::vector<ui32_t>& method_rvas) noexcept {
            if (!entry_va || entry_va < pe.image_base) return false;

            const auto entry_rva64 = entry_va - pe.image_base;
            if (entry_rva64 > 0xffffffffui64) return false;

            const auto entry_rva = ui32_t(entry_rva64);
            return std::find(method_rvas.begin(), method_rvas.end(), entry_rva) != method_rvas.end();
        }

        ui32_t get_next_vftable_rva(ui32_t current_rva, const std::vector<ui32_t>& all_vftable_rvas) noexcept {
            ui32_t next_rva = 0;

            for (auto rva : all_vftable_rvas) {
                if (rva <= current_rva) continue;
                if (!next_rva || rva < next_rva) next_rva = rva;
            }

            return next_rva;
        }

        bool find_method_offset_in_vftable(
            const void* image,
            size_t image_size,
            const pe_info_t& pe,
            ui32_t vftable_rva,
            const std::vector<ui32_t>& method_rvas,
            const std::vector<ui32_t>& all_vftable_rvas,
            ui32_t& out_offset) noexcept {
            out_offset = 0;
            if (!image || !image_size || !vftable_rva || method_rvas.empty()) return false;

            constexpr ui32_t max_vftable_scan = 0x400;

            auto scan_size = max_vftable_scan;
            if (const auto next_vftable_rva = get_next_vftable_rva(vftable_rva, all_vftable_rvas)) {
                const auto distance = next_vftable_rva - vftable_rva;
                if (distance < scan_size) scan_size = distance;
            }

            // Important: PDB emits separate vftable symbols for adjacent base/interface
            // subobjects. Some of them are only one pointer long. Do not scan past the
            // next vftable symbol; otherwise a one-slot table can falsely "find" a
            // method from the following table and produce fake conflicts.
            scan_size &= ~(ui32_t(sizeof(ui64_t)) - 1);
            if (!scan_size) return false;

            for (ui32_t offset = 0; offset < scan_size; offset += sizeof(ui64_t)) {
                auto file_offset = rva_to_file_offset(image, image_size, vftable_rva + offset);
                if (!file_offset || file_offset + sizeof(ui64_t) > image_size) break;

                const auto entry_va = *reinterpret_cast<const ui64_t*>(ui64_t(image) + file_offset);
                if (vftable_entry_matches_rva(entry_va, pe, method_rvas)) {
                    out_offset = offset;
                    return true;
                }
            }

            return false;
        }

        ui32_t find_vftable_method_offset(
            const PDB::RawFile& raw,
            const PDB::DBIStream& dbi,
            const void* image,
            size_t image_size,
            const pe_info_t& pe,
            const char* logical_name,
            const char* const* vftable_prefixes,
            size_t vftable_prefix_count,
            const char* const* method_names,
            size_t method_name_count,
            std::map<const char*, size_t>* _found_methods = nullptr) noexcept {
            std::vector<ui32_t> vftable_rvas;
            std::vector<ui32_t> all_vftable_rvas;
            std::vector<ui32_t> method_rvas;

            collect_vftable_rvas_by_prefix(raw, dbi, "??_7", all_vftable_rvas);
            std::sort(all_vftable_rvas.begin(), all_vftable_rvas.end());

            for (size_t i = 0; i < vftable_prefix_count; ++i) {
                collect_vftable_rvas_by_prefix(raw, dbi, vftable_prefixes[i], vftable_rvas);
            }

            std::sort(vftable_rvas.begin(), vftable_rvas.end());

            for (size_t i = 0; i < method_name_count; ++i) {
                auto found_count = collect_symbol_rvas(raw, dbi, method_names[i], true, method_rvas);
                if (!found_count || !_found_methods) continue;

                (*_found_methods)[method_names[i]] = found_count;
            }

            conlog(
                "[d] vftable offset %-32s: vftables=%llu, methods=%llu\n",
                logical_name ? logical_name : "<null>",
                ui64_t(vftable_rvas.size()),
                ui64_t(method_rvas.size()));

            ui32_t result_offset = 0;
            ui32_t match_count = 0;
            bool has_result = false;
            bool conflict = false;

            for (auto vftable_rva : vftable_rvas) {
                ui32_t offset = 0;
                if (!find_method_offset_in_vftable(image, image_size, pe, vftable_rva, method_rvas, all_vftable_rvas, offset)) continue;

                ++match_count;
                if (!has_result) {
                    has_result = true;
                    result_offset = offset;
                }
                else if (result_offset != offset) {
                    conflict = true;
                    conlog(
                        "[d] vftable offset %-32s: conflict vftable=%#x offset=%#x first=%#x\n",
                        logical_name ? logical_name : "<null>",
                        vftable_rva,
                        offset,
                        result_offset);
                }

                conlog(
                    "[d] vftable offset %-32s: vftable=%#x offset=%#x\n",
                    logical_name ? logical_name : "<null>",
                    vftable_rva,
                    offset);
            }

            if (!match_count) {
                conlog("[d] vftable offset %-32s -> 0 [not found]\n", logical_name ? logical_name : "<null>");
                return 0;
            }

            if (conflict) {
                conlog("[d] vftable offset %-32s -> %#x [using first matched offset]\n", logical_name ? logical_name : "<null>", result_offset);
            }
            else {
                conlog("[d] vftable offset %-32s -> %#x\n", logical_name ? logical_name : "<null>", result_offset);
            }

            return result_offset;
        }

        bool get_function_bounds_from_pdata(const void* image, size_t image_size, ui32_t function_rva, ui32_t& out_begin_rva, ui32_t& out_end_rva) noexcept {
            out_begin_rva = 0;
            out_end_rva = 0;

            if (!image || image_size < sizeof(IMAGE_DOS_HEADER) || !function_rva) return false;

            auto dos = (const IMAGE_DOS_HEADER*)image;
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)return false;

            if (dos->e_lfanew <= 0 || size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image_size)return false;

            auto nt = (const IMAGE_NT_HEADERS64*)(ui64_t(image) + dos->e_lfanew);

            if (nt->Signature != IMAGE_NT_SIGNATURE)return false;

            if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;

            const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];

            if (!dir.VirtualAddress || !dir.Size)return false;

            auto pdata_offset = rva_to_file_offset(image, image_size, dir.VirtualAddress);

            if (!pdata_offset || pdata_offset + dir.Size > image_size)return false;

            auto entries = (const IMAGE_RUNTIME_FUNCTION_ENTRY*)(ui64_t(image) + pdata_offset);

            const auto count = dir.Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY);

            // exact match first.
            for (size_t i = 0; i < count; ++i) {
                const auto begin = entries[i].BeginAddress;
                const auto end = entries[i].EndAddress;

                if (begin == function_rva && end > begin) {
                    out_begin_rva = begin;
                    out_end_rva = end;

                    return true;
                }
            }

            // fallback: symbol can point inside a thunk/chunk.
            for (size_t i = 0; i < count; ++i) {
                const auto begin = entries[i].BeginAddress;
                const auto end = entries[i].EndAddress;

                if (begin <= function_rva && function_rva < end && end > begin) {
                    out_begin_rva = begin;
                    out_end_rva = end;

                    return true;
                }
            }

            return false;
        }

        bool get_function_size_from_pdata(const void* image,size_t image_size,ui32_t function_rva,ui32_t& out_size) noexcept {
            out_size = 0;

            ui32_t begin = 0;
            ui32_t end = 0;

            if (!get_function_bounds_from_pdata(image, image_size, function_rva, begin, end))return false;

            out_size = end - begin;
            return out_size != 0;
        }

        status_t analyze_pdb(
            const target_t& dwmcore_target,
            const target_t& dxgi_target,
            const pe_info_t& dwmcore_pe, 
            configuration_t& _result) noexcept {
            if (PDB::ValidateFile(dwmcore_target.pdb.data, dwmcore_target.pdb.size) != PDB::ErrorCode::Success) return status_t::pdb_parsing_failed;
            if (PDB::ValidateFile(dxgi_target.pdb.data, dxgi_target.pdb.size) != PDB::ErrorCode::Success) return status_t::pdb_parsing_failed;


            auto raw = PDB::CreateRawFile(dwmcore_target.pdb.data);
            if (PDB::HasValidDBIStream(raw) != PDB::ErrorCode::Success) return status_t::unsupported_pdb;
            auto dbi = PDB::CreateDBIStream(raw);
            if (dbi.HasValidImageSectionStream(raw) != PDB::ErrorCode::Success) return status_t::unsupported_pdb;
            if (dbi.HasValidSymbolRecordStream(raw) != PDB::ErrorCode::Success) return status_t::unsupported_pdb;

            auto raw_dxgi = PDB::CreateRawFile(dxgi_target.pdb.data);
            if (PDB::HasValidDBIStream(raw_dxgi) != PDB::ErrorCode::Success) return status_t::unsupported_pdb;
            auto dbi_dxgi = PDB::CreateDBIStream(raw_dxgi);
            if (dbi_dxgi.HasValidImageSectionStream(raw_dxgi) != PDB::ErrorCode::Success) return status_t::unsupported_pdb;
            if (dbi_dxgi.HasValidSymbolRecordStream(raw_dxgi) != PDB::ErrorCode::Success) return status_t::unsupported_pdb;


            struct symbol_request_t { 
                const char* name;
                ui32_t* out;
                bool required; 
                bool allow_adjustor_thunks;
            };

            auto clegacy_swap_chain_present_mpo = ui32_t();
            auto cd3ddevice_present_mpo = ui32_t();
            auto coverlay_context_constructor = ui32_t();

            symbol_request_t requests[] = {
                { "COverlayContext::Present", ui32_p(&_result.offsets.present), true, false}, //also, we can hook coverlaycontext::islegacyrequired, because this is first call and first two parameters registers are equals
                { "CLegacySwapChain::PresentMPO", &clegacy_swap_chain_present_mpo, true, false },
                { "CD3DDevice::PresentMPO", &cd3ddevice_present_mpo, true, false },
                { "??0COverlayContext@@", &coverlay_context_constructor, true, true }
            };

            bool required_missing = false;
            for (auto& item : requests) {
                if (*item.out = find_symbol_rva(raw, dbi, item.name, item.allow_adjustor_thunks)) {
                    conlog("[d] symbol %-32s -> %#x\n", item.name, *item.out);
                }
                else if (item.required) {
                    conlog("[d] symbol %-32s -> 0 [required/not found]\n", item.name);
                    required_missing = true;
                }
                else {
                    conlog("[d] symbol %-32s -> 0 [optional/not exported]\n", item.name);
                }
            }

            if (required_missing) return status_t::symbol_not_found;

            {
                const char* target_vftable_name = "??_7?$CComObject@VCDXGIOutput@@@ATL@@6BIDXGIOutputDWM@@@";

                std::vector<ui32_t> vftable_rvas{};
                collect_vftable_rvas_by_prefix(raw_dxgi, dbi_dxgi, target_vftable_name, vftable_rvas);

                conlog("[d] found %d results for %s\n", vftable_rvas.size(), target_vftable_name);

                if (vftable_rvas.empty()) {
                    required_missing = true;
                }
                else {
                    _result.offsets.dxgi_output_vftable = vftable_rvas.front();
                }
            }

            {
                const char* vftables[] = {
                    "??_7CLegacySwapChain@@",
                    "??_7CDDisplaySwapChain@@"
                };

                const char* methods[] = {
                    "CLegacySwapChain::GetPhysicalBackBuffer",
                    "CDDisplaySwapChain::GetPhysicalBackBuffer"
                };

                _result.offsets.get_physical_back_buffer = find_vftable_method_offset(
                    raw,
                    dbi,
                    dwmcore_target.image.data,
                    dwmcore_target.image.size,
                    dwmcore_pe,
                    "get_physical_back_buffer",
                    vftables,
                    _countof(vftables),
                    methods,
                    _countof(methods));
            }

            {
                const char* vftables[] = {
                    "??_7CLegacySwapChainBuffer@@",
                    "??_7CDDisplaySwapChainBuffer@@"
                };

                const char* methods[] = {
                    "CLegacySwapChainBuffer::GetD3D11Resource",
                    "CDDisplaySwapChainBuffer::GetD3D11Resource"
                };

                _result.offsets.get_d3d11_resource = find_vftable_method_offset(
                    raw,
                    dbi,
                    dwmcore_target.image.data,
                    dwmcore_target.image.size,
                    dwmcore_pe,
                    "get_d3d11_resource",
                    vftables,
                    _countof(vftables),
                    methods,
                    _countof(methods));
            }

            {
                auto coverlay_context_constructor_offset = utils::rva_to_file_offset(
                    dwmcore_target.image.data,
                    dwmcore_target.image.size,
                    coverlay_context_constructor);

                auto function_size = ui32_t();

                if (get_function_size_from_pdata(
                    dwmcore_target.image.data,
                    dwmcore_target.image.size,
                    coverlay_context_constructor,
                    function_size)) {
                    conlog("[d] COverlayContext::COverlayContext size is %#lx\n", function_size);
                }
                else {
                    conlog("[d] failed to get COverlayContext::COverlayContext size from .pdata, falling back to 0x200\n");

                    function_size = 0x200;
                }
                
                auto function_code = ncore::disassembly::code(
                    byte_p(dwmcore_target.image.data) + coverlay_context_constructor_offset,
                    function_size);

                auto crender_target_offset = ui64_t();

                for (auto i = 0ui64, c = function_code.size(); i < c; i++) {
                    const auto& instruction = function_code[i];
                    if (*ui32_p(instruction.info.Mnemonic) != 'vom') continue;

                    if (*ui32_p(instruction.operands[0].OpMnemonic) != 'xcr' ||
                        *ui32_p(instruction.operands[1].OpMnemonic) != 'xdr') continue;

                    conlog("[d] disassembled render target storing instruction: \"%s\"\n", instruction.complete_instruction);

                    crender_target_offset = instruction.operands[0].Memory.Displacement;

                    break;
                }

                conlog("[d] disassembled render target offset: %#x (%d)\n", ui32_t(crender_target_offset), crender_target_offset);

                _result.offsets.render_target = crender_target_offset; //null is default offset, e.g. fallback, no need to check
            }

            {
                auto clegacy_swap_chain_present_mpo_offset = utils::rva_to_file_offset(
                    dwmcore_target.image.data,
                    dwmcore_target.image.size,
                    clegacy_swap_chain_present_mpo);

                auto cd3ddevice_present_mpo_offset = utils::rva_to_file_offset(
                    dwmcore_target.image.data,
                    dwmcore_target.image.size,
                    cd3ddevice_present_mpo);

                auto expected_delta = ui64_t(cd3ddevice_present_mpo_offset) - ui64_t(clegacy_swap_chain_present_mpo_offset);

                auto function_size = ui32_t();

                if (get_function_size_from_pdata(
                    dwmcore_target.image.data,
                    dwmcore_target.image.size,
                    clegacy_swap_chain_present_mpo,
                    function_size)) {
                    conlog("[d] CLegacySwapChain::PresentMPO size is %#lx\n", function_size);
                }
                else {
                    conlog("[d] failed to get CLegacySwapChain::PresentMPO size from .pdata, falling back to 0x400\n");

                    function_size = 0x400;
                }

                auto function_code = ncore::disassembly::code(
                    byte_p(dwmcore_target.image.data) + clegacy_swap_chain_present_mpo_offset,
                    function_size);

                auto dxgi_swapchain_offset = ui64_t();

                for (auto i = 0ui64, c = function_code.size(); i < c; i++) {
                    const auto& instruction = function_code[i];
                    if (*ui32_p(instruction.info.Mnemonic) != 'llac') continue;

                    if (instruction.info.AddrValue != expected_delta) continue;

                    for (auto j = i, s = 0ui64; j > s; j--) {
                        const auto& second = function_code[j];
                        if (*ui32_p(second.info.Mnemonic) != 'vom') continue;

                        if (*ui32_p(second.operands[0].OpMnemonic) != 'xdr') continue;

                        dxgi_swapchain_offset = second.operands[1].Memory.Displacement;

                        conlog("[d] disassembled dxgi swap chain storing instruction: \"%s\"\n", second.complete_instruction);

                        goto _DXGISCDisassemblyEnd;
                    }
                }
            _DXGISCDisassemblyEnd:

                conlog("[d] disassembled dxgi swap chain offset: %#x (%d)\n", ui32_t(dxgi_swapchain_offset), dxgi_swapchain_offset);

                if (!(_result.offsets.dxgi_swap_chain = dxgi_swapchain_offset)) {
                    required_missing = true;
                }
            }

            return required_missing ?
                status_t::symbol_not_found : 
                status_t::success;
        }
    }

    status_t get_target_pdb_info(
        const void* dll_file_data,
        size_t dll_file_size,
        std::string& _guid_age,
        std::string& _pdb_name) noexcept {
        auto info = details::pe_info_t();
        if (auto status = details::parse_pe_info(dll_file_data, dll_file_size, info)) return status;

        if ((_pdb_name = details::basename_of(info.pdb_name)).empty()) return status_t::pdb_info_not_found;

        _guid_age = details::format_guid_age(info.pdb_guid, info.pdb_age);

        return status_t::success;
    }

    status_t get_microsoft_server_pdb_url(
        const void* dll_file_data,
        size_t dll_file_size,
        std::string& _result) noexcept {
        auto guid_age= std::string();
        auto pdb_name = std::string();

        if (auto status = get_target_pdb_info(dll_file_data, dll_file_size, guid_age, pdb_name)) return status;

        _result = 
            "https://msdl.microsoft.com/download/symbols/" +
            pdb_name + "/" + 
            guid_age + "/" + 
            pdb_name;

        return status_t::success;
    }

    status_t detect_configuration(
        const target_t& dwmcore_target,
        const target_t& dxgi_target,
        configuration_t& _result) noexcept {
        if (!dwmcore_target || !dxgi_target) return status_t::invalid_argument_passed;

        auto dwmcore_info = details::pe_info_t();
        if (auto status = details::parse_pe_info(dwmcore_target.image.data, dwmcore_target.image.size, dwmcore_info)) return status;

        auto dxgi_info = details::pe_info_t();
        if (auto status = details::parse_pe_info(dxgi_target.image.data, dxgi_target.image.size, dxgi_info)) return status;

        _result.image_info = {
            .dwmcore = {
                .timestamp = dwmcore_info.timestamp,
                .size = dwmcore_info.size,
                .checksum = dwmcore_info.checksum
            },

            .dxgi = {
                .timestamp = dxgi_info.timestamp,
                .size = dxgi_info.size,
                .checksum = dxgi_info.checksum
            }
        };

        if (auto status = details::analyze_pdb(
            dwmcore_target,
            dxgi_target,
            dwmcore_info, 
            _result)) return status;

        return status_t::success;
    }
}
