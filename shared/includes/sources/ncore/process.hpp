#pragma once
#include "defines.hpp"
#include "handle.hpp"
#include "environment.hpp"
#include "static_array.hpp"
#include "strings.hpp"
#include "action.hpp"

#include <tlhelp32.h>
#include <psapi.h>
#include <vector>

#pragma warning(disable : 4996)

namespace ncore {
    static const auto const __processHandleCloser = handle::native_handle_t::closer_t(NtClose);
    static const auto const __snapshotHandleCloser = handle::native_handle_t::closer_t(NtClose);
    static constexpr const auto const __defaultProcessOpenAccess = ui32_t(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION);

    class process;

    template<unsigned _bufferSize = 1024> static __forceinline std::vector<process> get_processes(unsigned open_access = __defaultProcessOpenAccess);

    class process {
    protected:
        using handle_t = handle::native_handle_t;

    public:
        class module_t {
        public:
            using flags_t = decltype(_LDR_DATA_TABLE_ENTRY_COMPATIBLE::ENTRYFLAGSUNION);

            struct export_t {
                address_t module;

                ui16_t ordinal;
                address_t address;
                static_array<char, 0xff> name;

                __forceinline constexpr offset_t offset() const noexcept {
                    return ui64_t(address) - ui64_t(module);
                }
            };

            __forceinline module_t() = default;

            __forceinline __fastcall module_t(id_t process, address_t image, address_t entry, size_t size, ui16_t tls, flags_t flags, char* path, char* name) noexcept {
                _process_id = process;
                _address = image;
                _entry_point = entry;
                _size = size;
                _tls_index = tls;
                _flags = flags;
                _path = path;
                _name = name;
            }

        protected:
            id_t _process_id;

            address_t _address, _entry_point;
            size_t _size;
            ui16_t _tls_index;
            flags_t _flags;

            static_array<char, 0xff> _path, _name;

        public:
            template<typename _t = address_t> __forceinline constexpr auto address() const noexcept {
                return _t(_address);
            }

            template<typename _t = address_t> __forceinline constexpr auto entry() const noexcept {
                return _t(_entry_point);
            }

            __forceinline constexpr auto size() const noexcept {
                return _size;
            }

            __forceinline constexpr auto tls_index() const noexcept {
                return _tls_index;
            }

            __forceinline constexpr auto path() const noexcept {
                return std::string(_path.data());
            }

            __forceinline constexpr auto name() const noexcept {
                return std::string(_name.data());
            }

            __forceinline constexpr auto flags() const noexcept {
                return _flags;
            }

            __forceinline auto process() const noexcept {
                return ncore::process(_process_id);
            }
        };

        static __forceinline handle::native_t get_handle(id_t id, ui32_t access = __defaultProcessOpenAccess) {
            auto result = handle::native_t();
            auto attributes = OBJECT_ATTRIBUTES();
            auto client_id = CLIENT_ID();

            client_id.UniqueThread = handle::native_t(null);
            client_id.UniqueProcess = handle::native_t(id);
            InitializeObjectAttributes(&attributes, null, null, null, null);

            return NT_SUCCESS(NtOpenProcess(&result, access, &attributes, &client_id)) ? result : nullptr;
        }

    protected:
        template<typename data_t = const void*> using module_enumeration_callback_t = action<bool>::procedure_t<module_t&, data_t, address_t*>;

        id_t _id;
        handle_t _handle;

        static __forceinline handle_t temp_handle(id_t id, const handle_t& source, unsigned open_access) noexcept {
            auto value = source;
            if (!value) {
                (value = handle_t(get_handle(id, open_access), __processHandleCloser, false)).close_on_destroy(true);
            }
            return value;
        }

        __forceinline bool set_suspended(bool state) const noexcept {
            auto result = false;

            auto handle = temp_handle(_id, _handle, PROCESS_SUSPEND_RESUME);
            if (!handle) {
            _Exit:
                return result;
            }

            auto procedure = state ? NtSuspendProcess : NtResumeProcess;
            result = NT_SUCCESS(procedure(handle.get()));

            goto _Exit;
        }

