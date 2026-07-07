#pragma once
#include "process.hpp"
#include "dimension_vector.hpp"

namespace ncore {
    namespace utils {
        struct system_version {
            ui32_t major, minor, build;
        };

        template<bool _tryAccess = true> static __forceinline bool can_access_range(const void* begin, const void* end) noexcept {
            if constexpr (_tryAccess) {
                volatile byte_t buffer = { };

                __try {
                    buffer = begin == end ? 
                        (*byte_p(begin)) :
                        (*byte_p(begin) + *byte_p(end));
                }
                __except (true) {
                    return false;
                }

                return true;
            }
            else {
                auto process = process::current(null);

                return process.is_memory_available(address_t(begin)) && (begin == end || process.is_memory_available(address_t(end)));
            }
        }

        template<bool _tryAccess = true> static __forceinline bool can_access(const void* address) noexcept {
            return can_access_range<_tryAccess>(address, address);
        }

        static __forceinline bool get_window_rectangles(HWND window, POINT* _pos, SIZE* _size, RECT* _borders) {
            if (!window) return false;

            auto window_rect = RECT{ 0 };
            if (!GetWindowRect(window, &window_rect)) _Fail: return false;

            auto client_rect = RECT{ 0 };
            if (!GetClientRect(window, &client_rect)) goto _Fail;

            auto result = true;

            if (_size) {
                _size->cx = client_rect.right;
                _size->cy = client_rect.bottom;
                result &= _size->cx > 0 && _size->cy > 0;
            }

            MapWindowPoints(window, null, (LPPOINT)&client_rect, 2);

            auto WindowBorders = RECT{
                client_rect.left - window_rect.left,
                client_rect.top - window_rect.top,
                window_rect.right - client_rect.right,
                window_rect.bottom - client_rect.bottom
            };

            if (_borders) {
                *_borders = WindowBorders;
            }

            if (_pos) {
                _pos->x = window_rect.left + WindowBorders.left;
                _pos->y = window_rect.top + WindowBorders.top;
            }

            return result;
        }
        
        static __forceinline vec2f get_window_scale(HWND window) {
            if (!window) return { 1.f, 1.f };

            auto dpi = GetDpiForWindow(window);
            auto scale = float(dpi) / 96.f;

            return { scale, scale };
        }

        static __forceinline unsigned __int64 system_boot_time() {
            static auto time = unsigned __int64(0);
            if (!time) {
                struct {
                    unsigned __int64 boot;
                    unsigned __int64 current;
                    unsigned __int64 time_zone_bias;
                    unsigned __int32 time_zone_id;
                }system_time;

                if (NT_SUCCESS(NtQuerySystemInformation(SystemTimeOfDayInformation, &system_time, sizeof(system_time), 0))) {
                    time = system_time.boot;
                }
            }

            return time;
        }

        static __forceinline RTL_OSVERSIONINFOW get_system_version_info() {
            static RTL_OSVERSIONINFOW* result = nullptr;
            if (!result) {
                RtlGetVersion(result = new RTL_OSVERSIONINFOW());
            }

            return *result;
        }

        static __forceinline system_version get_system_version(bool adjust_major = false) {
            static system_version* result = nullptr;
            if (!result) {
                auto info = get_system_version_info();

                result = new system_version {
                    (adjust_major && info.dwBuildNumber >= 22000) ? 11 : info.dwMajorVersion,
                    info.dwMinorVersion,
                    info.dwBuildNumber
                };

            }

            return *result;
        }


