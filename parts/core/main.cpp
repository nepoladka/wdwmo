#if defined(_WINDLL) && defined(DEBUG)

#include "wdwmo.hpp"

#include "../../../ncore/source/environment.hpp"
#include "../../../ncore/source/strings.hpp"
#include "../../../ncore/source/task.hpp"
#include "../../../ncore/source/input.hpp"
#include "../../../ncore/source/utils.hpp"

#include <string>
#include <mutex>

#include <d3d11.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_dx11.h>

#include <binaries/consola.ttf.h>


using namespace ncore::types;


#define function_name "\033[96m" __FUNCTION__ "\033[0m"
#define conlog wdwmo::debug::conlogf


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


class wtoverlay {
public:
    struct {
        using handle_t = HWND;

        std::string name = { };
        handle_t handle = { };
        void* monitor = { };

        vec2i32 position = { }, relative_position = { };
        vec2i32 size = { }, relative_size = { };

        vec2f scale = { };

        bool on_screen = false;
    }target_window = { };


    static vec2i32 make_relative_position(const vec2i32& position, const wdwmo::rect_t& rect, const vec2f& scale) {
        return {
            i32_t(std::floor(float(position.x - rect.left) * scale.x)),
            i32_t(std::floor(float(position.y - rect.top) * scale.y))
        };
    }

    static vec2i32 make_relative_size(const vec2i32& size, const vec2f& scale) {
        return {
            i32_t(std::floor(float(size.x) * scale.x)),
            i32_t(std::floor(float(size.y) * scale.y))
        };
    }


    wtoverlay(const std::string& name) {
        target_window.name = name;
    }

    bool is_target_on_screen(const wdwmo::rect_t& rect, const vec2f& scale, bool full = false) {
        if (!target_window.handle) return false;

        auto window_size = target_window.size;
        auto window_position = target_window.position;

        if (window_size.x <= 0 || window_size.y <= 0) return false;

        const auto screen_left = rect.left;
        const auto screen_top = rect.top;

        const auto window_left = i32_t(std::floor(window_position.x * scale.x));
        const auto window_top = i32_t(std::floor(window_position.y * scale.y));
        const auto window_right = i32_t(std::ceil((window_position.x + window_size.x) * scale.x));
        const auto window_bottom = i32_t(std::ceil((window_position.y + window_size.y) * scale.y));

        return full ?
            (window_left >= screen_left &&
                window_top >= screen_top &&
                window_right <= rect.right &&
                window_bottom <= rect.bottom) :

            (window_right > screen_left &&
                window_left < rect.right &&
                window_bottom > screen_top &&
                window_top < rect.bottom);
    }

    vec2i32 get_relative_position(const wdwmo::rect_t& rect, const vec2f& scale) {
        return make_relative_position(target_window.position, rect, scale);
    }

    vec2i32 get_relative_size(const vec2f& scale) {
        return make_relative_size(target_window.size, scale);
    }

    bool get_window_info(bool update_handle = false) {
        auto result = false;
        auto handle = target_window.handle;

        if (update_handle || !handle) {
            target_window.handle = handle = FindWindowA(nullptr, target_window.name.data());

            if (!handle) {
            _Fail:
                target_window.monitor = { };
                target_window.on_screen = { };
                target_window.relative_position = { };
                target_window.relative_size = { };
                target_window.scale = { };
                target_window.position = { };
                target_window.size = { };

                return result;
            }
        }

        auto borders = RECT();
        ncore::utils::get_window_rectangles(handle, (POINT*)&target_window.position, (SIZE*)&target_window.size, &borders);

        auto scale = target_window.scale = ncore::utils::get_window_scale(handle);

        auto monitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
        if (monitor) {
            auto info = MONITORINFOEXA(); {
                info.cbSize = sizeof(info);
            }

            if (GetMonitorInfoA(monitor, &info)) {
                const auto& rect = *(wdwmo::rect_t*)&info.rcMonitor;

                target_window.on_screen = is_target_on_screen(rect, scale);
                target_window.relative_position = get_relative_position(rect, scale);
                target_window.relative_size = get_relative_size(scale);
            }
            else goto _Fail;
        }
        else goto _Fail;

        target_window.monitor = monitor;

        return true;
    }
};