        template<typename data_t = const void*> __forceinline address_t enumerate_modules(module_enumeration_callback_t<data_t> callback, data_t data, module_t* _module = nullptr) const noexcept {
            auto ldr = read_memory<PEB_LDR_DATA>(get_environment().Ldr);

            auto head = ldr.InLoadOrderModuleList.Flink;
            auto current = head;
            do {
                auto entry = read_memory<LDR_DATA_TABLE_ENTRY>(current);
                current = entry.InLoadOrderLinks.Flink;

                if (!entry.DllBase) continue;

                auto buffer = static_array<wchar_t, 0xff>();

                auto path = static_array<char, 0xff>();
                auto name = static_array<char, 0xff>();

                if (entry.FullDllName.Length) {
                    read_memory(entry.FullDllName.Buffer, entry.FullDllName.MaximumLength, buffer.data());

                    auto compatible = ncore::strings::compatible_string::make_u8(buffer.data());
                    strncpy_s(path.data(), path.capacity(), compatible.c_str(), compatible.size());
                }

                if (entry.BaseDllName.Length) {
                    read_memory(entry.BaseDllName.Buffer, entry.BaseDllName.MaximumLength, buffer.data());

                    auto compatible = ncore::strings::compatible_string::make_u8(buffer.data());
                    strncpy_s(name.data(), name.capacity(), compatible.c_str(), compatible.size());
                }

                auto module = module_t(_id, entry.DllBase, entry.EntryPoint, entry.SizeOfImage, entry.TlsIndex, entry.ENTRYFLAGSUNION, path.data(), name.data());

                auto result = module.address();
                if (!callback(module, data, &result)) continue;

                if (_module) {
                    *_module = module;
                }

                return result;
            } while (head != current);

            return nullptr;
        }

    public:
        __forceinline constexpr process(id_t id = null, unsigned open_access = null) noexcept {
            if ((_id = id) && open_access) {
                _handle = handle_t(get_handle(id, open_access), __processHandleCloser, false);
            }
        }

        __forceinline process(handle::native_t win32_handle) noexcept {
            _id = win32_handle == __current_process ? __process_id : GetProcessId(win32_handle);
            _handle = handle_t(win32_handle, __processHandleCloser, false);
        }

        static __forceinline process current(ui32_t open_access = __defaultProcessOpenAccess) noexcept {
            return open_access ? process(__process_id, open_access) : process(__current_process);
        }

        static __forceinline process get_by_id(id_t id, ui32_t open_access = null) noexcept {
            return process(id, open_access);
        }

        //executable name without exe [application.exe -> application]
        static __forceinline process get_by_name(const std::string& name, ui32_t open_access = __defaultProcessOpenAccess, action<bool>::procedure_t<const std::string&, const std::string&> comparison_procedure = nullptr) noexcept {
            auto processes = get_processes(open_access);
            for (auto& process : processes) {
                auto current = process.get_name();

                if (comparison_procedure) {
                    if (comparison_procedure(current, name)) _End: return ncore::process(process.id(), open_access);
                }
                else if (current == name) goto _End;
            }
            return process();
        }

        __forceinline const auto id() const noexcept {
            return _id;
        }

        __forceinline auto handle() const noexcept {
            return _handle.get();
        }

        __forceinline auto handle(ui32_t open_access = __defaultProcessOpenAccess) noexcept {
            return _handle.get() ? _handle.get() : (_handle = handle::native_handle_t(get_handle(_id, open_access), __processHandleCloser)).get();
        }

        __forceinline auto close_handle() noexcept {
            return _handle.close();
        }

        __forceinline auto release() noexcept {
            return _handle.close();
        }

        __forceinline auto alive(ui32_t* _status = nullptr) const noexcept {
            if (!_status) {
                auto status = ui32_t();
                _status = &status;
            }

            auto handle = temp_handle(_id, _handle, PROCESS_QUERY_INFORMATION | SYNCHRONIZE);
            if (!handle) return false;

            GetExitCodeProcess(handle.get(), LPDWORD(_status));

            return *_status == STATUS_PENDING && WaitForSingleObject(handle.get(), null) != WAIT_OBJECT_0;
        }

        __forceinline auto suspend() const noexcept {
            return set_suspended(true);
        }

        __forceinline auto resume() const noexcept {
            return set_suspended(false);
        }

        __forceinline auto terminate(long exit_status = EXIT_SUCCESS) const noexcept {
            auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
            return handle.get() ? NT_SUCCESS(NtTerminateProcess(handle.get(), exit_status)) : false;
        }

