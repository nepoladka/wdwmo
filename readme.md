# wdwmo - windows dwm overlay

**Windows Desktop Window Manager overlay through an internal DirectX render-pipeline hook in `dwm.exe`.**

`wdwmo` is an educational and portfolio-oriented project that renders directly into the Desktop Window Manager composition pipeline. Instead of creating a transparent top-level window, the overlay code runs inside `dwm.exe`, intercepts an internal DWM present call, obtains the Direct3D 11 render target used for a particular display, and delegates initialization and drawing to user-provided callbacks.

> [!WARNING]
> The project is intended for research involving reverse engineering and controlled experiments. It implements a concept rather than serving as a finished product. We are not liable for consequences resulting from the use of this project for purposes other than those intended.


## Contents

- [Features](#features)

- [Demonstration and tested configurations](#demonstration-and-tested-configurations)

- [Usage, API, integration model](#usage-api-integration-model)

- [TODO](#todo)

- [Explanation](#explanation)

  - [Related works and its limitations](#related-works-and-its-limitations)

  - [How the `dwm.exe` render works](#how-the-dwmexe-render-works)

  - [What do we need](#what-do-we-need)

  - [Restored DXGI undocumented internals](#restored-dxgi-undocumented-internals)

  - [Project dependencies](#project-dependencies)

  - [Project architecture](#project-architecture)

  - [Multiple initialization](#multiple-initialization)

  - [Licensing](#licensing)
---

## Features

- ### Callback-based static-library architecture
The core is not tied to **ImGui** or any specific renderer. The integrating project supplies initialization, render, and optional termination callbacks. The **ImGui** implementation exists only as a `debug` example.

- ### Windows 10 and Windows 11 render paths
The project supports the observed legacy and `CDDisplay` resource paths and selects a resource-access strategy from the generated configuration rather than relying on a single hardcoded `Windows` layout.

- ### Multi-monitor output identification
Each discovered render environment includes the physical output identity, desktop rectangle, display names, adapter, buffer size, and VidPn path IDs required to target rendering to the correct screen.

- ### Main display modes
The `debug` sample demonstrates the information and coordinate handling needed for extended, duplicated, and single-display configurations.

- ### Per-monitor scaling
Effective monitor DPI is converted to scaling factors and exposed in the environment. The sample applies these factors when translating a target window's desktop coordinates into screen-relative D3D render-target coordinates.

- ### Multiple initialization
A shared initialization guard allows callback implementations to be replaced while reusing the existing hook and original-function trampoline.

- ### Automated build-specific configuration
All offsets currently required by the core are recovered from the supplied `dwmcore.dll` / `dxgi.dll` images and matching PDB files, with the render-target-to-output field discovered at runtime from object identity.

- ### No hardcoded monitor index
The core does not assume that the target is the primary display or that output zero corresponds to the desired screen. Rendering contexts are associated with the actual `dwm` output object.

---

## Demonstration and tested configurations

- ### Windows 10 22H2 - real machine, NVIDIA

![Windows 10 22H2, build 19045.6466, real NVIDIA system](.screenshots/win10-22h2-19045-6466-real.png)

- ### Windows 11 25H2 - real machine, NVIDIA

![Windows 11 25H2, build 26200.8037, real NVIDIA system](.screenshots/win11-25h2-26200-8037-real.png)

- ### Windows 11 25H2 - VMware virtual machine

![Windows 11 25H2, build 26200.8655, VMware virtual machine](.screenshots/win11-25h2-26200-8655-virtual.png)

### Tested configurations

| Environment | Build | Observed DWM implementation | Resource access |
|---|---:|---|---|
| Windows 10 22H2, real NVIDIA system | 19045.6466 | `CLegacyRenderTarget` / `CLegacySwapChain` | legacy DXGI chain |
| Windows 11 25H2, VMware | 26200.8655 | legacy-style path similar to Windows 10 | modern internal buffer path and legacy fallback both work |
| Windows 11 25H2, real NVIDIA system | 26200.8037 | `CDDisplayRenderTarget` / `CDDisplaySwapChain` | internal physical back buffer → D3D11 resource |

These names describe the configurations observed during research. The selected path is not determined only by the Windows version. GPU vendor, display driver, virtualization, and the compositor environment can cause the same Windows build to instantiate a different implementation.

---

## Usage, API, integration model

The `release` core is intended to be statically linked into code that will execute inside `dwm.exe`. Rendering is framework-independent and callback-based.

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

The `guard` and `termination callback` are optional. Without a guard, the core can still install and use the hook, but it cannot coordinate replacement by a later mapped copy through shared storage.

### Dumper usage

`wdwmcd` operates on memory buffers and does not require a live `dwm` object:

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

## TODO

### Core

- [ ] Update monitor rectangles, VidPn IDs, names, and DPI after configuration changes.
- [ ] Test Intel, AMD, hybrid-GPU, multi-adapter, HDR, rotation, and additional multi-monitor configurations.
- [ ] Derive or validate the `COverlayContext::Present` ABI from richer PDB/type information instead of only the build family.
- [ ] Expand verified `Windows` build coverage.

### Dumper

- [ ] Full code review, refactor, cleanup.

### Another
- [ ] Add missing dependencies like **ncore**.

---

# Explanation

## Related works and its limitations

The project was developed after studying existing public DWM overlay implementations and the limitations of path-specific approaches:

- [aurenex/dwm-overlay](https://github.com/aurenex/dwm-overlay)
  - doesn't works on `Windows 11 24H2+` with `NVidia` drivers.
  - doesn't supports displays configuration changes.

- [chaosium43/dwm-overlay](https://github.com/chaosium43/dwm-overlay)
  - works only on `Windows 11`.
  - supports only primary monitor.
  - manipulates internal states of `dwm`.
  - has many offset dependencies that are difficult to maintain.
  - dumper requires external `dbghelp` modules and cached on disk analyzing `.pdb`.

So, I conducted a little research into `dwm` internals to solve these issues.

---

## How the `dwm.exe` render works
`dwm` uses different rendering paths depending on the OS version (and different paths are possible even within the same version), here is how it works in the tested environments:

`Windows 10 22H2 19045.6466`:
```text
-3.	CComposition::Present
		CComposition *this				//rcx 1f87ae28960
		
-2.	CRenderTargetManager::Present (CRenderTargetManager::Present(*((CRenderTargetManager **)this + 11)))
		CRenderTargetManager *this		//rcx 1f87ae16f20	[CComposition+0x58]
	
	v12 = [1f87ae16f20 + 0x8] = 1F87EA15F80
	v15 = *(_QWORD *)v12 + *(int *)(*(_QWORD *)(*(_QWORD *)v12 + 0x48i64) + 0xC) + 0x48;
	v16 = (*(__int64 (__fastcall **)(__int64, __int64))(*(_QWORD *)v15 + 0x20i64))(v15, a2);
		[1f87ae16f20 + 0x8] = 1F87EA15F80
		[1F87EA15F80] 		= 1F87EA2D370
		[1F87EA2D370 + 48]	= 7FFA2D9EC7B0
		[7FFA2D9EC7B0 + C]	= 48A0
		1F87EA2D370 + 48A0 + 48 = 1f87ea31c58
-1.	IRenderTarget.VFTable[4] //called in cycle for each IRenderTarget
	IRenderTarget::Present [thunk] (return CLegacyRenderTarget::Present((CLegacyRenderTarget *)(a1 - *(int *)(a1 - 4)), a2))
		IRenderTarget* this				//rcx 1f87ea31c58
	v12 += 8i64 //take next
	
0.	CLegacyRenderTarget::Present
		CLegacyRenderTarget *this		//rcx 1f87ea31c58	IRenderTarget - [IRenderTarget-0x4]

1.	COverlayContext::Present (COverlayContext::Present((COverlayContext *)((char *)this - 18192), *((IOverlaySwapChain **)this - 2313), v15, &v22, v7, disableMpo))
		COverlayContext* this			//rcx 1f87ea2d548	CLegacyRenderTarget-0x4710
		IOverlaySwapChain* swapChain	//rdx 1f87e4d21a0	[CLegacyRenderTarget-0x4848]

2.	COverlayContext::PresentMPO
		COverlayContext* this			//rcx 1f87ea2d548
		IOverlaySwapChain* swapChain	//rdx 1f87e4d21a0

3.	IOverlaySwapChain.VFTable[18]
	CLegacySwapChain::PresentMPO [thunk] (return CLegacySwapChain::PresentMPO((CLegacySwapChain *)((char *)swapChain - *((int *)swapChain - 1)), a2, a3, a4);) // 1f87e4d21a0-[1f87e4d21a0-4] = 1f87e4d21a0 
		CLegacySwapChain* swapChain		//rcx 1f87e4d21a0

4.	CLegacySwapChain::PresentMPO
		CLegacySwapChain* this			//rcx 1f87e4d21a0

5.	CD3DDevice::PresentMPO (CD3DDevice::PresentMPO(*((CD3DDevice **)this - 36), *((struct IDXGISwapChainDWM1 **)this - 35), a2, a3))
		CD3DDevice* this				//rcx 1f87e91b600	[CLegacySwapChain-0x120]
		IDXGISwapChainDWM1* swapChain	//rdx 1f87e562fc8	[CLegacySwapChain-0x118]

6.	CD3DDrawingContext(CD3DDevice+0x108).VFTable[14]
	D2DDeviceContextBase<ID2D1DeviceContext6,ID2D1DeviceContext6,null_type>::PresentMultiplaneOverlay [thunk] (return D2DDeviceContextBase<ID2D1DCRenderTarget,ID2D1DCRenderTarget,ID2D1DeviceContext6>::PresentMultiplaneOverlay((DrawingContext *)((char *)drawingContext - 0x278), swapChain, a3, a4, a5, a6, a7, a8);)
		DrawingContext* context			//rcx 1f87c845da0	CD3DDevice+0x108
		IDXGISwapChainDWM1* swapChain	//rdx 1f87e562fc8
		
7.	D2DDeviceContextBase<ID2D1DCRenderTarget,ID2D1DCRenderTarget,ID2D1DeviceContext6>::PresentMultiplaneOverlay
		DrawingContext* context			//rcx 1f87c845b28	[CD3DDevice+0x108]-0x278
		IDXGISwapChainDWM1* swapChain	//rdx 1f87e562fc8

8.	DrawingContext::PresentMultiplaneOverlay
		DrawingContext *this			//rcx 1f87c845b28
		IDXGISwapChainDWM1 *swapChain	//rdx 1f87e562fc8
		
9.	IDXGISwapChainDWM1.VFTable[23] //aurenex/dwm-overlay hook is here
	CDXGISwapChainDWMLegacy::PresentMultiplaneOverlay [thunk] (CDXGISwapChain::PresentMultiplaneOverlay((CDXGISwapChain *)(*((_QWORD *)this + 8) + 88i64),a2,a3,a4,a5,a6,a7))
		CDXGISwapChainDWMLegacy *this	//rcx 1f87e562fc8

10.	CDXGISwapChain::PresentMultiplaneOverlay
		CDXGISwapChain *this			//rcx 1f87ea3a5d8	[CDXGISwapChainDWMLegacy+0x40]+0x58
```

I ran through practically the entire present path on `Windows 10` with MPO enabled (since it takes precedence). Here, you can clearly see where the `aurenex/dwm-overlay` hook is placed, and it also becomes apparent why this approach causes all the main issues.

Windows 11 uses the same route on every build, but why is it doesn't work only on `24H2+` with NVidia drivers?
Because NVidia forces `dwm` to use `CDDisplayRenderTarget` instead of `CLegacyRenderTarget`.
And we can see selector in `dwmcore.dll` inside `CRenderTargetManager::RenderAndPresent` function:

```cpp
CRenderTargetManager::SortMonitorTargets((CRenderTargetManager *)this, (__int64)&sortedMonitorTargetsVec);
sortedMonitorCurrent = (void *)sortedMonitorTargetsVec;
sortedMonitorEnd = v25;
while ( sortedMonitorCurrent != sortedMonitorEnd )
{
	drawingContext = this[86];
	renderTarget = *(_QWORD *)sortedMonitorCurrent + 8i64;
	renderAndPresent = *(__int64 (__fastcall **)(__int64, struct CDrawingContext *))(*(_QWORD *)renderTarget + 0x40i64);
	if ( renderAndPresent == CLegacyRenderTarget::RenderAndPresent )
	{
		renderAndPresentResult = CLegacyRenderTarget::RenderAndPresent(renderTarget, drawingContext); //legacy present path
	}
	else if ( (char *)renderAndPresent == (char *)CDDisplayRenderTarget::RenderAndPresent )
	{
		renderAndPresentResult = CDDisplayRenderTarget::RenderAndPresent(renderTarget, drawingContext); //modern present path
	}
	else
	{
		renderAndPresentResult = renderAndPresent(renderTarget, drawingContext);
	}
	
	rapHr = renderAndPresentResult;
	if ( renderAndPresentResult < 0 )
		MilInstrumentationCheckHR_MaybeFailFast(0x14u, &dword_18032D228, 3u, renderAndPresentResult, 0xBEu, 0i64);// dword_18032D228 dd 8898008Bh
		  
	if ( !v1 || v1 >= 0 && rapHr < 0 )
		v1 = rapHr;
		  
	sortedMonitorCurrent = (char *)sortedMonitorCurrent + 0x10;
}
    
detail::vector_facade<CRenderTargetManager::CSortedMonitorTarget,detail::buffer_impl<CRenderTargetManager::CSortedMonitorTarget,4,1,detail::liberal_expansion_policy>>::
	~vector_facade<CRenderTargetManager::CSortedMonitorTarget,detail::buffer_impl<CRenderTargetManager::CSortedMonitorTarget,4,1,detail::liberal_expansion_policy>>(&sortedMonitorTargetsVec);
```

So, I decided to use `COverlayContext::Present` like `chaosium43/dwm-overlay` because it used in both scenarious on both `Windows` builds and it's the most top-level easy maintain function i can see:

```text
CLegacyRenderTarget::Present / RenderAndPresent
    └─ COverlayContext::Present
           └─ CLegacySwapChain
                  └─ DXGI legacy present path

CDDisplayRenderTarget::Present / RenderAndPresent
    └─ COverlayContext::Present
           └─ CDDisplaySwapChain
                  └─ ddisplay-backed present path
```

---

## What do we need

In the context of hooking DWM, we need to obtain the render context to draw our own content, this requires four things:

- `ID3D11Device`
- `ID3D11DeviceContext`
- `ID3D11Texture2D`
- `ID3D11RenderTargetView`

Every `dwm` overlay project obtains this necessary information from various sources in its own way. For example, `aurenex/dwm-overlay` retrieves the device and texture from the `IDXGISwapChain`, whereas `chaosium43/dwm-overlay` gets them from internal `dwm` structures - accessing the first via an offset and the second by calling a virtual function.

Experimental testing in my environments has shown that both methods can be streamlined by obtaining just one key element - the `ID3D11Resource`, from which all the other components can be derived, thereby minimizing dependencies.

At first I looked up for direct getters and found only this:

```text
180064000 CLegacySwapChainBuffer::GetD3D11Resource(void)
180200800 CDDisplaySwapChainBuffer::GetD3D11Resource(void)
```

But what is a `SwapChainBuffer` are? So, as i can see, this is something of internal `dwm` wrapper around some other things, anyway, we need to somehow get this object instance. And for lucky us, there is another direct getter now in our `IOverlaySwapChain` that we gets in `COverlayContext::Present` as second parameter: 

```text
180063E70 CLegacySwapChain::GetPhysicalBackBuffer(void)
1801EEA40 CDDisplaySwapChain::GetPhysicalBackBuffer(void)
```

so, all we need it's to get these two vftable offsets, which must be the same in one environment and then just do:

```cpp
IOverlaySwapChain->GetPhysicalBackBuffer()->GetD3D11Resource();
```

Okay, it's clear enough for `Windows 11`, but `Windows 10` lacks those methods and functions - there are no direct resource getters, no intermediate objects, nothing. However, I noticed that the rendering path does utilize `DXGISwapChain` - specifically within `CLegacySwapChain::PresentMPO`, and on `Windows 10`, the legacy path is used regardless of whether it's an Nvidia GPU or not, so, we can simply extract the `DXGISwapChain` just as `dwm` does.

And there is we split our present path to two:
- Legacy present path
  - works through IDXGISwapChainDWM
- Modern present path
  - works through IOverlaySwapChainBuffer

also, we need two `COverlayContext::Present` ABIs, for `Windows 10` and `Windows 11`, because they're little different:

`Windows 10 22H2:`
```cpp
HRESULT __fastcall COverlayContext::Present(
    COverlayContext* this,
    IOverlaySwapChain* swap_chain,
    ui32_t a3,
    const std::vector<RECT>& dirty_rects,
    ui32_t a5,
    bool legacy_present);
```

`Windows 11 25H2:`
```cpp
HRESULT __fastcall COverlayContext::Present(
    COverlayContext* this,
    IOverlaySwapChain* swap_chain,
    ui32_t a3,
    const std::vector<RECT>& dirty_rects,
    ui32_t a5,
    bool* a6,
    bool legacy_present);
```

Okay, at this stage we can already build an experimental version capable of rendering something, but for a proper targeted overlay, we also need additional information - at least, the desktop rectangle. Given the data we have so far, this is simply impossible if we need to account for all screen modes include **duplicate** and **extend**.

We can't simply map all of this together because there is no direct dependency chain from `device` / `device_context` / `resource` / `render_target_view` to `dxgi_output`, and heuristic mappings are essentially guesswork with varying degrees of success.

Here we need at least two things: the source path VidPN ID and the target path VidPN ID. I won't go into the details of how `QueryDisplayConfig` works on `Windows`, but to reliably obtain the necessary information, we need both of these IDs regardless. Of course, it is possible to get by with just one - the target VidPN ID, especially since `Windows 11` `dwmcore` has:

```text
1801ED7E0 CLegacySwapChain::GetVidPnTargetId(void)
1802BF4D0 COverlaySwapChain::GetVidPnTargetId(void)
```

However, `Windows 10` unfortunately has only:

```text
1800E2510 CDDisplaySwapChain::GetVidPnSourceId(void)
1800E3798 CLegacySwapChain::GetVidPnSourceId(void)
```

So, a simple cross-platform solution wasn't immediately apparent, but I did examine the structures at runtime.

### Runtime research
![Runtime research](.screenshots/wdwmo-dxgioutput-dynamic.png)

1. `COverlayContext*` for both monitors, get from `rcx` at `COverlayContext::Present` call.
2. Its `C...RenderTarget*`, expectedly stored at `0x0`.
3. `const ATL::CComObject<class CDXGIOutput>::``vftable'{for ``IDXGIOutputDWM'}`, also expectedly stored at `0x0` in this structure.
4. Output's GDI Display name.
5. Output's source VidPN ID.
6. Output's target VidPN ID.

And it turns out all the necessary information is right here.

I tried to see what reads these addresses - containing the target/source VidPN ID, but on `Windows 10`, nothing seems to need them at all, at least not when cycling through configurations like `duplicate` -> `extend` -> `duplicate`. So, I decided to examine the object's vftable, and I found something interesting there.

![DXGIOutputDWM VFTable](.screenshots/wdwmo-dxgioutput-vftable.png)

The `GetDesc` function, returning `DXGI_OUTPUT_DWM_DESC`. Since `dxgi.pdb` doesn't provides this structure layout I needed to know it manually, so, i've just traced calls of this function.

### Before executing `GetDesc` function:
![DXGIOutputDWM GetDesc result trace, before](.screenshots/wdwmo-dxgioutput-getdesc-caller.png)

1. Calling of `GetDesc`.
2. 1st argument, e.g. `DXGIOutputDWM*` (`this`).
3. 2nd argument, e.g. `DXGI_OUTPUT_DWM_DESC*` (`result`). Perfectly matches with caller code - `lea rdx, [rdi+20]`. Points to stack.

Because `rdx` is a stack pointer, I just viewed stack.

### Right after `GetDesc` execution:
![DXGIOutputDWM GetDesc result trace, after](.screenshots/wdwmo-dxgioutput-getdesc-after.png)

1. Function at where is tracing now.
2. Changed values at stack.

It is clearly evident that this function - `GetDesc` returns all the information we need. So, only a few small steps remain: obtain its address, call it, and extract the required values from the result.

Later I re-verified this using a slightly different approach - allocating a buffer of N length and filling it with garbage values to see exactly what changed, thereby allowing for a more accurate mapping of the layout.

It is also clearly visible that this object inherits from `IUnknown`, therefore, instead of determining the offsets by analyzing the `PDB`, we can simply declare it - along with `DXGI_OUTPUT_DWM_DESC`.

The only difference in both OS lies in the `DXGI_OUTPUT_DWM_DESC` structure, but that can be accounted for as well.

And, we need correctly defined `IDXGIOutputDWM`, including its guid.
To obtain its guid, I had to take a quick look at the `QueryInterface` implementation in that vftable, locate the associated ATL COM map, and find the required entry.

## Restored DXGI undocumented internals

Of course, these aren't exact replicas of `dwm's` valid internal structures, but they are accurate for each of the specified environments; to use them, all that was needed was to tinker a bit with the union and declare them correctly.

```cpp
//win10 22h2 19045 / win11 25h2 26200; 0x24
typedef struct _DXGI_OUTPUT_DWM_DESC_BASE {
	LUID adapterId;

	UINT sourceVidPnId;
	UINT targetVidPnId;

	UINT unknown0;
	UINT unknown1;
	UINT unknown2;

	INT width, height;
}DXGI_OUTPUT_DWM_DESC_BASE;

//win10 22h2 19045; 0xac, expanded to 0x100
typedef union _DXGI_OUTPUT_DWM_DESC_WIN10_22H2 {
	struct {
		_DXGI_OUTPUT_DWM_DESC_BASE base;

		UINT unknown3;
		UINT unknown4;
		UINT unknown5;
		UINT unknown6;
		UINT unknown7;

		RECT workingRect;
		RECT desktopRect;

		UINT unknown8;

		WCHAR deviceName[32];

		UINT unknown9;
		UINT unknown10;
		UINT unknown11;
		UINT unknown12;
	};

	BYTE raw[0x180] = { };
}DXGI_OUTPUT_DWM_DESC_WIN10;

//win11 25h2 26200; 0xc8, expanded to 0x100
typedef union _DXGI_OUTPUT_DWM_DESC_WIN11_25H2 {
	struct {
		_DXGI_OUTPUT_DWM_DESC_BASE base;

		UINT unknown3;
		UINT unknown4;
		UINT unknown5;
		UINT unknown6;
		UINT unknown7;

		UINT unknown8;
		UINT unknown9;
		UINT unknown10;
		UINT unknown11;
		UINT unknown12;

		RECT workingRect;
		RECT desktopRect;

		UINT unknown13;

		WCHAR deviceName[32];

		UINT unknown14;
		UINT unknown15;
		UINT unknown16;
		UINT unknown17;
		UINT unknown18;
		UINT unknown19;
	};

	BYTE raw[0x180] = { };
}DXGI_OUTPUT_DWM_DESC_WIN11;

typedef union _DXGI_OUTPUT_DWM_DESC {
	DXGI_OUTPUT_DWM_DESC_BASE base;

	DXGI_OUTPUT_DWM_DESC_WIN10 win10;
	DXGI_OUTPUT_DWM_DESC_WIN11 win11;

	BYTE raw[0x180] = { };
}DXGI_OUTPUT_DWM_DESC;
```

And of course restored `IDXGIOutputDWM` with its guid and necessary methods:

```cpp
MIDL_INTERFACE("6f66a9a0-bece-4ee8-b11b-990eb38ed976")
IDXGIOutputDWM : public IUnknown{
	virtual BOOL STDMETHODCALLTYPE HasDDAClient() = 0;

	virtual HRESULT STDMETHODCALLTYPE GetDesc(
		DXGI_OUTPUT_DWM_DESC* pDesc
	) = 0;
};
```

Also, we need corretcly defined `IDXGISwapChainDWM`, that was getted from `aurenex/dwm-overlay` repository.

```cpp
MIDL_INTERFACE("f69f223b-45d3-4aa0-98c8-c40c2b231029")
IDXGISwapChainDWMLegacy : public IDXGIDeviceSubObject{
	virtual HRESULT STDMETHODCALLTYPE Present(
		UINT syncInterval,
		UINT flags
	) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetBuffer(
		UINT buffer,
		REFIID riid,
		void** ppSurface
	) = 0;
};
```

---

## Project dependencies

Now that we have sorted out the complete set of what exactly we need for the job, we can consider how to obtain it. Naturally, each item requires its own offset - some are found in various structures, others in files, and some in virtual function tables and here they all are:

| Name | Type | Kind | Target |
| --- | --- | --- | --- |
| `present` | rva | static | `COverlayContext::Present` |
| `render_target` | struct | static | `COverlayContext` -> `C...RenderTarget` |
| `dxgi_output_vftable` | rva | static | `const ATL::CComObject<class CDXGIOutput>::``vftable'{for ``IDXGIOutputDWM'}` |
| `dxgi_output` | struct | runtime | `C...RenderTarget` -> `IDXGIOutputDWM` |
| `dxgi_swap_chain` | struct | static | `IOverlaySwapChain` -> `IDXGISwapChainDWM` |
| `get_physical_back_buffer` | vftable | static | `IOverlaySwapChain::VFTable` -> `GetPhysicalBackBuffer` |
| `get_d3d11_resource` | vftable | static | `ISwapChainBuffer::VFTable` -> `GetD3D11Resource` |

and their purpose:

- `present` - `dwmcore.dll` rva offset to main hook target.

- `render_target` - offset from `COverlayContext` instance to `CLegacy-/CDDisplay-RenderTarget`, we need this to correctly identify the physical output monitor - specifically its logical rect, VidPN ID, friendly name and GDI name.

- `dxgi_output_vftable` - `dxgi.dll` rva offset to vftable - which is located at the beginning of the DXGIOutputDWM object, is needed to determine which object is the one.

- `dxgi_output` - offset from `CLegacy-/CDDisplay-RenderTarget` to field containing `IDXGIOutputDWM*`. It is harder to determine it statically than to obtain it at runtime.

- `dxgi_swap_chain` - offset from `IOverlaySwapChain` to field containing `IDXGISwapChainDWM`, so we can easily work with it in legacy present path configurations.

- `get_physical_back_buffer` - `IOverlaySwapChain` vftable offset to matching function for retrieving nextly `ID3D11Resource`.

- `get_d3d11_resource` - `IOverlaySwapChainBuffer` vftable offset to matching function for retrieving `ID3D11Resource`.

And all of this can be determined automatically via static analysis.

---

## Project architecture

The project is designed as a static link library - with a core you won't need to modify often - intended for use via callbacks.

The project consists of three parts:

- `wdwmo` (`Windows DWM Overlay`): The project core - a module loaded directly into `dwm.exe`. The `debug` build includes a sample implementation featuring an **ImGui** window that displays primary information.

- `wdwmcd` (`Windows DWM Core Dumper`): A module responsible for automatically retrieving all necessary offsets and analyzing `dwmcore.dll` and `dxgi.dll` using `pdb` files.

- `wdwmosl` (`Windows DWM Overlay Sample Loader`): A sample loader project; it retrieves offsets from system libraries or those specified by the user and loads either a custom or standard debug `wdwmo` build into `dwm.exe`.

All three components have dependencies, some of which are missing - specifically `ncore`, which I do not plan to include in the project at this stage. If you need to build the project, you will have to replace all instances of `ncore` with your own equivalents. This is not difficult, as there are few such instances, but keep in mind that `ncore::disassembly` is a wrapper around **BeaEngine**.

### Toolchain

- `Windows x64`.
- `Visual Studio` / `MSVC`.
- `C++20`.
- `Windows SDK` with `D3D11`, `DXGI`, `SHCore`, and related headers/libraries.

The reverse-engineering logic and wrapper ABI are x64-specific.

### Included/external components

| Component | Used by | Repository form | License |
|---|---|---|---|
| MinHook | `wdwmo` | header and static library | BSD 2-Clause-style notices, including HDE32/HDE64 |
| Direct3D 11 / DXGI and Windows APIs | `wdwmo` and sample | Windows SDK/system components | Microsoft terms |
| raw_pdb | `wdwmcd` | bundled source | BSD 2-Clause |
| BeaEngine | `wdwmcd` | bundled static library | LGPL-3.0-or-later |
| Dear ImGui 1.92.8 | Debug sample only | bundled source | MIT |
| cpr | `wdwmosl` networking layer | bundled static library | MIT |
| curl / libcurl 7.86.0-DEV | `wdwmosl` networking layer | bundled static library | curl license |
| zlib 1.2.13 | libcurl compression support | bundled static library | zlib license |
| ncore | project utilities | private author-owned utilities | stated separately when distributed |

### No DIA SDK or DbgHelp requirement

Unlike implementations that delegate symbol handling to **DIA SDK** or **DbgHelp**, `wdwmcd` parses the supplied PDB data directly through **raw_pdb**. The core/dumper design therefore does not require a separately installed DIA runtime or a DbgHelp-based symbol session.

The project still uses normal `Windows` system libraries and statically linked helper libraries where appropriate; "no dynamic dependencies" here means no additional external symbol-engine DLL requirement.

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

## Licensing

The original project code is released under the [MIT License](.license.txt):

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
