#include "wdwmo.hpp"

#include <windows.h>
#include <atlbase.h>
#include <dxgi1_5.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <setupapi.h>
#include <devguid.h>
#include <shellscalingapi.h>

#include "dxgiu.hpp"

#include "../../../ncore/source/task.hpp"
#include "../../../ncore/source/utils.hpp"
#include "../../../ncore/source/base64.hpp"

#include <minhook.h>

#pragma comment(lib, "minhook.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shcore.lib")

//#ifdef DEBUG
#define WDWMO_LOGS
//#endif

//#define WDWMO_DIAGNOSTICS
#define WDWMO_NO_HOT_LOG
#define function_name "\033[96m" __FUNCTION__ "\033[0m"

#ifdef WDWMO_LOGS
#define conlog debug::conlogf
#ifdef WDWMO_NO_HOT_LOG
#define conlogh(...)
#else
#define conlogh(DELAY, ...) { static ui64_t ___log_time = null; ___log_time = debug::conlogfh(DELAY, ___log_time, __VA_ARGS__); }
#endif
#else
#define conlog(...)
#define conlogh(...)
#endif

namespace wdwmo {
    using namespace ncore::types;

#ifdef WDWMO_DIAGNOSTICS
    static constexpr const bool const __diagnosticFeatures = true;
#else
    static constexpr const bool const __diagnosticFeatures = false;
#endif

    namespace debug {
        static HANDLE console_output = nullptr;
        static message_callback_t message_callback = nullptr;

        void set_message_callback(message_callback_t callback) noexcept {
            message_callback = callback;
        }

        message_callback_t get_message_callback() noexcept {
            return message_callback;
        }

        void set_console_output(void* handle) noexcept { 
            console_output = (HANDLE)handle; 
        }

        void* get_console_output() noexcept { 
            return console_output;
        }

        int conlogf(const char* fmt, ...) noexcept {
            bool console = console_output && console_output != INVALID_HANDLE_VALUE;
            bool callback = message_callback;

            if (!console && !callback) return null;

            char buffer[4096] = { };
            va_list args;
            va_start(args, fmt);
            int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
            va_end(args);

            if (len <= 0) return null;

            if (len >= int(sizeof(buffer))) {
                len = int(sizeof(buffer)) - 1;
            }

            if (callback) return int(message_callback(buffer, len));
            else if (console) {
                auto written = DWORD();
                WriteConsoleA(console_output, buffer, DWORD(len), &written, nullptr);
                return int(written);
            }
            else return null; //unreachable
        }

        ui64_t conlogfh(ui64_t delay, ui64_t previous, const char* fmt, ...) noexcept {
            auto no_delay = delay == 0;

            auto current = no_delay ? 0 : GetTickCount64();
            if (no_delay || (current - previous) > delay) {
                bool console = console_output && console_output != INVALID_HANDLE_VALUE;
                bool callback = message_callback;

                if (!console && !callback) return null;

                char buffer[4096] = { };
                va_list args;
                va_start(args, fmt);
                int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
                va_end(args);

                if (len <= 0) return null;

                if (len >= int(sizeof(buffer))) {
                    len = int(sizeof(buffer)) - 1;
                }

                if (callback) {
                    message_callback(buffer, len);
                }
                else if (console) {
                    auto written = DWORD();
                    WriteConsoleA(console_output, buffer, DWORD(len), &written, nullptr);
                }
                else {
                    //nothing, unreachable
                }

                return current;
            }

            return previous;
        }
    }

    namespace utils {
        __forceinline void release_dxany(auto*& instance) noexcept {
            if (instance) {
                instance->Release();
                instance = nullptr;
            }
        }

        __forceinline bool get_pe_info(const void* data, size_t size, executable_image_info_t& _info) noexcept {
            if (size < sizeof(IMAGE_DOS_HEADER)) return false;

            auto dos = (const IMAGE_DOS_HEADER*)data;
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

            if (dos->e_lfanew <= 0 || size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > size) return false;

            auto nt32 = (const IMAGE_NT_HEADERS32*)(ui64_t(data) + dos->e_lfanew);
            if (nt32->Signature != IMAGE_NT_SIGNATURE) return false;

            _info.timestamp = nt32->FileHeader.TimeDateStamp;

            if (nt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                auto nt = (const IMAGE_NT_HEADERS64*)nt32;

                _info.size = nt->OptionalHeader.SizeOfImage;
                _info.checksum = nt->OptionalHeader.CheckSum;
            }
            else if (nt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                _info.size = nt32->OptionalHeader.SizeOfImage;
                _info.checksum = nt32->OptionalHeader.CheckSum;
            }
            else return false;

            return true;
        }

        template<typename _t = void*, bool _checks = false>
        __forceinline _t get_class_virtual_function(void* target, const i32_t offset) noexcept {
            auto vftable = *address_p(target);
            if constexpr (_checks) if (not ncore::can_access(vftable)) return nullptr;

            auto function = *address_p(ui64_t(vftable) + offset);
            if constexpr (_checks) if (not ncore::can_access(function)) return nullptr;

            return _t(function);
        }

        __forceinline vec2f get_monitor_scale(HMONITOR monitor) noexcept {
            UINT x = 96;
            UINT y = 96;

            if (!monitor || FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y))) return { 1.0f, 1.0f };

