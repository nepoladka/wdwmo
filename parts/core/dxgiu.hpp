#pragma once
//dxgi undocummented features

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


MIDL_INTERFACE("6f66a9a0-bece-4ee8-b11b-990eb38ed976")
IDXGIOutputDWM : public IUnknown{
	virtual BOOL STDMETHODCALLTYPE HasDDAClient() = 0;

	virtual HRESULT STDMETHODCALLTYPE GetDesc(
		DXGI_OUTPUT_DWM_DESC* pDesc
	) = 0;
};

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