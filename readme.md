# wdwmo

**Windows Desktop Window Manager overlay through an internal DirectX render-pipeline hook in `dwm.exe`.**

`wdwmo` is an educational and portfolio-oriented project that renders directly into the Desktop Window Manager composition pipeline. Instead of creating a transparent top-level window, the overlay code runs inside `dwm.exe`, intercepts an internal DWM present call, obtains the Direct3D 11 render target used for a particular display, and delegates initialization and drawing to user-provided callbacks.

The project is designed around three goals:

- work across the different render paths used by Windows 10 and Windows 11;
- discover build-dependent symbols, structure fields, and virtual-method offsets automatically from Microsoft PDB files;
- expose the result as a small statically linkable callback-based library rather than as a renderer tied to a particular UI framework.

> [!WARNING]
> This project uses undocumented DWM internals and executes inside a critical Windows process. Incorrect offsets, an incorrect function ABI, invalid rendering code, or an unhandled exception can terminate or destabilize `dwm.exe`. It is intended for reverse-engineering research and controlled experiments, not as a production graphics API.

## Contents

- [Concept](#concept)
- [Demonstration and tested configurations](#demonstration-and-tested-configurations)
- [Why hook `COverlayContext::Present`](#why-hook-coverlaycontextpresent)
- [DWM render paths](#dwm-render-paths)
- [Project architecture](#project-architecture)
- [Runtime flow](#runtime-flow)
- [Configuration and automatic offset discovery](#configuration-and-automatic-offset-discovery)
- [Manual offset recovery](#manual-offset-recovery)
- [Display and monitor identification](#display-and-monitor-identification)
- [Public API and integration model](#public-api-and-integration-model)
- [Multiple initialization](#multiple-initialization)
- [Dependencies and build layout](#dependencies-and-build-layout)
- [License and third-party notices](#license-and-third-party-notices)
- [Capabilities](#capabilities)
- [Limitations](#limitations)
- [TODO](#todo)
- [Related work](#related-work)

---

## Concept

A conventional desktop overlay creates a separate window and asks Windows to compose it above the target content. `wdwmo` takes a different approach: it enters the compositor itself and inserts drawing commands into DWM's existing Direct3D 11 rendering flow.

At a high level:

```text
application / desktop surfaces
            │
            ▼
          DWM
            │
            ├─ render target selected for one display
            ├─ COverlayContext::Present(...)
            │       └─ wdwmo hook
            │              ├─ identify the D3D11 resource
            │              ├─ identify the physical display
            │              ├─ prepare a per-resource environment
            │              └─ invoke the operator render callback
            │
            ▼
       display output
```

The hook does not replace DWM's present operation. It performs overlay rendering and then forwards the original call with the original arguments.

`wdwmo` also does not force DWM to compose a new frame. Rendering occurs when the intercepted DWM path is naturally executed. Frame scheduling and composition invalidation are intentionally outside the current scope of the core library.

---

## Demonstration and tested configurations

The Debug sample uses ImGui to display the internal context, adapter, back-buffer, monitor, VidPn, scaling, and window-relative coordinate data obtained by `wdwmo`. It also demonstrates targeting a normal desktop window and mapping its desktop coordinates into the render target of the correct display.

### Windows 10 22H2 — real machine, NVIDIA

![Windows 10 22H2, build 19045.6466, real NVIDIA system](.screenshots/win10-22h2-19045-6466-real.png)

### Windows 11 25H2 — real machine, NVIDIA

![Windows 11 25H2, build 26200.8037, real NVIDIA system](.screenshots/win11-25h2-26200-8037-real.png)

### Windows 11 25H2 — VMware virtual machine

![Windows 11 25H2, build 26200.8655, VMware virtual machine](.screenshots/win11-25h2-26200-8655-virtual.png)

| Environment | Build | Observed DWM implementation | Resource access |
|---|---:|---|---|
| Windows 10 22H2, real NVIDIA system | 19045.6466 | `CLegacyRenderTarget` / `CLegacySwapChain` | legacy DXGI chain |
| Windows 11 25H2, VMware | 26200.8655 | legacy-style path similar to Windows 10 | modern internal buffer path and legacy fallback both work |
| Windows 11 25H2, real NVIDIA system | 26200.8037 | `CDDisplayRenderTarget` / `CDDisplaySwapChain` | internal physical back buffer → D3D11 resource |

These names describe the configurations observed during research. The selected path is not determined only by the Windows version. GPU vendor, display driver, virtualization, and the compositor environment can cause the same Windows build to instantiate a different implementation.

---

## Why hook `COverlayContext::Present`

DWM contains several render-target and swap-chain implementations. Hooking a class that exists only in one path makes the overlay dependent on the current Windows version or display-driver configuration.

`COverlayContext::Present` is used because it is a convenient common point above the relevant implementations:

- it is reached by the tested Windows 10 legacy path;
- it is reached by both the legacy-style and `CDDisplay` paths observed on Windows 11;
- its second argument is an `IOverlaySwapChain*`, which provides access to the active render resource;
- it executes early enough to draw into the current DWM target before the original present operation continues;
- it avoids separately hooking `CLegacyRenderTarget`, `CDDisplayRenderTarget`, or a driver-dependent DXGI/DDIs path.

Conceptually:

```text
Windows 10 / legacy-style Windows 11

CLegacyRenderTarget::RenderAndPresent / Present
    └─ COverlayContext::Present
           └─ CLegacySwapChain
                  └─ DXGI legacy present path

Windows 11 / CDDisplay path

CDDisplayRenderTarget::RenderAndPresent / Present
    └─ COverlayContext::Present
           └─ CDDisplaySwapChain
                  └─ ddisplay-backed present path
```

The containing classes and downstream implementation differ, but `COverlayContext::Present` remains a usable convergence point.

### ABI differences

The symbol name alone is not enough: the wrapper must also match the function ABI used by the target build.

The tested Windows 10 form is equivalent to:

```cpp
uint64_t __fastcall COverlayContext::Present(
    COverlayContext* instance,
    IOverlaySwapChain* swap_chain,
    uint32_t value,
    const std::vector<RECT>& dirty_rects,
    uint32_t value2,
    bool legacy_present);
```

The tested Windows 11 form contains one additional pointer argument before the final `legacy_present` flag:

```cpp
uint64_t __fastcall COverlayContext::Present(
    COverlayContext* instance,
    IOverlaySwapChain* swap_chain,
    uint32_t value,
    const std::vector<RECT>& dirty_rects,
    uint32_t value2,
    bool* value3,
    bool legacy_present);
```

The current core selects the corresponding wrapper from the Windows build family. Recovering and validating the ABI directly from symbols/type information is a future improvement.

---

## DWM render paths

### Legacy path

The legacy implementation is centered around classes such as:

```text
CLegacyRenderTarget
CLegacySwapChain
CLegacySwapChainBuffer
IDXGISwapChainDWM / IDXGISwapChainDWM1
```

In this path, the `IOverlaySwapChain` object contains an internal DXGI DWM swap-chain pointer. Once its structure offset is known, `wdwmo` can obtain the back buffer through the legacy swap-chain interface and initialize a normal D3D11 rendering environment.

This path was observed on the tested Windows 10 system and on the Windows 11 VMware configuration.

### `CDDisplay` path

On the tested real Windows 11 system with an NVIDIA driver, DWM used classes such as:

```text
CDDisplayRenderTarget
CDDisplaySwapChain
CDDisplaySwapChainBuffer
```

The downstream present path involves `ddisplay.dll` rather than being identical to the legacy DXGI route. A fixed `CLegacySwapChain` layout is therefore not sufficient.

For this path, `wdwmo` uses virtual methods shared by the active overlay swap-chain implementation:

```text
IOverlaySwapChain::GetPhysicalBackBuffer
    └─ IOverlaySwapChainBuffer
           └─ ISwapChainBuffer::GetD3D11Resource
                  └─ ID3D11Resource / ID3D11Texture2D
```

The returned pointers are internal borrowed fields/getters. They are not treated as newly created COM objects by the getter itself. `wdwmo` creates and owns only the references needed for its cached D3D11 environment.

### Selection strategy

The generated configuration can describe both access strategies. At runtime the core prefers the internal physical-buffer path when both virtual-method offsets are available:

```text
GetPhysicalBackBuffer + GetD3D11Resource available
    └─ use the modern internal resource path

otherwise, DXGI swap-chain structure offset available
    └─ use the legacy fallback
```

This is intentionally capability-based rather than based only on `Windows 10` versus `Windows 11`. In the tested Windows 11 VMware configuration, both strategies are usable even though the instantiated classes are legacy-style.

---

## Project architecture

The repository consists of three main parts.

### `wdwmo`

The in-process overlay core.

- Release configuration: static library (`.lib`).
- Debug configuration: DLL containing the ImGui demonstration implementation.
- Installs the `COverlayContext::Present` hook.
- Creates and caches one rendering environment per discovered render resource.
- Exposes initialization and render callbacks.
- Handles image validation, resource acquisition, output identification, and hook lifecycle.

### `wdwmcd`

The **Windows DWM core dumper**.

- Static library (`.lib`).
- Accepts `dwmcore.dll`, `dxgi.dll`, and their matching PDB data as memory buffers.
- Parses PE/CodeView information.
- Resolves symbols without DIA SDK or DbgHelp.
- Scans vtables and disassembles selected functions.
- Produces a `wdwmo::configuration_t` tied to the supplied binaries.
- Can construct the corresponding Microsoft Symbol Server PDB URL.

### `wdwmosl`

The **Windows DWM overlay sample loader**.

- Debug/sample executable.
- Reads the system `dwmcore.dll` and `dxgi.dll` or paths supplied by the operator.
- Downloads matching PDB files from the Microsoft Symbol Server.
- Calls `wdwmcd::detect_configuration`.
- Demonstrates loading the Debug `wdwmo.dll` into `dwm.exe` and passing the generated configuration.
- Demonstrates repeated initialization and inspection of an already installed working set.

### Data flow

```text
              dwmcore.dll + dwmcore.pdb
              dxgi.dll    + dxgi.pdb
                         │
                         ▼
                      wdwmcd
                         │
                         │ configuration_t
                         ▼
operator / sample loader ──────► code running inside dwm.exe
                                      │
                                      ▼
                                    wdwmo
                                      │
                         hook COverlayContext::Present
                                      │
                         per-resource environment cache
                                      │
                         initialization/render callbacks
```

`wdwmo` itself is not an injector. Injection or otherwise arranging execution inside `dwm.exe` is the responsibility of the integrating project. `wdwmosl` is only one example.

---

## Runtime flow

### 1. Build-specific configuration is generated

`wdwmcd` analyzes the exact `dwmcore.dll` and `dxgi.dll` files that are expected to be loaded by the target DWM process. It fills:

- PE identity for both images;
- `COverlayContext::Present` RVA;
- required structure-field offsets;
- required vtable-relative method offsets;
- the `IDXGIOutputDWM` vtable RVA used for runtime object identification.

### 2. The core validates the target process

Inside `dwm.exe`, `wdwmo` locates the loaded `dwmcore.dll` and `dxgi.dll`, reads their PE identity, and compares it with the supplied configuration.

The identity currently contains:

```text
TimeDateStamp
SizeOfImage
CheckSum
```

This prevents offsets generated for one binary pair from being silently applied to another pair.

### 3. The present hook is installed

The target address is calculated as:

```text
dwmcore base + configuration.offsets.present
```

MinHook creates a trampoline to the original function. The core stores the original address in the active `working_set` and enables the hook.

### 4. The active D3D11 resource is obtained

For every intercepted call:

1. the core attempts the physical-back-buffer path when it is present in the configuration;
2. otherwise it reads the legacy `IDXGISwapChainDWM` pointer from `IOverlaySwapChain`;
3. the resulting resource or chain pointer is used as the environment-registry key.

### 5. A per-resource environment is initialized

On first use, `wdwmo` obtains and caches:

```text
IDXGIOutputDWM*
ID3D11Device*
ID3D11DeviceContext*
ID3D11Texture2D*
ID3D11RenderTargetView*
chain / adapter / display metadata
```

The operator's initialization callback is then called once for that environment.

### 6. The render callback is invoked

Before invoking the operator callback, `wdwmo` stores the currently bound output-merger render target and depth-stencil view. After the callback returns, the previous state is restored and the original DWM present function continues.

The Debug sample explicitly binds `context.info.render_target_view` before issuing ImGui draw calls.

---

## Configuration and automatic offset discovery

The core and dumper share the following configuration model:

```cpp
struct configuration_t {
    struct {
        executable_image_info_t dwmcore;
        executable_image_info_t dxgi;
    } image_info;

    struct {
        int32_t present;

        int32_t render_target;
        int32_t dxgi_output_vftable;
        int32_t dxgi_output;

        int32_t dxgi_swap_chain;

        int32_t get_physical_back_buffer;
        int32_t get_d3d11_resource;
    } offsets;
};
```

The fields are intentionally a mixture of module RVAs, structure displacements, vtable-relative byte offsets, and one runtime-discovered structure displacement.

| Field | Kind | Discovery time | Purpose |
|---|---|---|---|
| `present` | RVA in `dwmcore.dll` | static / PDB | Address of the main hook target, `COverlayContext::Present` |
| `render_target` | structure displacement | static / disassembly | `COverlayContext → C...RenderTarget*` |
| `dxgi_output_vftable` | RVA in `dxgi.dll` | static / PDB | Identifies an `IDXGIOutputDWM` subobject in memory |
| `dxgi_output` | structure displacement | runtime | `C...RenderTarget → IDXGIOutputDWM*`; discovered by scanning the active render-target layout |
| `dxgi_swap_chain` | structure displacement | static / disassembly | `IOverlaySwapChain → IDXGISwapChainDWM*`; legacy fallback |
| `get_physical_back_buffer` | vtable-relative byte offset | static / PDB + vtable scan | Calls `IOverlaySwapChain::GetPhysicalBackBuffer` |
| `get_d3d11_resource` | vtable-relative byte offset | static / PDB + vtable scan | Calls `ISwapChainBuffer::GetD3D11Resource` |

### Why these values were selected

The configuration contains only the information required to reach stable runtime objects from the common `COverlayContext::Present` call:

- `present` provides a single hook point shared by the observed paths;
- `render_target`, `dxgi_output_vftable`, and `dxgi_output` identify the physical output without depending on a fixed `CLegacyRenderTarget` or `CDDisplayRenderTarget` layout;
- the two virtual-method offsets provide a path-independent way to obtain the active D3D11 resource;
- `dxgi_swap_chain` preserves compatibility with the legacy path and acts as a fallback when the internal buffer methods cannot be recovered.

### PDB parsing without DIA or DbgHelp

`wdwmcd` uses `raw_pdb` directly. It validates and reads the DBI, image-section, public-symbol, global-symbol, and module-symbol streams.

Symbol resolution supports:

- exact undecorated names;
- conversion of qualified names into MSVC decorated-name prefixes;
- public and global symbol records;
- module symbol streams;
- `S_PROCREF` / `S_LPROCREF` records that reference procedure records stored in module streams;
- adjustor/vtordisp thunks when searching for vtable entries;
- rejection of adjustor thunks when the actual function body is required.

PDB section/offset pairs are converted to RVAs through the image-section stream.

### Vtable analysis

To recover virtual-method offsets, the dumper:

1. resolves all candidate vtable symbols for the relevant implementation classes;
2. resolves all candidate function or adjustor-thunk RVAs for the target method;
3. scans each vtable in the matching image;
4. compares each entry against the resolved method RVAs;
5. returns the byte displacement of the matching slot.

The scan is bounded by the next vtable symbol. This is important because MSVC emits adjacent vtables for separate base/interface subobjects, and some interface vtables can be only one pointer long. Scanning across the next symbol can produce a false match in a neighboring table.

### Function boundaries

When a function body must be disassembled, `wdwmcd` uses the PE exception directory (`.pdata`) to recover `RUNTIME_FUNCTION` boundaries. If a symbol points into a chunk or thunk, a containing range is accepted as a fallback.

The current implementation contains conservative size fallbacks for cases where `.pdata` lookup fails, but normal analysis uses the recovered function size.

---

## Manual offset recovery

This section describes how to recover the same values manually in a disassembler or debugger. Exact instructions and layouts can change between builds; the goal is to follow data flow and object identity rather than copy constants from another machine.

### `present`

```cpp
int32_t present; // RVA, static
```

Target symbol:

```text
COverlayContext::Present
```

With matching Microsoft symbols, resolve the function and store its RVA relative to the loaded `dwmcore.dll` image base.

At runtime:

```text
hook target = loaded dwmcore.dll base + present RVA
```

The function ABI must be verified independently. The tested Windows 10 and Windows 11 signatures are not identical.

### `render_target`

```cpp
int32_t render_target; // structure displacement, static
```

Meaning:

```text
COverlayContext + render_target → CRenderTarget-derived object pointer
```

The value is recovered from `COverlayContext::COverlayContext`. In the tested binaries, the constructor stores its render-target argument from `RDX` into the `COverlayContext` instance in `RCX`:

```asm
mov qword ptr [rcx + displacement], rdx
```

The destination displacement is `render_target`. A zero displacement is valid and is used by the tested layouts.

The core later dereferences this member and receives the active `CLegacyRenderTarget`, `CDDisplayRenderTarget`, or another compatible `CRenderTarget` implementation selected for that DWM session.

### `dxgi_output_vftable`

```cpp
int32_t dxgi_output_vftable; // RVA in dxgi.dll, static
```

Target symbol:

```text
const ATL::CComObject<class CDXGIOutput>::`vftable'{for `IDXGIOutputDWM'}
```

Decorated form used by the current dumper:

```text
??_7?$CComObject@VCDXGIOutput@@@ATL@@6BIDXGIOutputDWM@@@
```

The value is stored as an RVA relative to `dxgi.dll`.

At runtime the expected vtable address is:

```text
loaded dxgi.dll base + dxgi_output_vftable
```

Because this vtable is the first pointer of the relevant `IDXGIOutputDWM` interface subobject, it can be used as a strong runtime type marker.

### `dxgi_output`

```cpp
int32_t dxgi_output; // structure displacement, runtime
```

Meaning:

```text
active CRenderTarget-derived object + dxgi_output → IDXGIOutputDWM*
```

`CLegacyRenderTarget` and `CDDisplayRenderTarget` have different layouts. Instead of hardcoding both layouts, `wdwmo` discovers the offset in the active implementation:

1. read the render-target pointer from `COverlayContext + render_target`;
2. scan an initial range of pointer-sized fields in the object;
3. for every accessible pointer candidate, read its first pointer;
4. compare it with `dxgi.dll base + dxgi_output_vftable`;
5. cache the matching render-target field displacement in `configuration.offsets.dxgi_output`.

DWM uses one selected render-target implementation for the compositor session in the tested environments, so one runtime-discovered offset is cached for the process.

### `dxgi_swap_chain`

```cpp
int32_t dxgi_swap_chain; // structure displacement, static
```

Meaning:

```text
IOverlaySwapChain / CLegacySwapChain + dxgi_swap_chain
    → IDXGISwapChainDWM / IDXGISwapChainDWM1 pointer
```

A convenient recovery point is:

```text
CLegacySwapChain::PresentMPO
    └─ CD3DDevice::PresentMPO(..., IDXGISwapChainDWM1*, ...)
```

On Windows x64, the second integer/pointer argument is passed in `RDX`. Locate the call from `CLegacySwapChain::PresentMPO` to `CD3DDevice::PresentMPO`, then inspect the argument preparation immediately before the call. The relevant instruction loads `RDX` from a member of the swap-chain object:

```asm
mov rdx, qword ptr [base + displacement]
call CD3DDevice::PresentMPO
```

The memory displacement is `dxgi_swap_chain`.

Observed examples:

```text
Windows 10 22H2: -0x118
Windows 11 25H2 legacy implementation: +0x108
```

These examples are not portable constants and are included only to illustrate why automatic discovery is required.

### `get_physical_back_buffer`

```cpp
int32_t get_physical_back_buffer; // vtable-relative byte offset, static
```

Relevant implementations:

```text
CLegacySwapChain::GetPhysicalBackBuffer
CDDisplaySwapChain::GetPhysicalBackBuffer
```

Relevant vtables:

```text
CLegacySwapChain vtables
CDDisplaySwapChain vtables
```

Resolve the method body and any valid adjustor thunks, then locate the corresponding entry in the implementation vtables. The byte distance from the beginning of the selected vtable to the matching entry is the required offset.

The method returns an internal `IOverlaySwapChainBuffer`-compatible object for the physical back buffer used by the current present operation.

### `get_d3d11_resource`

```cpp
int32_t get_d3d11_resource; // vtable-relative byte offset, static
```

Relevant implementations:

```text
CLegacySwapChainBuffer::GetD3D11Resource
CDDisplaySwapChainBuffer::GetD3D11Resource
```

Relevant vtables:

```text
CLegacySwapChainBuffer vtables
CDDisplaySwapChainBuffer vtables
```

The recovery process is identical to `get_physical_back_buffer`: resolve the method/thunks and find the matching slot in each candidate vtable.

The returned pointer is the internal D3D11 resource associated with the swap-chain buffer. `wdwmo` queries it for `ID3D11Texture2D`, obtains the owning device and immediate context, and creates an `ID3D11RenderTargetView` for the cached environment.

---

## Display and monitor identification

Obtaining a texture is not enough for a multi-monitor overlay. The core must know which desktop output that texture belongs to, its virtual-desktop coordinates, and the DPI scaling that should be applied when mapping window coordinates into the render target.

### Output-object recovery

The chain begins at the hooked call:

```text
COverlayContext*
    └─ [render_target] → CRenderTarget-derived object
           └─ [runtime dxgi_output] → IDXGIOutputDWM
```

The `IDXGIOutputDWM` object is located by its build-specific vtable address rather than by assuming one fixed render-target layout.

### Output metadata

The object is queried as both `IDXGIOutput` and `IDXGIOutputDWM`.

`IDXGIOutput::GetDesc` provides information such as:

- `HMONITOR`;
- output rotation;
- standard DXGI output identity.

The internal `IDXGIOutputDWM::GetDesc` provides DWM-specific information, including:

- source VidPn ID;
- target VidPn ID;
- desktop rectangle used by the active Windows 10 or Windows 11 descriptor layout.

The source and target identifiers are interpreted together with the adapter LUID. VidPn IDs are not treated as globally unique across adapters.

### Friendly name, device path, and GDI name

With the adapter LUID and source/target VidPn IDs, the core calls `DisplayConfigGetDeviceInfo` for:

```text
DISPLAYCONFIG_SOURCE_DEVICE_NAME
DISPLAYCONFIG_TARGET_DEVICE_NAME
```

This provides:

- GDI display name, for example `\\.\DISPLAY1`;
- monitor device path;
- monitor-friendly name when available.

### Adapter metadata

The D3D11 device is queried for `IDXGIDevice`, then its `IDXGIAdapter` is used to retrieve:

- adapter LUID;
- vendor, device, subsystem, and revision IDs;
- dedicated video memory;
- dedicated system memory;
- shared system memory;
- adapter description.

### Scaling and desktop coordinates

The monitor handle is passed to `GetDpiForMonitor` with `MDT_EFFECTIVE_DPI`. The result is normalized against 96 DPI:

```text
scale.x = dpiX / 96
scale.y = dpiY / 96
```

The Debug sample demonstrates how to:

1. obtain a target window's desktop position and logical size;
2. determine whether it intersects the current output rectangle;
3. translate it into coordinates relative to that output;
4. apply per-monitor scaling;
5. render the overlay only into the environment associated with the correct display.

### Display modes

The design supports the main Windows display modes used by the tested sample:

- **Extend** — each output has its own desktop rectangle and environment;
- **Duplicate** — outputs can share a source while retaining target/output identity;
- **Show only on one display** — only the active attached output receives the relevant rendering environment.

The current implementation initializes the display metadata when the environment is created. Dynamic topology changes after initialization are listed in [TODO](#todo).

---

## Public API and integration model

The Release core is intended to be statically linked into code that will execute inside `dwm.exe`. Rendering is framework-independent and callback-based.

### Context model

The callback receives:

```cpp
struct context_t {
    environment_t& info; // cacheable per-resource information
    call_t& call;        // data belonging to the current Present call
};
```

`context.info` contains:

- output, device, immediate context, texture, and render-target-view pointers;
- buffer dimensions and format;
- adapter information;
- monitor handle, names, path, rectangle, scaling, rotation, and VidPn IDs;
- a local linked-value pool for operator state.

`context.call` contains:

- current `COverlayContext*`;
- current `IOverlaySwapChain*`;
- the DWM dirty-rectangle vector;
- the `legacy_present` flag passed to the original call.

### Minimal callback structure

```cpp
wdwmo::status_t on_initialize(const wdwmo::context_t& context) {
    // Called when a new render resource/environment is discovered,
    // and again when an existing environment is marked for reinitialization.

    // Initialize the renderer with:
    // context.info.device
    // context.info.device_context

    return wdwmo::status_t::success;
}

wdwmo::status_t on_render(const wdwmo::context_t& context) {
    auto device_context =
        context.info.device_context_as<ID3D11DeviceContext>();

    auto& target =
        context.info.render_target_view_as<ID3D11RenderTargetView>();

    device_context->OMSetRenderTargets(1, &target, nullptr);

    // Issue D3D11 draw commands here.

    return wdwmo::status_t::success;
}
```

Initialization is then performed with a generated configuration:

```cpp
const auto status = wdwmo::initialize(
    configuration,
    on_initialize,
    on_render,
    initialization_guard,
    false,
    on_terminate);
```

The guard and termination callback are optional. Without a guard, the core can still install and use the hook, but it cannot coordinate replacement by a later mapped copy through shared storage.

### Dumper usage

`wdwmcd` operates on memory buffers and does not require a live DWM object:

```cpp
wdwmo::configuration_t configuration{};

const auto status = wdwmcd::detect_configuration(
    {
        .image = { dwmcore_image, dwmcore_image_size },
        .pdb   = { dwmcore_pdb,   dwmcore_pdb_size   }
    },
    {
        .image = { dxgi_image, dxgi_image_size },
        .pdb   = { dxgi_pdb,   dxgi_pdb_size   }
    },
    configuration);
```

The matching PDB URL can be generated from a PE's RSDS record:

```cpp
std::string url;
wdwmcd::get_micrsofot_server_pdb_url(image, image_size, url);
```

Downloading and caching the PDB is left to the operator. The sample loader performs the download through its own networking layer.

---

## Multiple initialization

Repeated manual mapping or loading of a new implementation into `dwm.exe` must not blindly install another hook over an existing trampoline.

`wdwmo::initialization_guard` solves this by keeping a small shared record in a caller-selected process storage location. The Debug sample uses a field in the process environment block as the storage slot, but the provider is supplied by the operator.

The shared state records:

- a storage identifier;
- the active `working_set`;
- the existing implementation's termination callback;
- a shared core termination callback;
- an operator-controlled key/value pool.

When a new initialization finds an existing working set:

1. the previous operator termination callback is invoked;
2. the original hook/trampoline and working set are reused;
3. rendering is temporarily suspended;
4. initialization and rendering callbacks are replaced;
5. cached environments are marked for reinitialization;
6. rendering resumes.

This allows a new mapped implementation to replace its callback logic without stacking hooks or losing the original `COverlayContext::Present` trampoline.

The mechanism is intentionally operator-managed because the storage location and commit semantics depend on how the integrating project maps or unloads code.

---

## Dependencies and build layout

### Toolchain

- Windows x64;
- Visual Studio / MSVC;
- C++20;
- Windows SDK with D3D11, DXGI, SHCore, and related headers/libraries.

The reverse-engineering logic and wrapper ABI are x64-specific.

### Included/external components

| Component | Used by | Repository form | License |
|---|---|---|---|
| MinHook | `wdwmo` | header and static library | BSD 2-Clause-style notices, including HDE32/HDE64 |
| Direct3D 11 / DXGI and Windows APIs | `wdwmo` and sample | Windows SDK/system components | Microsoft terms |
| `raw_pdb` | `wdwmcd` | bundled source | BSD 2-Clause |
| BeaEngine | `wdwmcd` | bundled static library | LGPL-3.0-or-later |
| Dear ImGui 1.92.8 | Debug sample only | bundled source | MIT |
| cpr | `wdwmosl` networking layer | bundled static library | MIT |
| curl / libcurl 7.86.0-DEV | `wdwmosl` networking layer | bundled static library | curl license |
| zlib 1.2.13 | libcurl compression support | bundled static library | zlib license |
| `ncore` / `nweb` | sample/project utilities | private author-owned utilities | stated separately when distributed |

### No DIA SDK or DbgHelp requirement

Unlike implementations that delegate symbol handling to DIA SDK or DbgHelp, `wdwmcd` parses the supplied PDB data directly through `raw_pdb`. The core/dumper design therefore does not require a separately installed DIA runtime or a DbgHelp-based symbol session.

The project still uses normal Windows system libraries and statically linked helper libraries where appropriate; “no dynamic dependencies” here means no additional external symbol-engine DLL requirement.

### Repository build note

The author's private header-only `ncore` utility library is intentionally not included as a public, supported dependency. Consequently, the repository is currently a research/reference project rather than a turnkey build package. The DWM-specific logic is present, but anyone building the current tree must provide equivalent utility functions or replace the `ncore` calls.

### Configuration outputs

| Project | Debug | Release |
|---|---|---|
| `wdwmo` | DLL with demonstration code | static library |
| `wdwmcd` | static library with diagnostics | static library |
| `wdwmosl` | console sample loader | application/sample configuration |

---

## License and third-party notices

The original project code is released under the [MIT License](license):

```text
Copyright (c) 2026 nepoladka
```

The MIT license applies only to code authored for this project. Bundled
third-party source and static libraries retain their own licenses and copyright
notices. The authoritative component list and complete notices are provided in
[`third_party_notices.md`](third_party_notices.md) and the [`.licenses/`](.licenses/)
directory.

| Component | License |
|---|---|
| Dear ImGui | MIT |
| raw_pdb | BSD 2-Clause |
| MinHook and bundled HDE portions | BSD 2-Clause-style notices |
| BeaEngine | LGPL-3.0-or-later |
| cpr | MIT |
| curl / libcurl | curl license |
| zlib | zlib license |

> [!IMPORTANT]
> BeaEngine is currently distributed as a static library. The LGPL can require
> corresponding source and relinkable application material, or a suitable
> shared-library mechanism, when a combined binary is distributed. Adding a
> license file alone does not satisfy every static-link distribution scenario.
> See [Third-party notices](third_party_notices.md#beaengine-and-static-linking).

> [!CAUTION]
> `shared/includes/sources/binaries/consola.ttf.h` appears to contain embedded
> Consolas font data. It is not covered by the project's MIT license. Verify an
> applicable redistribution right or replace/remove the embedded font before a
> public release.

The bundled `libcurl.lib` identifies itself as `7.86.0-DEV` and uses the Windows
Schannel backend. Rebuilding cpr/libcurl with another feature set can introduce
additional license obligations; the third-party notice must be updated whenever
those binaries are replaced.

---

## Capabilities

### Callback-based static-library architecture

The core is not tied to ImGui or any specific renderer. The integrating project supplies initialization, render, and optional termination callbacks. The ImGui implementation exists only as a Debug example.

### Windows 10 and Windows 11 render paths

The project supports the observed legacy and `CDDisplay` resource paths and selects a resource-access strategy from the generated configuration rather than relying on a single hardcoded Windows layout.

### Multi-monitor output identification

Each discovered render environment includes the physical output identity, desktop rectangle, display names, adapter, buffer size, and VidPn path IDs required to target rendering to the correct screen.

### Main display modes

The Debug sample demonstrates the information and coordinate handling needed for extended, duplicated, and single-display configurations.

### Per-monitor scaling

Effective monitor DPI is converted to scaling factors and exposed in the environment. The sample applies these factors when translating a target window's desktop coordinates into screen-relative D3D render-target coordinates.

### Multiple initialization

A shared initialization guard allows callback implementations to be replaced while reusing the existing hook and original-function trampoline.

### Automated build-specific configuration

All offsets currently required by the core are recovered from the supplied `dwmcore.dll` / `dxgi.dll` images and matching PDB files, with the render-target-to-output field discovered at runtime from object identity.

### No hardcoded monitor index

The core does not assume that the target is the primary display or that output zero corresponds to the desired screen. Rendering contexts are associated with the actual DWM output object.

---

## Limitations

### Undocumented internals

The project depends on private DWM classes, internal interfaces, compiler-generated vtables, and non-public descriptor layouts. A Windows cumulative update or display-driver change can alter behavior even when symbol names remain available.

Automatic discovery reduces the amount of hardcoded build data, but it cannot make undocumented interfaces stable.

### Tested-build coverage

The design target is current Windows 10 and Windows 11 x64. The configurations explicitly verified during development are listed in [Demonstration and tested configurations](#demonstration-and-tested-configurations).

Other builds may work when the same symbols and recognizable code patterns exist, but they should be treated as unverified until tested.

### GPU and driver coverage

The current real-machine testing is NVIDIA-based, with VMware used to observe a different Windows 11 path. Intel, AMD, hybrid-GPU, and more complex multi-adapter systems require additional testing.

### Display changes after initialization

The current environment cache assumes that the identified resource, output, rectangle, and scaling remain valid for its lifetime. It does not yet fully handle:

- monitor hot-plug or removal;
- switching between Extend, Duplicate, and single-display modes while active;
- resolution, orientation, refresh, HDR, or DPI changes;
- DWM resource recreation or device reset;
- migration between adapters.

Restarting or reinitializing the overlay after such a change is the current safe behavior.

### DWM-controlled frame cadence

The overlay is rendered only when the hooked DWM present path executes. The core does not call internal scheduling functions, force dirty rendering, or request a composition pass. A completely static desktop can therefore update at DWM's own cadence rather than at an overlay-selected frame rate.

### Present ABI selection

The function RVA is obtained from PDB data, but the wrapper ABI is currently selected from the Windows build family. A future build can change the argument list while preserving the symbol name.

### One selected render-target implementation per session

The runtime `dxgi_output` field offset is discovered once and cached under the observed invariant that DWM selects one render-target implementation for the compositor session. Dynamic switching between incompatible render-target layouts in the same session is not currently modeled.

### Sample scope

`wdwmosl` and the ImGui implementation are demonstrations, not a hardened injector, input subsystem, renderer, or process-lifecycle framework.

---

## TODO

### Core

- [ ] Detect display-topology and DWM resource changes.
- [ ] Invalidate and rebuild affected per-resource environments.
- [ ] Refresh monitor rectangles, VidPn IDs, names, and DPI after configuration changes.
- [ ] Handle output removal and stale registry entries.
- [ ] Test Intel, AMD, hybrid-GPU, multi-adapter, HDR, rotation, and additional multi-monitor configurations.
- [ ] Derive or validate the `COverlayContext::Present` ABI from richer PDB/type information instead of only the build family.
- [ ] Expand verified Windows build coverage.

### Dumper

- [ ] Split PE parsing, PDB resolution, vtable analysis, disassembly, and configuration construction into separate components.
- [ ] Bring diagnostics, names, error handling, and formatting into the common project style.
- [ ] Make conflicting vtable-slot results an explicit analysis result instead of silently retaining the first offset.
- [ ] Improve validation of recovered instructions and offsets.
- [ ] Add reusable dump output/serialization for generated configurations.

### Documentation and samples

- [ ] Add additional screenshots for Extend, Duplicate, mixed scaling, and multiple physical displays.
- [ ] Add diagrams of the legacy and `CDDisplay` call paths.
- [ ] Add annotated disassembly examples for every recovered offset.
- [ ] Add a minimal renderer example without ImGui.
- [ ] Document tested driver and hardware combinations as coverage grows.
- [ ] Replace or isolate the private `ncore` utility calls if a self-contained public build becomes a project goal.

---

## Related work

The project was developed after studying existing public DWM overlay implementations and the limitations of path-specific approaches:

- [aurenex/dwm-overlay](https://github.com/aurenex/dwm-overlay)
- [chaosium43/dwm-overlay](https://github.com/chaosium43/dwm-overlay)

`wdwmo` differs primarily in its use of `COverlayContext::Present` as a common interception point, its support for both the legacy and physical-back-buffer resource paths, automatic build-specific configuration from PDB data, runtime `IDXGIOutputDWM` discovery, and callback-oriented static-library design.

---

## Research status

The overlay core, automatic configuration discovery, multi-display identification, per-monitor scaling, and callback model are implemented. The largest remaining functional task is dynamic display-configuration handling. The largest remaining maintenance task is restructuring and polishing the dumper implementation.