            return {
                float(x) / 96.0f,
                float(y) / 96.0f
            };
        }

        bool is_monitor_primary(void* monitor) noexcept {
            if (!monitor) return true;

            auto info = MONITORINFO{
                .cbSize = sizeof(MONITORINFO)
            };

            return GetMonitorInfoA((HMONITOR)monitor, &info) && (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        }
        
        rect_t get_monitor_rect(void* monitor, bool work) noexcept {
            if (!monitor) {
                monitor = MonitorFromPoint({ .x = 0, .y = 0 }, MONITOR_DEFAULTTOPRIMARY);
            }

            auto info = MONITORINFO{
                .cbSize = sizeof(MONITORINFO)
            };

            GetMonitorInfoA((HMONITOR)monitor, &info);

            return *rect_p(work ?
                (&info.rcWork) :
                (&info.rcMonitor));
        }

        ui64_t get_monitor_scale(void* monitor) noexcept {
            return *ui64_p(get_monitor_scale((HMONITOR)monitor).array);
        }
    }

    namespace details {
        using namespace utils;


        static constexpr const char __hlslRotationShaderVertex[] = { R"(
            struct vsi {
                float2 pos : POSITION;
                float2 uv  : TEXCOORD0;
            };

            struct psi {
                float4 pos : SV_POSITION;
                float2 uv  : TEXCOORD0;
            };

            psi m(vsi i) {
                psi o;
                o.pos = float4(i.pos, 0.0f, 1.0f);
                o.uv = i.uv;
                return o;
            }
        )" };

        static constexpr const char __hlslRotationShaderPixel[] = { R"(
            Texture2D ot : register(t0);
            SamplerState os : register(s0);

            struct psi {
                float4 pos : SV_POSITION;
                float2 uv  : TEXCOORD0;
            };

            float4 m(psi i) : SV_TARGET {
                return ot.Sample(os, i.uv);
            }
        )" };


        static ID3DBlob* rotation_shader_blob_vertex = nullptr;
        static ID3DBlob* rotation_shader_blob_pixel = nullptr;


        struct offscreen_view_t {
            ID3D11Texture2D* texture = nullptr;
            ID3D11RenderTargetView* render_target_view = nullptr;
            ID3D11ShaderResourceView* shader_resource_view = nullptr;

            UINT width = 0;   // callback/upright width
            UINT height = 0;  // callback/upright height
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

            __forceinline void release() noexcept {
                release_dxany(shader_resource_view);
                release_dxany(render_target_view);
                release_dxany(texture);

                width = 0;
                height = 0;
                format = DXGI_FORMAT_UNKNOWN;
            }
        };

        struct final_view_t {
            ID3D11Texture2D* texture = nullptr; // original DWM texture
            ID3D11RenderTargetView* render_target_view = nullptr; // original DWM RTV

            bool texture_referenced = false;
            bool render_target_view_referenced = false;

            UINT width = 0; // native DWM buffer width
            UINT height = 0; // native DWM buffer height
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_UNSPECIFIED;

            __forceinline void release() noexcept {
                if (render_target_view_referenced) {
                    release_dxany(render_target_view);
                }
                else {
                    render_target_view = nullptr;
                }

                if (texture_referenced) {
                    release_dxany(texture);
                }
                else {
                    texture = nullptr;
                }

                texture_referenced = false;
                render_target_view_referenced = false;
                width = 0;
                height = 0;
                format = DXGI_FORMAT_UNKNOWN;
                rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
            }
        };

        struct composite_vertex_t {
            float pos[2];
            float uv[2];
        };

        struct composite_pipeline_t {
            ID3D11VertexShader* vertex_shader = nullptr;
            ID3D11PixelShader* pixel_shader = nullptr;
            ID3D11InputLayout* input_layout = nullptr;

            ID3D11Buffer* vertex_buffer = nullptr;
            ID3D11Buffer* index_buffer = nullptr;

            ID3D11SamplerState* sampler = nullptr;
            ID3D11BlendState* blend = nullptr;
            ID3D11RasterizerState* rasterizer = nullptr;
            ID3D11DepthStencilState* depth_stencil = nullptr;

            __forceinline void release() noexcept {
                release_dxany(depth_stencil);
                release_dxany(rasterizer);
                release_dxany(blend);
                release_dxany(sampler);
                release_dxany(index_buffer);
                release_dxany(vertex_buffer);
                release_dxany(input_layout);
                release_dxany(pixel_shader);
                release_dxany(vertex_shader);
            }
        };

        struct composite_state_backup_t {
            ID3D11InputLayout* input_layout = nullptr;
            ID3D11Buffer* vertex_buffer = nullptr;
            UINT vertex_stride = 0;
            UINT vertex_offset = 0;
            ID3D11Buffer* index_buffer = nullptr;
            DXGI_FORMAT index_format = DXGI_FORMAT_UNKNOWN;
            UINT index_offset = 0;
            D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

            ID3D11VertexShader* vertex_shader = nullptr;
            ID3D11PixelShader* pixel_shader = nullptr;
            ID3D11GeometryShader* geometry_shader = nullptr;
            ID3D11HullShader* hull_shader = nullptr;
            ID3D11DomainShader* domain_shader = nullptr;

            ID3D11ShaderResourceView* pixel_shader_shader_resource_view = nullptr;
            ID3D11SamplerState* pixel_shader_sampler = nullptr;

            ID3D11BlendState* blend = nullptr;
            FLOAT blend_factor[4] = { };
            UINT sample_mask = 0xffffffff;
            ID3D11DepthStencilState* depth_stencil = nullptr;
            UINT stencil_ref = 0;

            ID3D11RasterizerState* rasterizer = nullptr;
            D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = { };
            UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            RECT scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = { };
            UINT scissor_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        };

        struct private_data_t {
            offscreen_view_t offscreen_view;
            final_view_t final_view;
            composite_pipeline_t composite_pipeline;

            __forceinline void release() noexcept {
                composite_pipeline.release();
                offscreen_view.release();
                final_view.release();
            }
        };


        IDXGISwapChainDWMLegacy* retrieve_dxgi_swap_chain(const i32_t struct_offset, void* swap_chain, const range<address_t>* dxgi_module_bounds = nullptr) noexcept {
            auto target = ui64_p(i64_t(swap_chain) + struct_offset);

            conlogh(1000,
                "[w] " function_name ": \n"
                "    offset:             %#lx\n"
                "    overlay swap chain: %#llx\n"
                "    target address:     %#llx\n",
                struct_offset,
                swap_chain,
                target);

            if (not ncore::can_access(target)) return nullptr;

            auto result = address_t(*target);
            if (not ncore::can_access(result)) return nullptr;

            if (dxgi_module_bounds) {
                auto vftable = *address_p(result);
                if (not ncore::can_access(vftable) or not dxgi_module_bounds->in_range(vftable)) return nullptr;
            }

            return (IDXGISwapChainDWMLegacy*)result;
        }

        void* get_physical_back_buffer(const i32_t offset, void* swap_chain) noexcept {
            auto function = get_class_virtual_function<ncore::action<void*>::procedure_t<void*>>(swap_chain, offset);

            return function ?
                function(swap_chain) :
                nullptr;
        }

        ID3D11Resource* get_d3d11_resource(const i32_t offset, void* swap_chain_buffer) noexcept {
            auto function = get_class_virtual_function<ncore::action<ID3D11Resource*>::procedure_t<void*>>(swap_chain_buffer, offset);

            return function ?
                function(swap_chain_buffer) :
                nullptr;
        }

        IUnknown* get_dxgi_dwm_output(const i32_t render_target_offset, const ui32_t vftable_offset, const range<address_t>& dxgi_bounds, void* overlay_context, i32_t& offset, i32_t begin = 0, i32_t count = 30) noexcept {
            auto expected_vftable_address = address_t(ui64_t(dxgi_bounds.min) + vftable_offset);
            auto structure = ui64_p(*ui64_p(ui64_t(overlay_context) + render_target_offset)); //CRenderTarget*

            conlog(
                "[w] " function_name ": \n"
                "    dxgi_bounds:     %#llx-%#llx\n"
                "    overlay_context: %#llx\n"
                "    offset:          %#lx\n"
                "    structure:       %#llx\n",
                dxgi_bounds.min, dxgi_bounds.max,
                overlay_context,
                offset,
                structure);

            if (offset) return (IUnknown*)(*ui64_p(ui64_t(structure) + offset));

            auto start = structure + begin;

            for (auto i = 0i32; i < count; i++) {
                auto pointer = start[i];
                if (!ncore::can_access_range(address_t(pointer), address_t(pointer + sizeof(address_t)))) continue;

                auto vftable_candidate = *address_p(pointer);
                if (vftable_candidate != expected_vftable_address) continue;
                
                offset = i * sizeof(ui64_t);

                return (IUnknown*)pointer;
            }

            return nullptr;
        }

        void get_buffer_info(chain_info_t& _info, ID3D11Texture2D* buffer) noexcept {
            auto description = D3D11_TEXTURE2D_DESC(); {
                buffer->GetDesc(&description);
            }

            _info.buffer = {
                .width = description.Width,
                .height = description.Height,
                .format = ui32_t(description.Format)
            };
        }

        bool get_adapter_info(chain_info_t& _info, IDXGIAdapter* adapter) noexcept {
            if (auto description = DXGI_ADAPTER_DESC(); SUCCEEDED(adapter->GetDesc(&description))) {
                _info.adapter = {
                    .id = *(luid_t*)&description.AdapterLuid,

                    .vendor_id = description.VendorId,
                    .device_id = description.DeviceId,
                    .subsys_id = description.SubSysId,
                    .revision = description.Revision,

                    .dedicated_video_memory = ui64_t(description.DedicatedVideoMemory),
                    .dedicated_system_memory = ui64_t(description.DedicatedSystemMemory),
                    .shared_system_memory = ui64_t(description.SharedSystemMemory),

                    .name = ncore::compatible_string(description.Description).string()
                };

                return true;
            }

            return false;
        }

        bool get_output_display_path_info(
            chain_info_t& _info, 
            const luid_t& adapter_id, 
            ui32_t source_id, 
            ui32_t target_id) {
            constexpr ui32_t __flags = QDC_ALL_PATHS; //not QDC_ONLY_ACTIVE_PATHS for duplicate mode supportion

            auto path_count = ui32_t();
            auto mode_count = ui32_t();

            auto paths = std::vector<DISPLAYCONFIG_PATH_INFO>();
            auto modes = std::vector<DISPLAYCONFIG_MODE_INFO>();

            auto result = LONG(ERROR_SUCCESS);

            //QueryDisplayConfig config can be changed between requests
            do {
                result = GetDisplayConfigBufferSizes(__flags, &path_count, &mode_count);
                if (result != ERROR_SUCCESS) return false;

                paths.resize(path_count);
                modes.resize(mode_count);

                result = QueryDisplayConfig(__flags,
                    &path_count, paths.data(),
                    &mode_count, modes.data(),
                    nullptr);
            } while (result == ERROR_INSUFFICIENT_BUFFER);

            if (result != ERROR_SUCCESS) return false;

            for (auto i = 0ui32; i < path_count; ++i) {
                const auto& path = paths[i];
                const auto& target = path.targetInfo;
                const auto& source = path.sourceInfo;

                if (!target.targetAvailable || 
                    *ui64_p(&adapter_id) != *ui64_p(&source.adapterId) ||
                    *ui64_p(&adapter_id) != *ui64_p(&target.adapterId) ||
                    source.id != source_id || 
                    target.id != target_id) continue;

                _info.output_display.active = (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
                _info.output_display.rotation = target.rotation;
                _info.output_display.output_technology = target.outputTechnology;

                return true;
            }

            return false;
        }

        bool get_output_display_info(
            chain_info_t& _info,
            HMONITOR monitor,
            const luid_t& adapter_id,
            ui32_t source_vid_pn_id,
            ui32_t target_vid_pn_id) noexcept {
            auto source_name = DISPLAYCONFIG_SOURCE_DEVICE_NAME{
                .header = {
                    .type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                    .size = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME),
                    .adapterId = *(const LUID*)&adapter_id,
                    .id = source_vid_pn_id
                }
            };

            auto target_name = DISPLAYCONFIG_TARGET_DEVICE_NAME{
                .header = {
                    .type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,
                    .size = sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME),
                    .adapterId = *(const LUID*)&adapter_id,
                    .id = target_vid_pn_id
                }
            };

            const bool has_source_name = DisplayConfigGetDeviceInfo(&source_name.header) == ERROR_SUCCESS;
            const bool has_target_name = DisplayConfigGetDeviceInfo(&target_name.header) == ERROR_SUCCESS;

            auto monitor_path = ncore::compatible_string();
            auto friendly_name = ncore::compatible_string();
            auto gdi_name = ncore::compatible_string();

            if (has_target_name) {
                monitor_path = target_name.monitorDevicePath;
                friendly_name = target_name.monitorFriendlyDeviceName;
            }

            if (has_source_name) {
                gdi_name = source_name.viewGdiDeviceName;
            }

            _info.output_display.handle = monitor;

            _info.output_display.primary = is_monitor_primary(monitor);

            memcpy(_info.output_display.scale, get_monitor_scale(monitor).array, sizeof(vec2f));

            if (!get_output_display_path_info(_info, adapter_id, source_vid_pn_id, target_vid_pn_id)) {
                _info.output_display.active = true;
                _info.output_display.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
                _info.output_display.output_technology = target_name.outputTechnology;
            }

            _info.output_display.monitor_path = monitor_path.string();
            _info.output_display.gdi_name = gdi_name.string();
            _info.output_display.friendly_name = friendly_name.string();

            _info.output_display.adapter_id = adapter_id;

            _info.output_display.path_id = {
                .source = source_vid_pn_id,
                .target = target_vid_pn_id
            };

            return true;
        }

        bool get_environment_info(
            chain_info_t& _info,
            ID3D11Device* device,
            ID3D11Texture2D* buffer,
            IUnknown* output = nullptr) noexcept {
            conlog(
                "[w] " function_name ": \n"
                "    device: %#llx\n"
                "    buffer: %#llx\n"
                "    output: %#llx\n",
                device,
                buffer,
                output);

            if (!device || !buffer) return false;

            get_buffer_info(_info, buffer);

            if (auto dxgi_device = CComPtr<IDXGIDevice>(); SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device)))) {
                if (auto adapter = CComPtr<IDXGIAdapter>(); SUCCEEDED(dxgi_device->GetAdapter(&adapter))) {
                    get_adapter_info(_info, adapter);

                    if (output) {
                        auto monitor = HMONITOR();
                        auto source_vid_pn_id = ui32_t();
                        auto target_vid_pn_id = ui32_t();

                        if (auto dxgi_output = CComPtr<IDXGIOutput>(); SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&dxgi_output)))) {
                            if (auto description = DXGI_OUTPUT_DESC(); SUCCEEDED(dxgi_output->GetDesc(&description))) {
                                monitor = description.Monitor;

                                _info.dxgi_output_rotation = ui32_t(description.Rotation);
                            }
                        }

                        if (auto dwm_output = CComPtr<IDXGIOutputDWM>(); SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&dwm_output)))) {
                            if (auto description = DXGI_OUTPUT_DWM_DESC(); SUCCEEDED(dwm_output->GetDesc(&description))) {
                                source_vid_pn_id = description.base.sourceVidPnId;
                                target_vid_pn_id = description.base.targetVidPnId;

                                auto selected_rect = ncore::get_system_version().build >= 22000 ?
                                    &description.win11.desktopRect :
                                    &description.win10.desktopRect;

                                memcpy(&_info.output_display.rect, selected_rect, sizeof(rect_t));
                            }

                            if constexpr (__diagnosticFeatures) {
                                constexpr auto size = 0x300;
                                auto description = memset(malloc(size), 0xcc, size);
                                auto result = dwm_output->GetDesc((DXGI_OUTPUT_DWM_DESC*)description);
                                auto encoded = ncore::base64::encode(description, size);

                                conlog(
                                    "[w] " function_name ": diagnostic description info:\n"
                                    "    output target: %#llx\n"
                                    "    result:        %08x\n"
                                    "    store buffer:  %#llx\n"
                                    "    encoded:       %s\n",
                                    output,
                                    result,
                                    description,
                                    encoded.c_str());
                            }
                        }

                        get_output_display_info(
                            _info,
                            monitor,
                            _info.adapter.id,
                            source_vid_pn_id,
                            target_vid_pn_id);
                    }
                }
            }

            return true;
        }


        __forceinline private_data_t*& get_private_data(context_t::environment_t* info) noexcept {
            struct accessor_t : public context_t::environment_t {
                __forceinline static void** get(context_t::environment_t* info) noexcept {
                    return &(((accessor_t*)info)->_private_data);
                }
            };

            return *(private_data_t**)accessor_t::get(info);
        }

        __forceinline private_data_t*& get_private_data(context_t::environment_t& info) noexcept {
            return get_private_data(&info);
        }

        __forceinline constexpr void get_rotation_uv(DXGI_MODE_ROTATION rotation, float uv[4][2]) noexcept {
            switch (rotation) {
            default:
                // TL, TR, BL, BR
                uv[0][0] = 0.0f; uv[0][1] = 0.0f;
                uv[1][0] = 1.0f; uv[1][1] = 0.0f;
                uv[2][0] = 0.0f; uv[2][1] = 1.0f;
                uv[3][0] = 1.0f; uv[3][1] = 1.0f;
                break;

            case DXGI_MODE_ROTATION_ROTATE90:
                uv[0][0] = 1.0f; uv[0][1] = 0.0f;
                uv[1][0] = 1.0f; uv[1][1] = 1.0f;
                uv[2][0] = 0.0f; uv[2][1] = 0.0f;
                uv[3][0] = 0.0f; uv[3][1] = 1.0f;
                break;

            case DXGI_MODE_ROTATION_ROTATE180:
                uv[0][0] = 1.0f; uv[0][1] = 1.0f;
                uv[1][0] = 0.0f; uv[1][1] = 1.0f;
                uv[2][0] = 1.0f; uv[2][1] = 0.0f;
                uv[3][0] = 0.0f; uv[3][1] = 0.0f;
                break;

            case DXGI_MODE_ROTATION_ROTATE270:
                uv[0][0] = 0.0f; uv[0][1] = 1.0f;
                uv[1][0] = 0.0f; uv[1][1] = 0.0f;
                uv[2][0] = 1.0f; uv[2][1] = 1.0f;
                uv[3][0] = 1.0f; uv[3][1] = 0.0f;
                break;
            }
        }

        __forceinline bool update_quad_uv(ID3D11DeviceContext* context, ID3D11Buffer* vertex_buffer, DXGI_MODE_ROTATION rotation) noexcept {
            float uv[4][2] = { };
            get_rotation_uv(rotation, uv);

            composite_vertex_t vertices[4] = {
                { { -1.0f,  1.0f }, { uv[0][0], uv[0][1] } },
                { {  1.0f,  1.0f }, { uv[1][0], uv[1][1] } },
                { { -1.0f, -1.0f }, { uv[2][0], uv[2][1] } },
                { {  1.0f, -1.0f }, { uv[3][0], uv[3][1] } },
            };

            D3D11_MAPPED_SUBRESOURCE mapped = { };
            if (FAILED(context->Map(vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;

            memcpy(mapped.pData, vertices, sizeof(vertices));
            context->Unmap(vertex_buffer, 0);

            return true;
        }

        __forceinline void get_offscreen_size(const chain_info_t& info, DXGI_MODE_ROTATION rotation, ui32_t& _width, ui32_t& _height) noexcept {
            if (rotation == DXGI_MODE_ROTATION_ROTATE90 || rotation == DXGI_MODE_ROTATION_ROTATE270) {
                _width = info.buffer.height;
                _height = info.buffer.width;
            }
            else {
                _width = info.buffer.width;
                _height = info.buffer.height;
            }
        }

        __forceinline void cleanup_view(ID3D11DeviceContext* context, const offscreen_view_t& view) noexcept {
            if (!context || !view.render_target_view || view.width == 0 || view.height == 0) return;

            auto target = view.render_target_view;
            context->OMSetRenderTargets(1, &target, nullptr);

            auto viewport = D3D11_VIEWPORT{
                .TopLeftX = 0.0f,
                .TopLeftY = 0.0f,
                .Width = float(view.width),
                .Height = float(view.height),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f
            };
            context->RSSetViewports(1, &viewport);

            const float color[] = { 0.0f, 0.0f, 0.0f, 0.0f }; //transparent
            context->ClearRenderTargetView(view.render_target_view, color);
        }

        __forceinline composite_state_backup_t get_composite_state(ID3D11DeviceContext* context) noexcept {
            auto result = composite_state_backup_t();

            context->IAGetInputLayout(&result.input_layout);
            context->IAGetVertexBuffers(0, 1, &result.vertex_buffer, &result.vertex_stride, &result.vertex_offset);
            context->IAGetIndexBuffer(&result.index_buffer, &result.index_format, &result.index_offset);
            context->IAGetPrimitiveTopology(&result.topology);

            context->VSGetShader(&result.vertex_shader, nullptr, nullptr);
            context->PSGetShader(&result.pixel_shader, nullptr, nullptr);
            context->GSGetShader(&result.geometry_shader, nullptr, nullptr);
            context->HSGetShader(&result.hull_shader, nullptr, nullptr);
            context->DSGetShader(&result.domain_shader, nullptr, nullptr);

            context->PSGetShaderResources(0, 1, &result.pixel_shader_shader_resource_view);
            context->PSGetSamplers(0, 1, &result.pixel_shader_sampler);

            context->OMGetBlendState(&result.blend, result.blend_factor, &result.sample_mask);
            context->OMGetDepthStencilState(&result.depth_stencil, &result.stencil_ref);

            context->RSGetState(&result.rasterizer);
            context->RSGetViewports(&result.viewport_count, result.viewports);
            context->RSGetScissorRects(&result.scissor_count, result.scissor_rects);

            return result;
        }

        __forceinline void set_composite_state(ID3D11DeviceContext* context, composite_state_backup_t& state) noexcept {
            context->IASetInputLayout(state.input_layout);
            context->IASetVertexBuffers(0, 1, &state.vertex_buffer, &state.vertex_stride, &state.vertex_offset);
            context->IASetIndexBuffer(state.index_buffer, state.index_format, state.index_offset);
            context->IASetPrimitiveTopology(state.topology);

            context->VSSetShader(state.vertex_shader, nullptr, 0);
            context->PSSetShader(state.pixel_shader, nullptr, 0);
            context->GSSetShader(state.geometry_shader, nullptr, 0);
            context->HSSetShader(state.hull_shader, nullptr, 0);
            context->DSSetShader(state.domain_shader, nullptr, 0);

            context->PSSetShaderResources(0, 1, &state.pixel_shader_shader_resource_view);
            context->PSSetSamplers(0, 1, &state.pixel_shader_sampler);

            context->OMSetBlendState(state.blend, state.blend_factor, state.sample_mask);
            context->OMSetDepthStencilState(state.depth_stencil, state.stencil_ref);

            context->RSSetState(state.rasterizer);
            context->RSSetViewports(state.viewport_count, state.viewports);
            context->RSSetScissorRects(state.scissor_count, state.scissor_rects);

            release_dxany(state.input_layout);
            release_dxany(state.vertex_buffer);
            release_dxany(state.index_buffer);
            release_dxany(state.vertex_shader);
            release_dxany(state.pixel_shader);
            release_dxany(state.geometry_shader);
            release_dxany(state.hull_shader);
            release_dxany(state.domain_shader);
            release_dxany(state.pixel_shader_shader_resource_view);
            release_dxany(state.pixel_shader_sampler);
            release_dxany(state.blend);
            release_dxany(state.depth_stencil);
            release_dxany(state.rasterizer);
        }

        status_t create_offscreen_texture(ID3D11Device* device, ui32_t width, ui32_t height, DXGI_FORMAT format, offscreen_view_t& _view) noexcept {
            if (!device || width == 0 || height == 0 || format == DXGI_FORMAT_UNKNOWN) return status_t::invalid_argument_passed;

            _view.release();

            auto description = D3D11_TEXTURE2D_DESC{
                .Width = width,
                .Height = height,
                .MipLevels = 1,
                .ArraySize = 1,
                .Format = format,
                .SampleDesc = {
                    .Count = 1,
                    .Quality = 0
                },
                .Usage = D3D11_USAGE_DEFAULT,
                .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                .CPUAccessFlags = 0,
                .MiscFlags = 0
            };

            auto status = status_t();

            auto texture = (ID3D11Texture2D*)nullptr;
            if (FAILED(device->CreateTexture2D(&description, nullptr, &texture)) || !texture) {
                status = status_t::cant_create_texture;

            _Exit:
                return status;
            }

            auto render_target_view = (ID3D11RenderTargetView*)nullptr;
            if (FAILED(device->CreateRenderTargetView(texture, nullptr, &render_target_view)) || !render_target_view) {
                status = status_t::cant_create_render_target_view;

            _ReleaseTextureExit:
                texture->Release();

                goto _Exit;
            }

            auto shader_resource_view = (ID3D11ShaderResourceView*)nullptr;
            if (FAILED(device->CreateShaderResourceView(texture, nullptr, &shader_resource_view)) || !shader_resource_view) {
                status = status_t::cant_create_shader_resource_view;

                render_target_view->Release();
                
                goto _ReleaseTextureExit;
            }

            _view.texture = texture;
            _view.render_target_view = render_target_view;
            _view.shader_resource_view = shader_resource_view;
            _view.width = width;
            _view.height = height;
            _view.format = format;

            return status_t::success;
        }

        status_t create_composite_quad(ID3D11Device* device, composite_pipeline_t& pipeline) noexcept {
            composite_vertex_t vertices[4] = {
                { { -1.0f,  1.0f }, { 0.0f, 0.0f } }, // TL
                { {  1.0f,  1.0f }, { 1.0f, 0.0f } }, // TR
                { { -1.0f, -1.0f }, { 0.0f, 1.0f } }, // BL
                { {  1.0f, -1.0f }, { 1.0f, 1.0f } }, // BR
            };

            const ui16_t indices[6] = {
                0, 1, 2,
                2, 1, 3
            };

            auto vertex_buffer_description = D3D11_BUFFER_DESC{
                .ByteWidth = sizeof(vertices),
                .Usage = D3D11_USAGE_DYNAMIC,
                .BindFlags = D3D11_BIND_VERTEX_BUFFER,
                .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE
            };

            auto vertex_buffer_data = D3D11_SUBRESOURCE_DATA{
                .pSysMem = vertices
            };

            if (FAILED(device->CreateBuffer(
                &vertex_buffer_description, 
                &vertex_buffer_data, 
                &pipeline.vertex_buffer))) return status_t::cant_create_vertex_buffer;

            auto index_buffer_description = D3D11_BUFFER_DESC{
                .ByteWidth = sizeof(indices),
                .Usage = D3D11_USAGE_IMMUTABLE,
                .BindFlags = D3D11_BIND_INDEX_BUFFER
            };

            auto index_buffer_data = D3D11_SUBRESOURCE_DATA{
                .pSysMem = indices
            };

            if (FAILED(device->CreateBuffer(
                &index_buffer_description, 
                &index_buffer_data, 
                &pipeline.index_buffer))) return status_t::cant_create_index_buffer;

            return status_t::success;
        }

        __forceinline bool create_composite_blend_state(ID3D11Device* device, ID3D11BlendState*& _blend) noexcept {
            auto description = D3D11_BLEND_DESC{
                .RenderTarget = {
                    {
                        .BlendEnable = TRUE,
                        .SrcBlend = D3D11_BLEND_ONE,
                        .DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
                        .BlendOp = D3D11_BLEND_OP_ADD,
                        .SrcBlendAlpha = D3D11_BLEND_ONE,
                        .DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
                        .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                        .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL
                    }
                }
            };

            return SUCCEEDED(device->CreateBlendState(&description, &_blend)) && _blend;
        }

        status_t create_composite_pipeline(
            ID3D11Device* device, 
            ID3DBlob* _vertex_shader,
            ID3DBlob* _pixel_shader,
            composite_pipeline_t& _pipeline) noexcept {
            if (!device || !_vertex_shader || !_pixel_shader) return status_t::invalid_argument_passed;

            _pipeline.release();

            auto status = status_t();

            if (FAILED(device->CreateVertexShader(
                rotation_shader_blob_vertex->GetBufferPointer(),
                rotation_shader_blob_vertex->GetBufferSize(),
                nullptr,
                &_pipeline.vertex_shader)) || !_pipeline.vertex_shader) {
                status = status_t::cant_create_vertex_shader;

            _Fail:
                _pipeline.release();

            _Exit:
                return status;
            }

            if (FAILED(device->CreatePixelShader(
                rotation_shader_blob_pixel->GetBufferPointer(),
                rotation_shader_blob_pixel->GetBufferSize(),
                nullptr,
                &_pipeline.pixel_shader)) || !_pipeline.pixel_shader) {
                status = status_t::cant_create_pixel_shader;

                goto _Fail;
            }

            D3D11_INPUT_ELEMENT_DESC input_elements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(float) * 2, D3D11_INPUT_PER_VERTEX_DATA, 0 }
            };

            if (FAILED(device->CreateInputLayout(
                input_elements,
                ARRAYSIZE(input_elements),
                rotation_shader_blob_vertex->GetBufferPointer(),
                rotation_shader_blob_vertex->GetBufferSize(),
                &_pipeline.input_layout)) || !_pipeline.input_layout) {
                status = status_t::cant_create_shader_layout;

                goto _Fail;
            }

            auto sampler_description = D3D11_SAMPLER_DESC{
                .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
                .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                .ComparisonFunc = D3D11_COMPARISON_ALWAYS,
                .MinLOD = 0.0f,
                .MaxLOD = D3D11_FLOAT32_MAX
            };

            if (FAILED(device->CreateSamplerState(&sampler_description, &_pipeline.sampler)) || !_pipeline.sampler) {
                status = status_t::cant_create_sampler;

                goto _Fail;
            }

            if (!create_composite_blend_state(device, _pipeline.blend)) {
                status = status_t::cant_create_composite_blend;

                goto _Fail;
            }

            auto rasterizer_description = D3D11_RASTERIZER_DESC{
                .FillMode = D3D11_FILL_SOLID,
                .CullMode = D3D11_CULL_NONE,
                .DepthClipEnable = FALSE,
                .ScissorEnable = FALSE
            };

            if (FAILED(device->CreateRasterizerState(&rasterizer_description, &_pipeline.rasterizer)) || !_pipeline.rasterizer) {
                status = status_t::cant_create_rasterizer;

                goto _Fail;
            }

            auto depth_description = D3D11_DEPTH_STENCIL_DESC{
                .DepthEnable = FALSE,
                .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
                .DepthFunc = D3D11_COMPARISON_ALWAYS,
                .StencilEnable = FALSE
            };

            if (FAILED(device->CreateDepthStencilState(&depth_description, &_pipeline.depth_stencil)) || !_pipeline.depth_stencil) {
                status = status_t::cant_create_depth_stencil;

                goto _Fail;
            }

            if (status = create_composite_quad(device, _pipeline)) goto _Fail;

            status = status_t::success;

            goto _Exit;
        }

        status_t render_to_final(
            ID3D11DeviceContext* context,
            const offscreen_view_t& source,
            const final_view_t& final,
            composite_pipeline_t& pipeline) noexcept {
            if (!context ||
                !source.shader_resource_view ||
                !final.render_target_view ||
                !pipeline.vertex_buffer ||
                !pipeline.index_buffer ||
                !pipeline.vertex_shader ||
                !pipeline.pixel_shader ||
                !pipeline.input_layout ||
                !pipeline.sampler ||
                !pipeline.blend ||
                !pipeline.rasterizer ||
                !pipeline.depth_stencil) return status_t::invalid_argument_passed;

            if (!update_quad_uv(context, pipeline.vertex_buffer, final.rotation)) return status_t::cant_update_quads;

            auto state = get_composite_state(context);

            auto target = final.render_target_view;
            context->OMSetRenderTargets(1, &target, nullptr);

            auto viewport = D3D11_VIEWPORT{
                .TopLeftX = 0.0f,
                .TopLeftY = 0.0f,
                .Width = float(final.width),
                .Height = float(final.height),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f
            };
            context->RSSetViewports(1, &viewport);

            UINT stride = sizeof(composite_vertex_t);
            UINT offset = 0;

            context->IASetInputLayout(pipeline.input_layout);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->IASetVertexBuffers(0, 1, &pipeline.vertex_buffer, &stride, &offset);
            context->IASetIndexBuffer(pipeline.index_buffer, DXGI_FORMAT_R16_UINT, 0);

            context->VSSetShader(pipeline.vertex_shader, nullptr, 0);
            context->PSSetShader(pipeline.pixel_shader, nullptr, 0);
            context->GSSetShader(nullptr, nullptr, 0);
            context->HSSetShader(nullptr, nullptr, 0);
            context->DSSetShader(nullptr, nullptr, 0);

            context->PSSetShaderResources(0, 1, &source.shader_resource_view);
            context->PSSetSamplers(0, 1, &pipeline.sampler);

            const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
            context->OMSetBlendState(pipeline.blend, blend_factor, 0xffffffff);
            context->OMSetDepthStencilState(pipeline.depth_stencil, 0);
            context->RSSetState(pipeline.rasterizer);

            context->DrawIndexed(6, 0, 0);

            auto null_shader_resource_view = (ID3D11ShaderResourceView*)nullptr;
            context->PSSetShaderResources(0, 1, &null_shader_resource_view);

            set_composite_state(context, state);

            return status_t::success;
        }

        status_t compile_rotation_shaders(ID3DBlob*& _vertex, ID3DBlob*& _pixel) noexcept {
            constexpr auto flags = D3DCOMPILE_ENABLE_STRICTNESS;

            auto error_blob = (ID3DBlob*)nullptr;
            auto status = status_t();

            if (FAILED(D3DCompile(
                __hlslRotationShaderVertex, sizeof(__hlslRotationShaderVertex) - 1,
                nullptr, nullptr, nullptr,
                "m", "vs_4_0",
                flags, 0,
                &_vertex, &error_blob)) || !_vertex) {
                status = status_t::vertex_shader_compilation_error;

            _Fail:
                release_dxany(error_blob);

            _Exit:
                return status;
            }

            if (FAILED(D3DCompile(
                __hlslRotationShaderPixel, sizeof(__hlslRotationShaderPixel) - 1,
                nullptr, nullptr, nullptr,
                "m", "ps_4_0",
                flags, 0,
                &_pixel, &error_blob)) || !_pixel) {
                status = status_t::pixel_shader_compilation_error;

                goto _Fail;
            }

            status = status_t::success;

            goto _Exit;
        }
    }


    static working_set* self = nullptr;
    static range<address_t>* dxgi_bounds = nullptr;


    template<typename _t>
    status_t retrieve_render_context(
        _t* source,
        ID3D11Device*& _device,
        ID3D11DeviceContext*& _device_context,
        ID3D11Texture2D*& _texture,
        ID3D11RenderTargetView*& _render_target_view,
        context_t::environment_t::references_info_t& _references_info) noexcept {
        return status_t::unexpected_state;
    }

    //modern
    template<> status_t retrieve_render_context<ID3D11Resource>(
        ID3D11Resource* resource,
        ID3D11Device*& _device,
        ID3D11DeviceContext*& _device_context,
        ID3D11Texture2D*& _texture,
        ID3D11RenderTargetView*& _render_target_view,
        context_t::environment_t::references_info_t& _references_info) noexcept {
        auto type = D3D11_RESOURCE_DIMENSION();
        auto result = status_t(status_t::error);

        if (resource->GetType(&type), type != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        _ResourceFail:
            result = status_t::cant_get_resource;

        _Fail:
            return result;
        }

        if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&_texture))) || !_texture) goto _ResourceFail;

        if (resource->GetDevice(&_device); !_device) {
            result = status_t::cant_get_device;

        _FailReleaseTexture:
            _texture->Release();

            goto _Fail;
        }

        if (_device->GetImmediateContext(&_device_context); !_device_context) {
            result = status_t::cant_get_device_context;

        _FailReleaseDevice:
            _device->Release();

            goto _FailReleaseTexture;
        }

        if (FAILED(_device->CreateRenderTargetView(_texture, nullptr, &_render_target_view)) || !_render_target_view) {
            result = status_t::cant_get_target_view;

        _FailReleaseDeviceContext:
            _device_context->Release();

            goto _FailReleaseDevice;
        }

        _references_info.was_referenced = {
            .device = true,
            .device_context = true,
            .texture = true,
            .render_target_view = true
        };

        return status_t::success;
    }

    //legacy
    template<> status_t retrieve_render_context<IDXGISwapChainDWMLegacy>(
        IDXGISwapChainDWMLegacy* chain,
        ID3D11Device*& _device,
        ID3D11DeviceContext*& _device_context,
        ID3D11Texture2D*& _texture,
        ID3D11RenderTargetView*& _render_target_view,
        context_t::environment_t::references_info_t& _references_info) noexcept {
        ID3D11Resource* resource = nullptr;

        if (FAILED(chain->GetBuffer(NULL, IID_PPV_ARGS(&resource))) || !resource) return status_t::cant_get_resource;

        auto result = retrieve_render_context<typeofrn(resource)>(
            resource,
            _device,
            _device_context,
            _texture,
            _render_target_view,
            _references_info);

        resource->Release();

        return result;
    }

    status_t make_offscreen_layer(
        context_t::environment_t* context, 
        ID3D11Device* device,
        ID3D11DeviceContext* device_context,
        ID3D11Texture2D* texture,
        ID3D11RenderTargetView* render_target_view) noexcept {
        if (!context || !device || !device_context || !texture || !render_target_view) return status_t::invalid_argument_passed;

        auto& info = context->chain_info;

        const auto resources_info = context->resources_info;
        const auto rotation = DXGI_MODE_ROTATION(context->chain_info.dxgi_output_rotation);
        const auto format = DXGI_FORMAT(info.buffer.format);

        auto data = new details::private_data_t();

        data->final_view = {
            .texture = texture,
            .render_target_view = render_target_view,
            .texture_referenced = resources_info.was_referenced.texture,
            .render_target_view_referenced = resources_info.was_referenced.render_target_view,
            .width = info.buffer.width,
            .height = info.buffer.height,
            .format = format,
            .rotation = rotation
        };

        auto width = ui32_t();
        auto height = ui32_t();

        details::get_offscreen_size(info, rotation, width, height);

        auto status = status_t();

        if (status = details::create_offscreen_texture(
            device, 
            width, 
            height, 
            format, 
            data->offscreen_view)) {
        _Fail:
            data->final_view = { };
            data->composite_pipeline.release();
            data->offscreen_view.release();

            delete data;

            return status;
        }

        if (status = details::create_composite_pipeline(
            device,
            details::rotation_shader_blob_vertex,
            details::rotation_shader_blob_pixel,
            data->composite_pipeline)) goto _Fail;

        auto& private_data = details::get_private_data(*context); {
            auto previous = private_data;
            private_data = data;
            if (previous) {
                previous->release();
            }
        }
        
        context->resources_info.was_referenced.texture = false;
        context->resources_info.was_referenced.render_target_view = false;

        context->texture = private_data->offscreen_view.texture;
        context->render_target_view = private_data->offscreen_view.render_target_view;

        info.buffer.width = private_data->offscreen_view.width;
        info.buffer.height = private_data->offscreen_view.height;
        info.buffer.format = private_data->offscreen_view.format;

        conlog(
            "[w] " function_name ": transform view created:\n"
            "    original:  %#llx / %#llx, %dx%d (%#x), rotation: %d [display: %d, dxgi: %d]\n"
            "    offscreen: %#llx / %#llx / %#llx, %dx%d (%#x)\n"
            "    rect:      %d,%d -> %d,%d\n",
            private_data->final_view.texture,
            private_data->final_view.render_target_view,
            private_data->final_view.width, private_data->final_view.height, private_data->final_view.format, private_data->final_view.rotation,
            info.output_display.rotation, info.dxgi_output_rotation,
            private_data->offscreen_view.texture,
            private_data->offscreen_view.render_target_view,
            private_data->offscreen_view.shader_resource_view,
            private_data->offscreen_view.width, private_data->offscreen_view.height, private_data->offscreen_view.format,
            info.output_display.rect.left, info.output_display.rect.top, info.output_display.rect.right, info.output_display.rect.bottom);

        return status_t::success;
    }

    template<typename source_t>
    status_t on_initialize(
        working_set* set,
        source_t* source,
        context_t::environment_t*& context,
        context_t::call_t& call_info) noexcept {
        constexpr auto legacy = std::is_same<source_t, IDXGISwapChainDWMLegacy>::value;

        auto& configuration = set->configuration();
        auto& info = context->chain_info;

        auto reinitialize = context->reinitialize;

        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* device_context = nullptr;
        ID3D11Texture2D* texture = nullptr;
        ID3D11RenderTargetView* render_target_view = nullptr;

        if (reinitialize) {
            context->release_resources();
        }

        if (auto status = retrieve_render_context<source_t>(
            source,
            device,
            device_context,
            texture,
            render_target_view,
            context->resources_info)) return status;

        auto output = details::get_dxgi_dwm_output(
            configuration.offsets.render_target,
            configuration.offsets.dxgi_output_vftable,
            *dxgi_bounds,
            call_info.instance,
            configuration.offsets.dxgi_output);

        details::get_environment_info(
            info,
            device,
            texture,
            output);

        conlog(
            "[w] " function_name " (%#llx) [%c]:\n"
            "    instance:       %#llx\n"
            "    chain:          %#llx\n"
            "    texture:        %#llx\n"
            "    device:         %#llx\n"
            "    device context: %#llx\n"
            "    output:         %#llx (%#lx)\n"
            "    configuration:  %d (%d) | %d\n"
            "    rect:           %d,%d -> %d,%d\n"
            "    monitor:        %#016x | %s | %s\n"
            "    adapter:        %08x:%08x\n"
            "    vid pn id:      %08x:%08x\n"
            "    buffer:         %dx%d (%#x)\n"
            "    internal:       %#llx\n",
            retrieve_render_context<source_t>, legacy ? 'l' : 'm',
            call_info.instance,
            call_info.chain,
            texture,
            device,
            device_context,
            output, configuration.offsets.dxgi_output,
            info.output_display.rotation, info.dxgi_output_rotation, info.output_display.output_technology,
            info.output_display.rect.left, info.output_display.rect.top, info.output_display.rect.right, info.output_display.rect.bottom,
            info.output_display.handle, info.output_display.gdi_name.empty() ? "" : info.output_display.gdi_name.c_str(), info.output_display.monitor_path.empty() ? "" : info.output_display.monitor_path.c_str(),
            info.adapter.id.high, info.adapter.id.low,
            info.output_display.path_id.source, info.output_display.path_id.target,
            info.buffer.width, info.buffer.height, info.buffer.format,
            &info);

        context->output = output;
        context->device = device;
        context->device_context = device_context;
        context->texture = texture;
        context->render_target_view = render_target_view;

        make_offscreen_layer(
            context,
            device,
            device_context,
            texture,
            render_target_view);

        if (reinitialize) {
            context->reinitialize = false;
        }

        return set->initialize(__thread_id, *context, call_info);
    }

    status_t present_callback(working_set* set, void* overlay_swap_chain, context_t::call_t& call_info) noexcept {
        const auto& configuration = set->configuration();

        auto source = (void*)nullptr;
        auto callback = (typeof(on_initialize<void>)*)nullptr;

        if (configuration.offsets.is_modern_available()) {
            auto swap_chain_buffer = details::get_physical_back_buffer(configuration.offsets.get_physical_back_buffer, overlay_swap_chain);
            if (!swap_chain_buffer) return status_t::cant_get_chain_buffer;

            auto resource = details::get_d3d11_resource(configuration.offsets.get_d3d11_resource, swap_chain_buffer);
            if (!resource) return status_t::cant_get_resource;

            source = resource;
            *(void**)&callback = &on_initialize<typeofrn(resource)>;
        }
        else if (configuration.offsets.is_legacy_available()) {
            auto chain = details::retrieve_dxgi_swap_chain(configuration.offsets.dxgi_swap_chain, overlay_swap_chain, dxgi_bounds);
            if (!chain) return status_t::cant_retrieve_dxgi_chain;

            source = chain;
            *(void**)&callback = &on_initialize<typeofrn(chain)>;
        }
        else _FailWrongConfiguration: return status_t::wrong_configuration_target;

        if (!source || !callback) goto _FailWrongConfiguration;

        auto environment = (context_t::environment_t*)nullptr;
        auto first = false;

        if (auto status = set->get_context_environment(source, environment, first)) return status;

        if (!environment) return status_t::chain_environment_not_found;

        if (first || environment->reinitialize) if (auto status = callback(set, source, environment, call_info)) {
            if (first) {
                conlog("[w] " function_name ": initialization failed, status: %d\n", status);
            }

            return status;
        }

        auto device_context = environment->device_context_as<ID3D11DeviceContext>();
        auto previous_render_target_view = CComPtr<ID3D11RenderTargetView>();
        auto previous_depth_stencil_view = CComPtr<ID3D11DepthStencilView>();
        auto result = status_t();

        device_context->OMGetRenderTargets(1, &previous_render_target_view, &previous_depth_stencil_view);

        if (auto private_data = details::get_private_data(*environment)) {
            details::cleanup_view(device_context, private_data->offscreen_view);

            result = set->render(*environment, call_info);

            if (result == status_t::success) {
                result = details::render_to_final(
                    device_context,
                    private_data->offscreen_view,
                    private_data->final_view,
                    private_data->composite_pipeline);
            }
        }
        else {
            result = set->render(*environment, call_info);
        }

        device_context->OMSetRenderTargets(1, &previous_render_target_view, previous_depth_stencil_view);

        return result;
    }


    //win10-19045-6466 22h2
    ui64_t __fastcall wrapped_present_win10(
        void* instance,             // COverlayContext*
        void* overlay_swap_chain,   // IOverlaySwapChain*
        ui32_t a3,                  // uint
        const std::vector<rect_t, std::allocator<rect_t>>& dirty_rects, // std::vector<tagRECT,std::allocator<tagRECT>> const &,
        ui32_t a5,                  // uint,
        bool legacy_present         // bool
    ) noexcept {
        //1800EC048 COverlayContext::Present(
        // COverlayContext*, 
        // IOverlaySwapChain*,
        // uint,
        // std::vector<tagRECT,std::allocator<tagRECT>> const &,
        // uint,
        // bool)

        auto self = wdwmo::self;
        auto original = self->get_original_present<typeof(wrapped_present_win10)*>();

        auto call_info = context_t::call_t{
            .instance = instance,
            .chain = overlay_swap_chain,
            .legacy_present = legacy_present,
            .dirty_rects = {
                .original = dirty_rects,
                .custom = { },
                .changed = false
            }
        };

        if (self->can_proceed()) {
            self->on_present(overlay_swap_chain, call_info);
        }

        auto result = original(
            instance, 
            overlay_swap_chain, 
            a3, 
            call_info.dirty_rects.changed ? call_info.dirty_rects.custom : call_info.dirty_rects.original,
            a5, 
            legacy_present);

        return result;
    }

    //win11-22600-8457 25h2
    ui64_t __fastcall wrapped_present_win11(
        void* instance,             // COverlayContext*
        void* overlay_swap_chain,   // IOverlaySwapChain*
        ui32_t a3,                  // uint
        const std::vector<rect_t, std::allocator<rect_t>>& dirty_rects, // std::vector<tagRECT,std::allocator<tagRECT>> const &,
        ui32_t a5,                  // uint,
        bool* a6,                   // bool*
        bool legacy_present         // bool
    ) noexcept {
        //180231000	COverlayContext::Present(
        // COverlayContext*,
        // IOverlaySwapChain*,
        // uint,
        // std::vector<tagRECT,std::allocator<tagRECT>> const &,
        // uint,
        // bool*,
        // bool)

        auto self = wdwmo::self;

        auto original = self->get_original_present<typeof(wrapped_present_win11)*>();

        auto call_info = context_t::call_t{
            .instance = instance,
            .chain = overlay_swap_chain,
            .legacy_present = legacy_present,
            .dirty_rects = {
                .original = dirty_rects,
                .custom = { },
                .changed = false
            }
        };

        if (self->can_proceed()) {
            self->on_present(overlay_swap_chain, call_info);
        }

        auto result = original(
            instance,
            overlay_swap_chain,
            a3,
            call_info.dirty_rects.changed ? call_info.dirty_rects.custom : call_info.dirty_rects.original,
            a5,
            a6, 
            legacy_present);

        return result;
    }


    status_t terminate_local(const initialization_guard*) {
        conlog("[w] " function_name " (%#llx): called\n", terminate_local);

        if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK) return status_t::hook_disabling_failed;

        if (auto set = wdwmo::self) {
            if (auto registry = set->release()) {
                for (auto& entry : *registry) {
                    delete entry.second;
                }

                registry->clear();

                delete registry;
            }
        }

        return status_t::success;
    }

    void context_t::environment_t::release_resources() noexcept {
        if(auto& private_data = details::get_private_data(*this)) {
            private_data->release();
            private_data = nullptr;
        }

        if (auto instance = device_as<IUnknown>(); instance && resources_info.was_referenced.device) {
            instance->Release();
        }
        device = nullptr;

        if (auto instance = device_context_as<IUnknown>(); instance && resources_info.was_referenced.device_context) {
            instance->Release();
        }
        device_context = nullptr;

        if (auto instance = texture_as<IUnknown>(); instance && resources_info.was_referenced.texture) {
            instance->Release();
        }
        texture = nullptr;

        if (auto instance = render_target_view_as<IUnknown>(); instance && resources_info.was_referenced.render_target_view) {
            instance->Release();
        }
        render_target_view = nullptr;

        output = nullptr;

        resources_info = { };
    }

    void working_set::reset(present_callback_t present_callback, initialization_callback_t initialization_callback, render_callback_t render_callback) noexcept {
        conlog(
            "[w] " function_name ": callbacks:\n"
            "    present:        %#llx -> %#llx\n"
            "    initialization: %#llx -> %#llx\n"
            "    render:         %#llx -> %#llx\n",
            _present_callback, present_callback,
            _initialization_callback, initialization_callback,
            _render_callback, render_callback);

        _response = false;
        _active = false;

        for (int i = 0, l = 2500, s = 5; i < l && !_response; i += s) {
            ncore::thread::sleep(s);
        }

        _initialization_callback = initialization_callback;

        if (_context_registry) {
            for (auto& context : *_context_registry) {
                if (context.second) context.second->reinitialize = true;
            }
        }

        _render_callback = render_callback;
        _present_callback = present_callback;

        _active = true;
    }

    working_set* get_current_working_set() noexcept {
        return self;
    }

    working_set* set_current_working_set(working_set* set) noexcept {
        auto previous = self;

        self = set;

        return previous;
    }

    ui64_t get_system_build_support_status(
        ui32_t build,
        ui64_t unknown,
        ui64_t not_supported,
        ui64_t supported,
        ui64_t supported_with_troubles) noexcept {
        constexpr ui32_t build_win10_22h2 = 19045; //supported
        constexpr ui32_t build_win11_21h2 = 22000; //probably supported
        constexpr ui32_t build_win11_22h2 = 22621; //probably supported
        constexpr ui32_t build_win11_23h2 = 22631; //
        constexpr ui32_t build_win11_24h2 = 26100; //supported
        constexpr ui32_t build_win11_25h2 = 26200; //supported
        constexpr ui32_t build_win11_26h1 = 28000; //

        switch (build) {
        default: break;

        case build_win10_22h2:
        case build_win11_24h2:
        case build_win11_25h2:
            return supported;
        }

        return unknown;
    }

    status_t was_initialized_before(initialization_guard* guard, bool& _result) noexcept {
        if (!guard) return status_t::guard_not_set;

        _result = guard->get_or_create_storage()->existing_set_info.instance != nullptr;

        return status_t::success;
    }

    status_t::extended_t validate_configuration(
        const configuration_t& configuration,
        ui64_t dwmcore_module_identifier,
        ui64_t dxgi_module_identifier) noexcept {
        auto process = ncore::process::current(null);
        if (!process.alive()) return { status_t::cant_access_current_process };

        auto check_module = [](ncore::process& process, const char* name, const executable_image_info_t& image) -> status_t {
            auto module = ncore::process::module_t();
            if (!process.search_module(name, &module)) return status_t::core_module_not_found;

            auto info = executable_image_info_t();
            if (!utils::get_pe_info(module.address(), module.size(), info)) return status_t::cant_get_pe_info;

            if (image.timestamp != info.timestamp) return status_t::wrong_image_timestamp;
            if (image.size != info.size) return status_t::wrong_image_size;
            if (image.checksum != info.checksum) return status_t::wrong_image_checksum;

            return status_t::success;
        };

        if (auto status = check_module(process, "dwmcore.dll", configuration.image_info.dwmcore)) return { status.value, dwmcore_module_identifier };

        if (auto status = check_module(process, "dxgi.dll", configuration.image_info.dxgi)) return { status.value, dxgi_module_identifier };

        return { status_t::success };
    }

    status_t initialize(
        const configuration_t& configuration,
        initialization_callback_t initialization_callback,
        render_callback_t render_callback,
        initialization_guard* guard,
        bool place_hooks, // ignored if first initialization or guard is not set
        initialization_guard::termination_callback_t termination_callback) noexcept {
        working_set* self = nullptr;

        conlog(
            "[w] " function_name " (%#llx):\n"
            "    called from:          %#llx\n"
            "    initialization guard: %#llx\n",
            initialize,
            __return_address,
            guard);

        if (guard) {
            auto& exist = guard->get_or_create_storage()->existing_set_info;
            if (exist.instance) {
                conlog(
                    "[w] " function_name ": previous set info found:\n"
                    "    target:   %#llx\n"
                    "    instance: %#llx\n",
                    &exist,
                    exist.instance);

                if (auto termination_callback = exist.termination_callback; termination_callback && ncore::can_access(termination_callback)) {
                    termination_callback(guard);
                }

                conlog(
                    "[w] " function_name ": original addresses from storage:\n"
                    "    present: %#llx\n",
                    exist.instance->get_original_present<address_t>());

                self = exist.instance;
            }
        }

        auto dwm_process = ncore::process::current(null);
        if (!dwm_process.alive()) return status_t::cant_access_current_process;

        auto dwmcore_module = ncore::process::module_t();
        auto dxgi_module = ncore::process::module_t();
        if (!dwm_process.search_module("dwmcore.dll", &dwmcore_module) || !dwm_process.search_module("dxgi.dll", &dxgi_module)) return status_t::core_module_not_found;

        auto dwmcore_info = executable_image_info_t();
        auto dxgi_info = executable_image_info_t();
        if (!utils::get_pe_info(dwmcore_module.address(), dwmcore_module.size(), dwmcore_info) || !utils::get_pe_info(dxgi_module.address(), dxgi_module.size(), dxgi_info)) return status_t::cant_get_pe_info;

        dxgi_bounds = new range<address_t>(dxgi_module.address(), byte_p(dxgi_module.address()) + dxgi_module.size());

        if (auto status = details::compile_rotation_shaders(details::rotation_shader_blob_vertex, details::rotation_shader_blob_pixel)) {
            conlog("[w] " function_name " [!] rotation shaders isn't compiled, rotation modes unsupported, status %d\n", status);
        }
        else {
            conlog("[w] " function_name " rotation shaders compiled successfully\n");
        }

        conlog(
            "[w] " function_name ": core modules:\n"
            "    dwmcore.dll: %#llx - %#llx (%#llx)\n"
            "    dxgi.dll:    %#llx - %#llx (%#llx)\n",
            dwmcore_module.address(), byte_p(dwmcore_module.address()) + dwmcore_module.size(), dwmcore_module.size(),
            dxgi_module.address(), byte_p(dxgi_module.address()) + dxgi_module.size(), dxgi_module.size());

        if (self) {
            conlog("[w] " function_name ": reseting previous instance callbacks\n");

            wdwmo::self = self;

            self->reset(present_callback, initialization_callback, render_callback);
        }
        else {
            auto get_build_compatible_present_wrapper = [](ui32_t build) noexcept -> address_t {
                if (build <= 19045) return wrapped_present_win10;

                if (build >= 22000) return wrapped_present_win11;

                return nullptr;
            };

            if ((configuration.image_info.dwmcore && configuration.image_info.dwmcore != dwmcore_info) || 
                (configuration.image_info.dxgi && configuration.image_info.dxgi != dxgi_info)) return status_t::wrong_configuration_target;

            auto system_build = ncore::get_system_version().build;

            auto present_target = address_t(dwmcore_module.address<ui64_t>() + configuration.offsets.present);
            auto present_wrapper = get_build_compatible_present_wrapper(system_build);
            if (!present_wrapper) return status_t::unexpected_system_build;

            auto present_original = address_t();

            if (MH_Initialize() != MH_OK) return status_t::hook_initialization_failure;

            if (MH_CreateHook(present_target, present_wrapper, &present_original) != MH_OK) return status_t::hook_creating_failed;

            self = new working_set(
                configuration,
                present_callback,
                initialization_callback,
                render_callback,
                present_original);

            conlog(
                "[w] " function_name ": new set created - %#llx, callbacks:\n"
                "    initialization:  %#llx\n"
                "    render:          %#llx\n"
                "    present target:  %#llx\n"
                "    present jumpout: %#llx\n",
                self,
                initialization_callback,
                render_callback,
                present_target,
                present_original);

            wdwmo::self = self;

            place_hooks = true;
        }

        if (place_hooks) {
            conlog("[w] " function_name ": enabling hooks\n");

            if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) return status_t::hook_enabling_failed;

            if (guard) {
                guard->get_or_create_storage()->shared_termination_callback = terminate_local;
                conlog("[w] " function_name ": shared termination callback %#llx set for guard %#llx\n", terminate_local, guard);
            }
        }

        if (guard) {
            auto& exist = guard->get_or_create_storage()->existing_set_info;

            exist = {
                .instance = self,
                .termination_callback = termination_callback
            };

            guard->commit_changes();
        }

        return status_t::success;
    }

    status_t terminate(initialization_guard* guard) noexcept {
        working_set* self = nullptr;

        conlog(
            "[w] " function_name " (%#llx):\n"
            "    called from:          %#llx\n"
            "    initialization guard: %#llx\n",
            terminate,
            __return_address,
            guard);

        if (guard) {
            auto& exist = guard->get_or_create_storage()->existing_set_info;
            if (!exist.instance) goto _NotInitializedReturn;

            if (auto termination_callback = exist.termination_callback; termination_callback && ncore::can_access(termination_callback)) {
                termination_callback(guard);
            }

            self = exist.instance;
        }

        if (!self) {
            self = wdwmo::self;
        }

        if (!self) _NotInitializedReturn: return status_t::not_initialized;

        auto terminated = false;
        auto result = status_t();

        if (guard) {
            auto storage = guard->get_or_create_storage();

            if (auto termination_callback = storage->shared_termination_callback; termination_callback && ncore::can_access(termination_callback)) {
                result = termination_callback(guard);
                terminated = true;
            }

            delete guard->erase_storage();
        }

        if (terminated) return result;

        return terminate_local(guard);
    }
}