constexpr bool __inputHooks = true;
constexpr ui32_t __actionMajorKey = VK_F10;
constexpr ui32_t __actionKillProcess = VK_END;
constexpr ui32_t __actionTerminateWdwmo = VK_HOME;

ui32_t input_thread_id = null;
bool should_terminate = false;
wtoverlay* targeted_overlay = nullptr;

byte_t win10_22h2_19045_6466_info[] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,

    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,

    0x48, 0xc0, 0x0e, 0x00, //0xec048
    0x00, 0x00, 0x00, 0x00, //0x0
    0xc8, 0xcb, 0x09, 0x00, //0x9cbc8
    0x00, 0x00, 0x00, 0x00, //0x0
    0xe8, 0xfe, 0xff, 0xff, //0xfffffee8 | -0x118 | -280
    0x00, 0x00, 0x00, 0x00, //0x0
    0x00, 0x00, 0x00, 0x00  //0x0
};

byte_t win11_25h2_26200_8655_info[] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,

    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,

    0x00, 0x18, 0x23, 0x00, //0x231800
    0x00, 0x00, 0x00, 0x00, //0x0
    0xc0, 0xf4, 0x0c, 0x00, //0xcf4c0
    0x00, 0x00, 0x00, 0x00, //0x0
    0x08, 0x01, 0x00, 0x00, //0x108
    0xc0, 0x00, 0x00, 0x00, //0xc0
    0x98, 0x00, 0x00, 0x00  //0x98
};


template<bool _setOutputs = false>
void* create_console() noexcept {
    if (!AllocConsole() && GetLastError() != ERROR_ACCESS_DENIED) return INVALID_HANDLE_VALUE;

    SetConsoleTitleA("wdwmo - dwm.exe");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if constexpr (_setOutputs) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
        freopen_s(&stream, "CONIN$", "r", stdin);
    }

    auto handle = GetStdHandle(STD_OUTPUT_HANDLE);

    auto mode = DWORD();
    GetConsoleMode(handle, &mode);
    SetConsoleMode(handle, mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    return handle;
}


void** guard_storage_provider(const wdwmo::initialization_guard*) noexcept {
    return &__process_environment->TelemetryCoverageHeader;
}

ui64_t get_guard_storage_identifier() noexcept {
    return ui64_t(__process_environment);
}

wdwmo::initialization_guard* get_or_create_shared_guard(ui64_t storage_identifier) noexcept {
    auto temporary = wdwmo::initialization_guard(guard_storage_provider, storage_identifier);
    auto storage = temporary.get_or_create_storage();

    if (auto it = storage->user_pool.find(storage_identifier); it != storage->user_pool.end()) return (wdwmo::initialization_guard*)it->second;

    auto instance = new wdwmo::initialization_guard(guard_storage_provider, storage_identifier);

    storage->user_pool.emplace(storage_identifier, ui64_t(instance));

    return instance;
}


WPARAM translate_mouse_wparam(WPARAM message, const MSLLHOOKSTRUCT* info) noexcept {
    if (info) switch (message) {
    default: break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return MAKEWPARAM(0, HIWORD(info->mouseData));
    }

    return 0;
}

LPARAM translate_keyboard_lparam(WPARAM message, const KBDLLHOOKSTRUCT* info) noexcept {
    if (!info) return 0;

    LPARAM lparam = 1;
    lparam |= LPARAM(info->scanCode & 0xff) << 16;

    if (info->flags & LLKHF_EXTENDED) {
        lparam |= 1u << 24;
    }

    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        lparam |= 1u << 30;
        lparam |= 1u << 31;
    }

    return lparam;
}

