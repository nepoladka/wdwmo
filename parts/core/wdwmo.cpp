#include "wdwmo.hpp"

#include <windows.h>
#include <atlbase.h>
#include <dxgi1_5.h>
#include <d3d11.h>
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

            //const bool active = 

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

            _info.output_display.active = true; //(path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
            _info.output_display.primary = is_monitor_primary(monitor);

            memcpy(_info.output_display.scale, get_monitor_scale(monitor).array, sizeof(vec2f));

            //.rotation = ui32_t(target.rotation),
            //.output_technology = ui32_t(target.outputTechnology),

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
                        //auto gdi_name = std::wstring();
                        auto source_vid_pn_id = ui32_t();
                        auto target_vid_pn_id = ui32_t();

                        if (auto dxgi_output = CComPtr<IDXGIOutput>(); SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&dxgi_output)))) {
                            if (auto description = DXGI_OUTPUT_DESC(); SUCCEEDED(dxgi_output->GetDesc(&description))) {
                                //gdi_name = description.DeviceName;
                                monitor = description.Monitor;

                                _info.output_display.rotation = ui32_t(description.Rotation);
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

    template<typename _t>
    status_t on_initialize(
        working_set* set,
        _t* source,
        context_t::environment_t*& context,
        context_t::call_t& call_info) noexcept {
        constexpr auto legacy = std::is_same<_t, IDXGISwapChainDWMLegacy>::value;

        if (set->suspended()) {
            set->response(true);

            return status_t::success;
        }

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

        if (auto status = retrieve_render_context<_t>(
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
            "    rect:           %d,%d -> %d,%d\n"
            "    monitor:        %#016x | %s | %s\n"
            "    adapter:        %08x:%08x\n"
            "    vid pn id:      %08x:%08x\n"
            "    buffer:         %dx%d (%#x)\n"
            "    internal:       %#llx\n",
            retrieve_render_context<_t>, legacy ? 'l' : 'm',
            call_info.instance,
            call_info.chain,
            texture,
            device,
            device_context,
            output, configuration.offsets.dxgi_output,
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

        if (reinitialize) {
            context->reinitialize = false;
        }

        return set->initialize(__thread_id, *context, call_info);
    }

    status_t present_callback(working_set* set, void* overlay_swap_chain, context_t::call_t& call_info) {
        if (set->suspended()) {
            set->response(true);

            return status_t::aborted;
        }

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

        if (auto status = set->get_context_environment(source, __thread_id, environment, first)) return status;

        if (!environment) return status_t::chain_environment_not_found;

        if (first || environment->reinitialize) if (auto status = callback(set, source, environment, call_info)) {
            if (first) {
                conlog("[w] " function_name ": initialization failed, status: %d\n", status);
            }

            return status;
        }

        auto device_context = environment->device_context_as<ID3D11DeviceContext>();
        auto previous_render_target_view = (ID3D11RenderTargetView*)nullptr;
        auto previous_depth_stencil_view = (ID3D11DepthStencilView*)nullptr;

        device_context->OMGetRenderTargets(1, &previous_render_target_view, &previous_depth_stencil_view);

        auto result = set->render(*environment, call_info);

        device_context->OMSetRenderTargets(1, &previous_render_target_view, previous_depth_stencil_view);

        if (previous_depth_stencil_view) {
            previous_depth_stencil_view->Release();
        }

        if (previous_render_target_view) {
            previous_render_target_view->Release();
        }

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
            .dirty_rects = dirty_rects,
            .legacy_present = legacy_present
        };

        self->on_present(overlay_swap_chain, call_info);

        auto result = original(
            instance, 
            overlay_swap_chain, 
            a3, 
            call_info.dirty_rects, 
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
            .dirty_rects = dirty_rects,
            .legacy_present = legacy_present
        };

        self->on_present(overlay_swap_chain, call_info);

        auto result = original(
            instance,
            overlay_swap_chain,
            a3, 
            call_info.dirty_rects,
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
        if (auto instance = device_as<IUnknown>(); instance && resources_info.was_referenced.device) {
            instance->Release();
        }

        if (auto instance = device_context_as<IUnknown>(); instance && resources_info.was_referenced.device_context) {
            instance->Release();
        }

        if (auto instance = texture_as<IUnknown>(); instance && resources_info.was_referenced.texture) {
            instance->Release();
        }

        if (auto instance = render_target_view_as<IUnknown>(); instance && resources_info.was_referenced.render_target_view) {
            instance->Release();
        }
    }

    void working_set::reset(present_callback_t present_callback, initialization_callback_t initialization_callback, render_callback_t render_callback) noexcept {
        conlog(
            "[w] %s: callbacks:\n"
            "    present:        %#llx -> %#llx\n"
            "    initialization: %#llx -> %#llx\n"
            "    render:         %#llx -> %#llx\n",
            function_name,
            _present_callback, present_callback,
            _initialization_callback, initialization_callback,
            _render_callback, render_callback);

        _active = false;
        _response = false;

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