        __forceinline auto set_privilege(const std::string& name, bool state) noexcept {
            auto result = false;

            auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
            if (!handle) _Exit: return result;

            auto token = handle::native_t();
            auto luid = LUID();

            if (NT_ERROR(NtOpenProcessToken(handle.get(), TOKEN_ADJUST_PRIVILEGES, &token))) goto _Exit;

            if (!LookupPrivilegeValueA(nullptr, name.c_str(), &luid)) {
            _CloseTokenExit:
                NtClose(token);

                goto _Exit;
            }

            auto privileges = TOKEN_PRIVILEGES();
            privileges.PrivilegeCount = 1;
            privileges.Privileges[0].Luid = luid;
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED * state;

            result = AdjustTokenPrivileges(token, false, &privileges, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr); 

            goto _CloseTokenExit;
        }


        template<size_t _bufferSize = MAX_PATH + FILENAME_MAX> __forceinline auto get_path() const noexcept {
            auto result = std::string();

            auto handle = temp_handle(_id, _handle, PROCESS_QUERY_INFORMATION);
            if (!handle) _Exit: return result;

            unsigned long buffer_size = _bufferSize;
            char buffer[_bufferSize] = { null };

            if (QueryFullProcessImageNameA(handle.get(), null, buffer, &buffer_size)) {
                result = buffer;
            }

            goto _Exit;
        }

        __forceinline auto get_name() const noexcept {
            auto path = get_path();

            char name[FILENAME_MAX + 1]{ 0 };
            _splitpath(path.c_str(), nullptr, nullptr, name, nullptr);

            return std::string(name);
        }

        template<typename _t = address_t> __forceinline _t get_environment_address() const noexcept {
            if (_id == id_t(__process_id)) return (_t)__process_environment;

            auto handle = temp_handle(_id, _handle, THREAD_ALL_ACCESS);
            if (!handle) _Exit: return (_t)nullptr;

            auto information = PROCESS_BASIC_INFORMATION();
            auto information_length = ULONG(sizeof(information));
            if (NT_ERROR(NtQueryInformationProcess(handle.get(), ProcessBasicInformation, &information, information_length, &information_length))) goto _Exit;

            return (_t)information.PebBaseAddress;
        }

        __forceinline auto get_environment() const noexcept {
            auto result = PEB();

            auto handle = temp_handle(_id, _handle, __defaultProcessOpenAccess);
            if (!handle) _Exit: return result;

            auto information = PROCESS_BASIC_INFORMATION();
            auto information_length = ULONG(sizeof(information));
            if (NT_ERROR(NtQueryInformationProcess(handle.get(), ProcessBasicInformation, &information, information_length, &information_length))) goto _Exit;

            NtReadVirtualMemory(handle.get(), information.PebBaseAddress, &result, sizeof(result), nullptr);

            goto _Exit;
        }

        __forceinline auto get_base() const noexcept {
            return get_environment().ImageBaseAddress;
        }

        __forceinline auto get_modules() const noexcept {
            auto result = std::vector<module_t>();

            const auto callback = [](module_t& module, decltype(result)* result, address_t* _return) {
                result->push_back(module);
                return false;
            };

            enumerate_modules<decltype(result)*>(callback, &result);
            return result;
        }

        __forceinline auto search_module(const std::string& name, module_t* _module = nullptr) const noexcept {
            const auto callback = [](module_t& module, const std::string* name, address_t* _return) noexcept {
                return strings::string_to_lower(module.name()) == *name;
                };

            auto lower_name = strings::string_to_lower(name);
            return enumerate_modules<const std::string*>(callback, &lower_name, _module);
        }

        __forceinline auto search_module(address_t address, module_t* _module = nullptr) const noexcept {
            const auto callback = [](module_t& module, address_t address, address_t* _return) noexcept {
                return module.address() == address;
                };

            return enumerate_modules<address_t>(callback, address, _module);
        }

        __forceinline auto get_address_base(address_t address, module_t* _module = nullptr) const noexcept {
            static auto callback = [](module_t& module, ui64_t address, address_t* _return) noexcept {
                return address >= module.address<ui64_t>() && address <= (module.address<ui64_t>() + module.size());
                };

            return enumerate_modules<ui64_t>(callback, ui64_t(address), _module);
        }

        __forceinline bool is_memory_available(address_t address) const noexcept {
            auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
            if (!handle) return false;

            auto region_info = MEMORY_BASIC_INFORMATION();

            return NT_SUCCESS(NtQueryVirtualMemory(handle.get(), address, MEMORY_INFORMATION_CLASS::MemoryBasicInformation, &region_info, sizeof(region_info), nullptr)) ?
                !bool(region_info.State & MEM_FREE) :
                false;
        }