LRESULT CALLBACK mouse_callback(int nCode, WPARAM wParam, LPARAM lParam) noexcept {
    const auto info = (MSLLHOOKSTRUCT*)lParam;

    if (ImGui::GetCurrentContext()) {
        auto monitor = MonitorFromPoint(info->pt, MONITOR_DEFAULTTONEAREST);
        auto scale = wdwmo::utils::get_monitor_scale(monitor);
        auto rect = wdwmo::utils::get_monitor_rect(monitor);

        auto position = wtoverlay::make_relative_position(*(vec2i32*)&info->pt, rect, *(vec2f*)&scale);

        const auto wparam = translate_mouse_wparam(wParam, info);
        const auto lparam = MAKELPARAM(position.x, position.y);

        ImGui_ImplWin32_WndProcHandler(
            GetDesktopWindow(),
            UINT(wParam),
            wparam,  //info->flags,
            lparam); //MAKELPARAM(info->pt.x, info->pt.y));
        
        //if (ImGui::GetIO().WantCaptureMouse && wParam != WM_MOUSEMOVE) return -1;
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK keyboard_callback(int nCode, WPARAM wParam, LPARAM lParam) noexcept {
    const auto info = (KBDLLHOOKSTRUCT*)lParam;

    if (ImGui::GetCurrentContext()) {
        const auto wparam = info->vkCode;
        const auto lparam = translate_keyboard_lparam(wParam, info);

        ImGui_ImplWin32_WndProcHandler(
            GetDesktopWindow(),
            UINT(wParam),
            wparam,  //info->vkCode,
            lparam); //info->scanCode);

        //if (ImGui::GetIO().WantCaptureKeyboard) return -1;
    }

    if (info->vkCode == __actionMajorKey) {
        if (ncore::input::is_key_pressed(__actionKillProcess)) {
            ncore::process::current(null).terminate();
        }

        if (ncore::input::is_key_pressed(__actionTerminateWdwmo)) {
            wdwmo::terminate(get_or_create_shared_guard(get_guard_storage_identifier()));
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

wdwmo::status_t initialization_callback(const wdwmo::context_t& context) noexcept {
    conlog("[w] " function_name ": callled for %#llx\n", context.call.chain);

    if (context.info.device && context.info.device_context && !ImGui::GetCurrentContext()) {
        auto imgui_context = ImGui::CreateContext(); {
            ImGui::SetCurrentContext(imgui_context);
        }

        auto win32_result = ImGui_ImplWin32_Init(GetDesktopWindow());
        auto dx11_result = ImGui_ImplDX11_Init(
            context.info.device_as<ID3D11Device>(),
            context.info.device_context_as<ID3D11DeviceContext>());

        //ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)binary_files::consola_ttf, sizeof(binary_files::consola_ttf), 14.0f);

        conlog(
            "[w] " function_name ": imgui:\n"
            "    context:     %#llx\n"
            "    win32 state: %d\n"
            "    dx11 state:  %d\n",
            imgui_context,
            win32_result,
            dx11_result);
    }

    return wdwmo::status_t::success;
}

wdwmo::status_t render_callback(const wdwmo::context_t& context) noexcept {
    if (context.info.device_context && context.info.render_target_view) {
        context.info.device_context_as<ID3D11DeviceContext>()->OMSetRenderTargets(
            1,
            &context.info.render_target_view_as<ID3D11RenderTargetView>(),
            nullptr);
    }

    const auto header_color = ImGui::ColorConvertU32ToFloat4(0xffdfdf3f); //abgr

    auto imgui_context = ImGui::GetCurrentContext();
    auto thread_id = __thread_id;

    auto relative_position = vec2i32();
    auto relative_size = vec2i32();
    auto target_on_screen = false;

    if (imgui_context) {
        ImGui::SetCurrentContext(imgui_context);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        {
            ImGui::SetNextWindowBgAlpha(0.65f);
            ImGui::Begin("wdwmo - imgui", nullptr, ImGuiWindowFlags_AlwaysAutoResize); {
                ImGui::BeginGroup(); {
                    ImGui::BeginGroup(); {
                        ImGui::TextColored(header_color, "internals");

                        ImGui::Text("thread:          %d (%x)", thread_id, thread_id);
                        ImGui::Text("dxgi chain:      %#llx", context.call.chain);
                        ImGui::Text("device:          %#llx", context.info.device);
                        ImGui::Text("device context:  %#llx", context.info.device_context);
                        ImGui::Text("render target:   %#llx", context.info.render_target_view);

                        ImGui::Text("device:  %s\n"
                                "path:   %08x:%08x\n",
                            context.info.chain_info.output_display.gdi_name.c_str(),
                            context.info.chain_info.output_display.path_id.source, context.info.chain_info.output_display.path_id.target);

                        ImGui::Text("monitor: %s\n"
                                "status: %s, %s\n", 
                            context.info.chain_info.output_display.friendly_name.empty() ? "default" : context.info.chain_info.output_display.friendly_name.c_str(),
                            context.info.chain_info.output_display.handle ? "attached" : "detached", context.info.chain_info.output_display.primary? "primary" : "secondary");
                        
                        ImGui::BeginDisabled(not context.info.chain_info.output_display.handle); {
                            ImGui::Text(
                                "handle: %#llx\n"
                                "pos:    %d, %d\n"
                                "size:   %dx%d\n"
                                "scale:  %.2f, %.2f\n",
                                context.info.chain_info.output_display.handle,
                                context.info.chain_info.output_display.rect.left, context.info.chain_info.output_display.rect.top,
                                context.info.chain_info.output_display.rect.right - context.info.chain_info.output_display.rect.left, context.info.chain_info.output_display.rect.bottom - context.info.chain_info.output_display.rect.top,
                                context.info.chain_info.output_display.scale[0], context.info.chain_info.output_display.scale[1]);
                        } ImGui::EndDisabled();

                        ImGui::Text(
                            "buffer: \n"
                            "format: %x\n"
                            "size:   %dx%d\n",
                            context.info.chain_info.buffer.format,
                            context.info.chain_info.buffer.width, context.info.chain_info.buffer.height);
                    } ImGui::EndGroup();

                    ImGui::SameLine();

                    ImGui::BeginGroup(); {
                        ImGui::TextColored(header_color, "imgui");

                        ImGui::Text("context:      %#llx", imgui_context);
                        ImGui::Text("framerate:    %.1f", ImGui::GetIO().Framerate);

                        ImGui::Text("display size: %.0fx%.0f\n",
                            ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

                        ImGui::Text("window: \n"
                            "pos:       %.0f, %.0f\n"
                            "size:      %.0fx%.0f\n",
                            ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                            ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);

                        ImGui::Text("viewport: \n"
                            "pos:       %.0f, %.0f\n"
                            "size:      %.0fx%.0f\n",
                            ImGui::GetMainViewport()->Pos.x, ImGui::GetMainViewport()->Pos.y,
                            ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y);

                        ImGui::Text(
                            "mouse pos: %.0f, %.0f\n",
                            ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);

                        ImGui::Text("want capture: \n"
                            "keyboard:  %s\n"
                            "mouse:     %s\n"
                            "text:      %s\n",
                            ImGui::GetIO().WantCaptureKeyboard ? "true" : "false",
                            ImGui::GetIO().WantCaptureMouse ? "true" : "false",
                            ImGui::GetIO().WantTextInput ? "true" : "false");
                    } ImGui::EndGroup();
                } ImGui::EndGroup();

                if (auto wto = targeted_overlay) {
                    relative_position = wto->get_relative_position(context.info.chain_info.output_display.rect, context.info.chain_info.output_display.scale);
                    relative_size = wto->get_relative_size(context.info.chain_info.output_display.scale);

                    if (context.info.chain_info.output_display.handle) {
                        target_on_screen = wto->is_target_on_screen(context.info.chain_info.output_display.rect, context.info.chain_info.output_display.scale, false);
                    }
                    else {
                        target_on_screen = wto->target_window.on_screen;
                    }

                    ImGui::BeginGroup(); {
                        ImGui::TextColored(header_color, "window-targeted overlay");

                        ImGui::BeginGroup(); {
                            ImGui::Text("target: %s (%#llx) %s\n"
                                "handle:  %#llx\n"
                                "monitor: %#llx\n"
                                "pos:     %d, %d\n"
                                "size:    %dx%d\n"
                                "scale:   %.2f, %.2f\n",
                                wto->target_window.name.c_str(), wto->target_window.handle, target_on_screen ? "on screen" : wto->target_window.on_screen ? "may be on another screen" : "not on screen at all",
                                wto->target_window.handle,
                                wto->target_window.monitor,
                                wto->target_window.position.x, wto->target_window.position.y,
                                wto->target_window.size.x, wto->target_window.size.y,
                                wto->target_window.scale.x, wto->target_window.scale.y);

                            ImGui::Text("screen relative (window): \n"
                                "visible: %s\n"
                                "pos:     %d, %d\n"
                                "size:    %dx%d\n",
                                wto->target_window.on_screen ? "true" : "false",
                                wto->target_window.relative_position.x, wto->target_window.relative_position.y,
                                wto->target_window.relative_size.x, wto->target_window.relative_size.y);

                            ImGui::SameLine();

                            ImGui::Text("screen relative (internals): \n"
                                "visible: %s\n"
                                "pos:     %d, %d\n"
                                "size:    %dx%d\n",
                                target_on_screen ? "true" : "false",
                                relative_position.x, relative_position.y,
                                relative_size.x, relative_size.y);
                        } ImGui::EndGroup();
                    } ImGui::EndGroup();
                }
            } ImGui::End();

            if (auto wto = targeted_overlay) {
                if (target_on_screen) {
                    ImGui::SetNextWindowPos({ float(relative_position.x), float(relative_position.y) });
                    ImGui::SetNextWindowSize({ float(relative_size.x), float(relative_size.y) });
                    ImGui::SetNextWindowBgAlpha(0.25f);
                    ImGui::Begin("wdwmo - imgui - wto", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs); {
                        ImGui::Text("imgui window: \n"
                            "pos:  %.0f, %.0f\n"
                            "size: %.0fx%.0f\n",
                            ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                            ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
                    }ImGui::End();
                }
            }
        }
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return wdwmo::status_t::success;
}

wdwmo::status_t termination_callback(const wdwmo::initialization_guard* guard) noexcept {
    conlog(
        "[w] " function_name ": called\n"
        "    from:         %#llx\n"
        "    thread:       %d\n"
        "    new instance: %#llx\n"
        "    termination:  %#llx\n",
        __return_address,
        __thread_id,
        guard,
        &should_terminate);

    should_terminate = true;

    if (auto thread_id = input_thread_id) {
        PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
    }

    return wdwmo::status_t::success;
}

void input_thread() noexcept {
    const auto mouse_hook = SetWindowsHookExA(WH_MOUSE_LL, &mouse_callback, nullptr, 0);
    const auto keyboard_hook = SetWindowsHookExA(WH_KEYBOARD_LL, &keyboard_callback, nullptr, 0);

    if (mouse_hook && keyboard_hook) {
        input_thread_id = __thread_id;

        conlog(
            "[w] " function_name " (%#llx): input hooks set\n"
            "    mouse:       %#llx\n"
            "    keyboard:    %#llx\n"
            "    termination: %#llx\n"
            "    actions binds: \n"
            "    \033[91mkill process:    %s + %s\033[0m\n"
            "    \033[93mterminate wdwmo: %s + %s\033[0m\n",
            input_thread,
            mouse_hook,
            keyboard_hook,
            &should_terminate,
            ncore::input::get_key_name(__actionKillProcess), ncore::input::get_key_name(__actionMajorKey),
            ncore::input::get_key_name(__actionTerminateWdwmo), ncore::input::get_key_name(__actionMajorKey));

        auto message = MSG();
        while (!should_terminate && (GetMessageA(&message, nullptr, 0, 0) > 0)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        UnhookWindowsHookEx(keyboard_hook);
        UnhookWindowsHookEx(mouse_hook);

        conlog(
            "[w] " function_name " (%#llx): input hooks unset\n"
            "    termination: %s\n"
            "    mouse:       %#llx\n"
            "    keyboard:    %#llx\n",
            input_thread,
            should_terminate ? "true" : "false",
            mouse_hook,
            keyboard_hook);
    }
}

void window_targeted_overlay_info_thread(wtoverlay* wto) noexcept {
    conlog(
        "[w] " function_name " (%#llx): entered\n"
        "    instance: %#llx\n"
        "    target:   %s\n"
        "    thread:   %d\n",
        window_targeted_overlay_info_thread,
        wto,
        wto->target_window.name.c_str(),
        __thread_id);

    auto counter = 0ui64;

    while (not should_terminate) {
        ncore::thread::sleep(10);

        auto update_handle = (counter += 10) >= 5000;

        if (!wto->get_window_info(update_handle)) {
            ncore::thread::sleep(1000);

            continue;
        }

        if (update_handle) {
            counter = 0ui64;
        }
    }

    conlog(
        "[w] " function_name " (%#llx): terminated\n"
        "    instance: %#llx\n"
        "    target:   %s\n"
        "    thread:   %d\n",
        window_targeted_overlay_info_thread,
        wto,
        wto->target_window.name.c_str(),
        __thread_id);
}

void on_attach(wdwmo::configuration_t* configuration) noexcept {
    using namespace wdwmo;

    auto system_boot_time = ncore::system_boot_time();
    auto process_environment = __process_environment;
    auto guard_identifier = get_guard_storage_identifier();

    conlog(
        "[w] " function_name " (%#llx): build from " __TIMESTAMP__ ", id %08x\n"
        "    system boot time:    %#llx\n"
        "    process environment: %#llx\n"
        "    guard identifier:    %#llx\n",
        on_attach,
        __cstrh(__TIMESTAMP__),
        system_boot_time,
        process_environment,
        guard_identifier);

    auto guard = get_or_create_shared_guard(guard_identifier);

    if (!configuration || !ncore::can_access(configuration)) {
        conlog("[w] " function_name ": configuration (%#llx) isn't specified or inacessible\n", configuration);

        return;
    }

    auto initialization_status = wdwmo::initialize(
        *configuration,
        initialization_callback,
        render_callback,
        guard,
        false,
        termination_callback);

    conlog("[w] " function_name ": initialization status: %d\n", initialization_status);

    if (initialization_status == status_t::success) {
        if (auto wto = targeted_overlay = new wtoverlay("Untitled - Paint")) {
            auto thread = ncore::thread::create(window_targeted_overlay_info_thread, wto);
        }
    }

    if constexpr (__inputHooks) {
        input_thread();
    }
}

void on_attach_safe(void* data) noexcept {
    wdwmo::debug::set_console_output(create_console<false>());

    auto crash_code = ui32_t();
    auto crash_address = address_t();

    __try {
        if (data) {
            conlog(
                "[w] " function_name ": data provided:\n"
                "    instance:       %#llx\n"
                "    present offset: %#x\n"
                "    dxgi offset:    %#x\n",
                data,
                ((wdwmo::configuration_t*)data)->offsets.present,
                ((wdwmo::configuration_t*)data)->offsets.dxgi_swap_chain);
        }
        else {
            auto build = ncore::get_system_version().build;

            data = build >= 22000 ?
                win11_25h2_26200_8655_info :
                win10_22h2_19045_6466_info;

            conlog(
                "[w] " function_name ": data isn't provided, falling back\n"
                "    windows build:  %u\n"
                "    selected info:  %#llx\n"
                "    present offset: %#x\n"
                "    dxgi offset:    %#x",
                build,
                data,
                ((wdwmo::configuration_t*)data)->offsets.present,
                ((wdwmo::configuration_t*)data)->offsets.dxgi_swap_chain);
        }

        return on_attach((wdwmo::configuration_t*)data);
    }
    __except (
        crash_code = exception_code(),
        crash_address = exception_info()->ExceptionRecord->ExceptionAddress,
        EXCEPTION_EXECUTE_HANDLER) {
        conlog("[w] " function_name ": main crashed at %#llx, reason 0x%08x\n", crash_address, crash_code);
    }
}

int DllMain(void* address, int reason, void* data) noexcept {
    if (reason == DLL_PROCESS_ATTACH) {
        LdrDisableThreadCalloutsForDll(address);

        ncore::async(on_attach_safe, data).release();
    }

    return TRUE;
}
#endif
