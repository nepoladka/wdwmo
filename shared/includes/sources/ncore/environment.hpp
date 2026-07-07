#pragma once
#include <windows.h>
#include <ntstatus.h>
#include "includes/ntos.h"

#define NtCurrentTeb() (PTEB(__readgsqword(offsetof(NT_TIB, Self))))

#define __thread_environment NtCurrentTeb()
#define __process_environment (__thread_environment->ProcessEnvironmentBlock)
#define __process_id DWORD32(__thread_environment->ClientId.UniqueProcess)
#define __thread_id DWORD32(__thread_environment->ClientId.UniqueThread)

#define __current_process HANDLE(-1)
#define __current_thread HANDLE(-2)

#define __lit_micro(NUM) [](__int64 v){ v *= -10; return *PLARGE_INTEGER(&v); }(NUM)
#define __lit_mili(NUM) __lit_micro(NUM * 1000) //large integer time milisecounds

#ifdef NCORE_ENVIRONMENT_OVERRIDE_WINAPI
#define GetCurrentProcess() __current_process
#define GetCurrentProcessId() __process_id

#define GetCurrentThread() __current_thread
#define GetCurrentThreadId() __thread_id

#define GetCommandLineW() (__process_environment->ProcessParameters->CommandLine.Buffer)

#define GetLastError() DWORD32(__thread_environment->LastErrorValue)
#define SetLastError(VALUE) (__thread_environment->LastErrorValue = VALUE)

#define GetLastStatus() NTSTATUS(__thread_environment->LastStatusValue)
#define SetLastStatus(VALUE) (__thread_environment->LastStatusValue = VALUE)
#endif

namespace ncore::environment {
#ifdef _NCORE_HANDLE
	static __forceinline auto wait_for(handle::native_t object, ui32_t timeout) {
		auto time = __lit_mili(timeout);
		return NtWaitForSingleObject(object, false, &time);
	}
#endif
}