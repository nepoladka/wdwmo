#pragma once
#include "action.hpp"
#include "handle.hpp"
#include "defines.hpp"
#include "environment.hpp"
#include "static_array.hpp"

#include <memory>
#include <string>
#include <tuple>

/* available defines:
    NCORE_THREAD_EVENT_CREATION_FAILURE
*/

namespace ncore {
    static constexpr const auto const __defaultThreadOpenAccess = ui32_t(THREAD_ALL_ACCESS);
    static const auto const __threadHandleCloser = handle::native_handle_t::closer_t(NtClose);

    class thread {
    public:
        using context_t = CONTEXT;
        using environment_t = TEB;

        static __forceinline constexpr handle::native_t get_handle(id_t id, ui32_t access = __defaultThreadOpenAccess) {
            auto result = handle::native_t();
            auto attributes = OBJECT_ATTRIBUTES();
            auto client_id = CLIENT_ID();

            client_id.UniqueThread = handle::native_t(id);
            client_id.UniqueProcess = handle::native_t(null);
            InitializeObjectAttributes(&attributes, null, null, null, null);

            return NT_SUCCESS(NtOpenThread(&result, access, &attributes, &client_id)) ? result : nullptr;
        }

        static __forceinline auto get_info(handle::native_t handle) noexcept {
            using info_t = THREAD_BASIC_INFORMATION;

            auto information = info_t();
            if (handle) {
                NtQueryInformationThread(handle, THREADINFOCLASS::ThreadBasicInformation, &information, sizeof(information), nullptr);
            }

            return information;
        }

        static __forceinline auto get_id(handle::native_t handle) noexcept {
            return id_t(get_info(handle).ClientId.UniqueThread);
        }

    protected:
        using handle_t = handle::native_handle_t;
        using thread_procedure_t = void(*)(void*);
        using thread_start_t = PTHREAD_START_ROUTINE;

        id_t _id;
        handle_t _handle;

        __forceinline constexpr thread(id_t id, handle_t handle) noexcept {
            _id = id;
            _handle = handle;
        }

        __forceinline handle_t temp_handle(id_t id, const handle_t& source, ui32_t open_access) const noexcept {
            auto result = (handle_t&)source;
            if (!source) {
                (result = handle_t(get_handle(id, open_access), __threadHandleCloser, false)).close_on_destroy(true);
            }

            return result;
        }

        __forceinline bool set_suspended(bool state) const noexcept {
            auto result = false;

            auto handle = temp_handle(_id, _handle, THREAD_SUSPEND_RESUME);
            if (!handle) {
            _Exit:
                return result;
            }

            auto procedure = state ? NtSuspendThread : NtResumeThread;
            result = NT_SUCCESS(procedure(handle.get(), nullptr));

            goto _Exit;
        }

        static __forceinline handle::native_t create_ex(handle::native_t process, address_t start, address_t parameter, unsigned flags, size_t stack_size) noexcept {
            auto handle = handle::native_t();

            if (!process) {
                process = __current_process;
            }

            auto status = NtCreateThreadEx(&handle, THREAD_ALL_ACCESS, nullptr, process, start, parameter, flags, null, stack_size, null, nullptr);
#ifdef NCORE_THREAD_EVENT_CREATION_FAILURE
            if (NT_ERROR(status)) {
                NCORE_THREAD_EVENT_CREATION_FAILURE;
            }
#endif

            return handle;
        }

    public:
        __forceinline constexpr thread(id_t id = null, ui32_t open_access = null) noexcept {
            if ((_id = id) && open_access) {
                _handle = handle_t(get_handle(id, open_access), __threadHandleCloser, false);
            }
        }

        __forceinline thread(handle::native_t win32_handle) noexcept {
            if (win32_handle == __current_thread) {
                _id = __thread_id;
            }
            else {
                _id = get_id(win32_handle);
            }

            _handle = handle_t(win32_handle, __threadHandleCloser, false);
        }

        static __forceinline thread current(unsigned open_access = THREAD_ALL_ACCESS) noexcept {
            return open_access ? thread(__thread_id, open_access) : thread(__current_thread);
        }

        static __forceinline thread get_by_id(id_t id, unsigned open_access = null) noexcept {
            return thread(id, open_access);
        }

        static __forceinline thread create(address_t start, void* parameter = nullptr, handle::native_t process = nullptr, bool keep_handle = false, int priority = null, unsigned flags = null, size_t stack_size = null) noexcept {
            auto handle = create_ex(process, start, parameter, flags, stack_size);
            if (!handle) return thread();

            if (priority) {
                //set_priority(handle, priority); //cut from the public version
            }

            if (keep_handle) return thread(handle);

            auto id = get_id(handle);
            __threadHandleCloser(handle);

            return thread(id);
        }

        __forceinline id_t id() const noexcept {
            return _id;
        }

        __forceinline auto handle() const noexcept {
            return _handle.get();
        }

        __forceinline auto handle(ui32_t open_access = __defaultThreadOpenAccess) noexcept {
            return _handle.get() ? _handle.get() : (_handle = handle::native_handle_t(get_handle(_id, open_access), __threadHandleCloser)).get();
        }

        __forceinline auto close_handle() noexcept {
            return _handle.close();
        }

        __forceinline auto release() noexcept {
            return _handle.close();
        }

        __forceinline auto suspend() const noexcept {
            return set_suspended(true);
        }

        __forceinline auto resume() const noexcept {
            return set_suspended(false);
        }

        __forceinline auto terminate(long exit_status = EXIT_SUCCESS) const noexcept {
            auto handle = temp_handle(_id, _handle, THREAD_TERMINATE);
            return handle.get() ? NT_SUCCESS(NtTerminateThread(handle.get(), exit_status)) : false;
        }

        static __forceinline void set_timer_resolution(ui32_t time) noexcept {
            NtSetTimerResolution(time, true, nullptr);
        }

        static __forceinline void sleep_micro(i32_t microsecounds, bool alertable = false) noexcept {
            if (microsecounds < null) return;

            auto delay = __lit_micro(microsecounds);
            NtDelayExecution(alertable, &delay);
        }

        static __forceinline void sleep_mili(i32_t milisecounds, bool alertable = false) noexcept {
            return sleep_micro(milisecounds * 1000, alertable);
        }

        static __forceinline void sleep(i32_t time, bool alertable = false) noexcept {
#ifdef NCORE_THREAD_SLEEP_MICRO
            return sleep_micro(time, alertable);
#else
            return sleep_mili(time, alertable);
#endif
        }
    };
}