        __forceinline bool get_memory_info(address_t address, address_t* _base = nullptr, size_t* _size = nullptr, ui32_t* _protection = nullptr, ui32_t* _state = nullptr, ui32_t* _type = nullptr) const noexcept {
            auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
            if (!handle) return false;

            auto region_info = MEMORY_BASIC_INFORMATION();

            if (NT_ERROR(NtQueryVirtualMemory(handle.get(), address, MEMORY_INFORMATION_CLASS::MemoryBasicInformation, &region_info, sizeof(region_info), nullptr))) return false;

            if (_base) {
                *_base = region_info.BaseAddress;
            }

            if (_size) {
                *_size = region_info.RegionSize;
            }

            if (_protection) {
                *_protection = region_info.Protect;
            }

            if (_state) {
                *_state = region_info.State;
            }

            if (_type) {
                *_type = region_info.Type;
            }

            return true;
        }

        __forceinline ui32_t get_memory_protect(address_t address) const noexcept {
            auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
            if (!handle) return null;

            auto region_info = MEMORY_BASIC_INFORMATION();

            return NT_SUCCESS(NtQueryVirtualMemory(handle.get(), address, MEMORY_INFORMATION_CLASS::MemoryBasicInformation, &region_info, sizeof(region_info), nullptr)) ?
                ui32_t(region_info.Protect) :
                null;
        }

        __forceinline bool set_memory_protect(address_t address, size_t size, ui32_t protect, ui32_t* _previous = nullptr) noexcept {
            auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
            if (!handle) return false;

            if (!_previous) {
                auto value = ui32_t();
                _previous = &value;
            }

            return NT_SUCCESS(NtProtectVirtualMemory(handle.get(), &address, &size, protect, PULONG(_previous)));
        }

        __forceinline address_t allocate_memory(size_t size = PAGE_SIZE, ui32_t protection = PAGE_EXECUTE_READWRITE, address_t address = nullptr) noexcept {
            auto result = address_t(nullptr);

            auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
            if (!handle) _Exit: return result;

            if (NT_SUCCESS(NtAllocateVirtualMemory(handle.get(), &address, null, &size, MEM_COMMIT, protection))) {
                result = address;
            }

            goto _Exit;
        }

        __forceinline bool release_memory(address_t address, size_t size = PAGE_SIZE) noexcept {
            auto result = bool(false);

            if (!(address && size)) _Exit: return result;

            auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
            if (!handle) goto _Exit;

            result =
                //NT_SUCCESS(NtFreeVirtualMemory(handle.get(), &address, &size, MEM_DECOMMIT)) &&
                NT_SUCCESS(NtFreeVirtualMemory(handle.get(), &address, &size, MEM_RELEASE));

            goto _Exit;
        }

        __forceinline constexpr bool write_memory(address_t address, const void* data, size_t size) noexcept {
            if (address && data && size) {
                if (_id == __process_id) return memcpy(address, data, size);

                auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
                auto handle_value = handle.get();
                if (handle_value) return NT_SUCCESS(NtWriteVirtualMemory(handle_value, address, address_t(data), size, nullptr));
            }

            return false;
        }

        template<typename _t> __forceinline constexpr bool write_memory(address_t address, const _t& data) noexcept {
            return write_memory(address, &data, sizeof(_t));
        }

        __forceinline constexpr bool read_memory(address_t address, size_t size, void* _data) const noexcept {
            if (address && size && _data) {
                if (_id == __process_id) return memcpy(_data, address, size);
                
                auto handle = temp_handle(_id, _handle, PROCESS_ALL_ACCESS);
                auto handle_value = handle.get();
                if (handle_value) return NT_SUCCESS(NtReadVirtualMemory(handle_value, address, _data, size, nullptr));
            }

            return false;
        }

        template<typename _t> __forceinline constexpr _t read_memory(address_t address) const noexcept {
            auto result = _t();
            read_memory(address, sizeof(_t), &result);
            return result;
        }
    };

    template<unsigned _bufferSize> static __forceinline std::vector<process> get_processes(unsigned open_access) {
        std::vector<process> results;

        id_t buffer[_bufferSize] = { null };
        unsigned long count = null;

        if (!K32EnumProcesses(buffer, sizeof(id_t) * _bufferSize, &count)) _Exit: return results;
        count /= sizeof(id_t);

        for (unsigned i = null; i < count; i++) {
            auto handle = ncore::process::get_handle(buffer[i], open_access);
            if (!handle) continue;

            __processHandleCloser(handle);

            results.push_back(process(buffer[i]));
        }

        goto _Exit;
    }
}