        //barely changed github.com/TheCruZ/Simple-Manual-Map-Injector, very old, almost deprecated
        static __forceinline address_t manual_map_library(HANDLE process, byte_t* file, size_t size, unsigned fdwReason = DLL_PROCESS_ATTACH, address_t lpReserved = NULL, bool clearHeader = true, bool clearNonNeededSections = true, bool adjustProtections = true, bool sehExceptionSupport = true) {
            using f_LoadLibraryA = HINSTANCE(WINAPI*)(const char* lpLibFilename);
            using f_GetProcAddress = FARPROC(WINAPI*)(HMODULE hModule, LPCSTR lpProcName);
            using f_DLL_ENTRY_POINT = BOOL(WINAPI*)(void* hDll, DWORD dwReason, void* pReserved);
            using f_RtlAddFunctionTable = BOOL(WINAPIV*)(PRUNTIME_FUNCTION FunctionTable, DWORD EntryCount, DWORD64 BaseAddress);

            struct MANUAL_MAPPING_DATA
            {
                f_LoadLibraryA pLoadLibraryA;
                f_GetProcAddress pGetProcAddress;

                f_RtlAddFunctionTable pRtlAddFunctionTable;

                BYTE* pbase;
                HINSTANCE hMod;
                DWORD fdwReasonParam;
                LPVOID reservedParam;
                BOOL SEHSupport;
            };

            struct LOCAL {
                static __declspec(noinline) void __stdcall Shellcode(MANUAL_MAPPING_DATA* pData) {
                    if (!pData) {
                        pData->hMod = (HINSTANCE)0x404040;

                        return;
                    }

                    BYTE* pBase = pData->pbase;
                    auto* pOpt = &reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + reinterpret_cast<IMAGE_DOS_HEADER*>((uintptr_t)pBase)->e_lfanew)->OptionalHeader;

                    auto _LoadLibraryA = pData->pLoadLibraryA;
                    auto _GetProcAddress = pData->pGetProcAddress;
#ifdef _WIN64
                    auto _RtlAddFunctionTable = pData->pRtlAddFunctionTable;
#endif
                    auto _DllMain = reinterpret_cast<f_DLL_ENTRY_POINT>(pBase + pOpt->AddressOfEntryPoint);

                    BYTE* LocationDelta = pBase - pOpt->ImageBase;
                    if (LocationDelta) {
                        if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
                            auto* pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
                            const auto* pRelocEnd = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<uintptr_t>(pRelocData) + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size);
                            while (pRelocData < pRelocEnd && pRelocData->SizeOfBlock) {
                                UINT AmountOfEntries = (pRelocData->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                                WORD* pRelativeInfo = reinterpret_cast<WORD*>(pRelocData + 1);

                                for (UINT i = 0; i != AmountOfEntries; ++i, ++pRelativeInfo) {
                                    if (((*pRelativeInfo) >> 0x0C) == IMAGE_REL_BASED_DIR64) {
                                        UINT_PTR* pPatch = reinterpret_cast<UINT_PTR*>(pBase + pRelocData->VirtualAddress + ((*pRelativeInfo) & 0xFFF));
                                        *pPatch += reinterpret_cast<UINT_PTR>(LocationDelta);
                                    }
                                }

                                pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pRelocData) + pRelocData->SizeOfBlock);
                            }
                        }
                    }

                    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
                        auto* pImportDescr = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
                        while (pImportDescr->Name) {
                            char* szMod = reinterpret_cast<char*>(pBase + pImportDescr->Name);
                            HINSTANCE hDll = _LoadLibraryA(szMod);

                            ULONG_PTR* pThunkRef = reinterpret_cast<ULONG_PTR*>(pBase + pImportDescr->OriginalFirstThunk);
                            ULONG_PTR* pFuncRef = reinterpret_cast<ULONG_PTR*>(pBase + pImportDescr->FirstThunk);

                            if (!pThunkRef) {
                                pThunkRef = pFuncRef;
                            }

                            for (; *pThunkRef; ++pThunkRef, ++pFuncRef) {
                                if (IMAGE_SNAP_BY_ORDINAL(*pThunkRef)) {
                                    *pFuncRef = (ULONG_PTR)_GetProcAddress(hDll, reinterpret_cast<char*>(*pThunkRef & 0xFFFF));
                                }
                                else {
                                    auto* pImport = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + (*pThunkRef));
                                    *pFuncRef = (ULONG_PTR)_GetProcAddress(hDll, pImport->Name);
                                }

                            }

                            ++pImportDescr;
                        }
                    }

                    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
                        auto* pTLS = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
                        auto* pCallback = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(pTLS->AddressOfCallBacks);

                        for (; pCallback && *pCallback; ++pCallback) {
                            (*pCallback)(pBase, DLL_PROCESS_ATTACH, NULL);
                        }
                    }

                    bool ExceptionSupportFailed = false;

#ifdef _WIN64

                    if (pData->SEHSupport) {
                        auto excep = pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
                        if (excep.Size) {
                            if (!_RtlAddFunctionTable(
                                reinterpret_cast<IMAGE_RUNTIME_FUNCTION_ENTRY*>(pBase + excep.VirtualAddress),
                                excep.Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY), (DWORD64)pBase)) {
                                ExceptionSupportFailed = true;
                            }

                        }

                    }

#endif

                    _DllMain(pBase, pData->fdwReasonParam, pData->reservedParam);

                    if (ExceptionSupportFailed) {
                        pData->hMod = reinterpret_cast<HINSTANCE>(0x505050);
                    }
                    else {
                        pData->hMod = reinterpret_cast<HINSTANCE>(pBase);
                    }
                }
            };

            IMAGE_NT_HEADERS* pOldNtHeader = NULL;
            IMAGE_OPTIONAL_HEADER* pOldOptHeader = NULL;
            IMAGE_FILE_HEADER* pOldFileHeader = NULL;
            BYTE* pTargetBase = NULL;

            //"MZ"
            if (reinterpret_cast<IMAGE_DOS_HEADER*>(file)->e_magic != 0x5A4D) return NULL;

            pOldNtHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(file + reinterpret_cast<IMAGE_DOS_HEADER*>(file)->e_lfanew);
            pOldOptHeader = &pOldNtHeader->OptionalHeader;
            pOldFileHeader = &pOldNtHeader->FileHeader;

            if (pOldFileHeader->Machine != IMAGE_FILE_MACHINE_AMD64) return NULL;

            pTargetBase = reinterpret_cast<BYTE*>(VirtualAllocEx(process, NULL, pOldOptHeader->SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!pTargetBase) return NULL;

            DWORD oldp = 0;
            VirtualProtectEx(process, pTargetBase, pOldOptHeader->SizeOfImage, PAGE_EXECUTE_READWRITE, &oldp);

            MANUAL_MAPPING_DATA data{ 0 };
            data.pLoadLibraryA = LoadLibraryA;
            data.pGetProcAddress = GetProcAddress;
            data.pRtlAddFunctionTable = (f_RtlAddFunctionTable)RtlAddFunctionTable;
            data.pbase = pTargetBase;
            data.fdwReasonParam = fdwReason;
            data.reservedParam = lpReserved;
            data.SEHSupport = sehExceptionSupport;


            //File header, only first 0x1000 bytes for the header
            if (NtWriteVirtualMemory(process, pTargetBase, file, 0x1000, NULL)) {
                VirtualFreeEx(process, pTargetBase, 0, MEM_RELEASE);

                return NULL;
            }

            IMAGE_SECTION_HEADER* pSectionHeader = IMAGE_FIRST_SECTION(pOldNtHeader);
            for (UINT i = 0; i != pOldFileHeader->NumberOfSections; ++i, ++pSectionHeader) {
                if (pSectionHeader->SizeOfRawData) {
                    if (NtWriteVirtualMemory(process, (pTargetBase + pSectionHeader->VirtualAddress), file + pSectionHeader->PointerToRawData, pSectionHeader->SizeOfRawData, NULL)) {
                        VirtualFreeEx(process, pTargetBase, 0, MEM_RELEASE);

                        return NULL;
                    }
                }
            }

            //Mapping params
            BYTE* MappingDataAlloc = reinterpret_cast<BYTE*>(VirtualAllocEx(process, NULL, sizeof(MANUAL_MAPPING_DATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!MappingDataAlloc) {
                VirtualFreeEx(process, pTargetBase, NULL, MEM_RELEASE);

                return NULL;
            }

            if (NtWriteVirtualMemory(process, MappingDataAlloc, &data, sizeof(MANUAL_MAPPING_DATA), NULL)) {
                VirtualFreeEx(process, pTargetBase, NULL, MEM_RELEASE);
                VirtualFreeEx(process, MappingDataAlloc, NULL, MEM_RELEASE);

                return NULL;
            }

            //Shell code
            void* pShellcode = VirtualAllocEx(process, NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!pShellcode) {
                VirtualFreeEx(process, pTargetBase, NULL, MEM_RELEASE);
                VirtualFreeEx(process, MappingDataAlloc, NULL, MEM_RELEASE);

                return NULL;
            }

            if (NtWriteVirtualMemory(process, pShellcode, LOCAL::Shellcode, 0x1000, NULL)) {
                VirtualFreeEx(process, pTargetBase, NULL, MEM_RELEASE);
                VirtualFreeEx(process, MappingDataAlloc, NULL, MEM_RELEASE);
                VirtualFreeEx(process, pShellcode, NULL, MEM_RELEASE);

                return NULL;
            }

            HANDLE hThread = CreateRemoteThread(process, NULL, NULL, (LPTHREAD_START_ROUTINE)pShellcode, MappingDataAlloc, NULL, NULL);
            if (!hThread) {
                VirtualFreeEx(process, pTargetBase, 0, MEM_RELEASE);
                VirtualFreeEx(process, MappingDataAlloc, 0, MEM_RELEASE);
                VirtualFreeEx(process, pShellcode, 0, MEM_RELEASE);

                return NULL;
            }
            CloseHandle(hThread);


            HINSTANCE hCheck = NULL;
            while (!hCheck) {
                DWORD exitcode = 0;
                GetExitCodeProcess(process, &exitcode);
                if (exitcode != STILL_ACTIVE) return NULL;

                MANUAL_MAPPING_DATA data_checked{ 0 };
                NtReadVirtualMemory(process, MappingDataAlloc, &data_checked, sizeof(data_checked), NULL);
                hCheck = data_checked.hMod;

                if (hCheck == (HINSTANCE)0x404040) {
                    VirtualFreeEx(process, pTargetBase, 0, MEM_RELEASE);
                    VirtualFreeEx(process, MappingDataAlloc, 0, MEM_RELEASE);
                    VirtualFreeEx(process, pShellcode, 0, MEM_RELEASE);
                    return NULL;
                }

                Sleep(10);
            }

            BYTE* emptyBuffer = (BYTE*)malloc(1024 * 1024 * 20);
            if (emptyBuffer == NULL) return NULL;
            memset(emptyBuffer, BYTE(0), 1024 * 1024 * 20);

            //CLEAR PE HEAD
            if (clearHeader) {
                NtWriteVirtualMemory(process, pTargetBase, emptyBuffer, 0x1000, NULL);
            }
            //END CLEAR PE HEAD


            if (clearNonNeededSections) {
                pSectionHeader = IMAGE_FIRST_SECTION(pOldNtHeader);
                for (UINT i = 0; i != pOldFileHeader->NumberOfSections; ++i, ++pSectionHeader) {
                    if (pSectionHeader->Misc.VirtualSize) {
                        if ((sehExceptionSupport ? 0 : strcmp((char*)pSectionHeader->Name, ".pdata") == 0) || strcmp((char*)pSectionHeader->Name, ".rsrc") == 0 || strcmp((char*)pSectionHeader->Name, ".reloc") == 0) {
                            NtWriteVirtualMemory(process, (pTargetBase + pSectionHeader->VirtualAddress), emptyBuffer, pSectionHeader->Misc.VirtualSize, NULL);
                        }
                    }
                }
            }

            if (adjustProtections) {
                pSectionHeader = IMAGE_FIRST_SECTION(pOldNtHeader);
                for (UINT i = 0; i != pOldFileHeader->NumberOfSections; ++i, ++pSectionHeader) {
                    if (pSectionHeader->Misc.VirtualSize) {
                        DWORD old = 0;
                        DWORD newP = PAGE_READONLY;

                        if ((pSectionHeader->Characteristics & IMAGE_SCN_MEM_WRITE) > 0) {
                            newP = PAGE_READWRITE;
                        }
                        else if ((pSectionHeader->Characteristics & IMAGE_SCN_MEM_EXECUTE) > 0) {
                            newP = PAGE_EXECUTE_READ;
                        }

                        VirtualProtectEx(process, pTargetBase + pSectionHeader->VirtualAddress, pSectionHeader->Misc.VirtualSize, newP, &old);
                    }
                }

                DWORD old = 0;
                VirtualProtectEx(process, pTargetBase, IMAGE_FIRST_SECTION(pOldNtHeader)->VirtualAddress, PAGE_READONLY, &old);
            }

            NtWriteVirtualMemory(process, pShellcode, emptyBuffer, 0x1000, NULL);

            VirtualFreeEx(process, pShellcode, NULL, MEM_RELEASE);

            VirtualFreeEx(process, MappingDataAlloc, NULL, MEM_RELEASE);

            return pTargetBase;
        }
    }

    using namespace utils;
}
