#pragma once
#include "types.hpp"

namespace wdwmo {
    using namespace types;

    struct configuration_t {
        struct {
            executable_image_info_t dwmcore = { }, dxgi = { };
        }image_info = { };

        struct {
            i32_t present = { };                    //rva       static  COverlayContext::Present

            i32_t render_target = { };              //struct    static  COverlayContext -> C...RenderTarget
            i32_t dxgi_output_vftable = { };        //rva       static  ??_7?$CComObject@VCDXGIOutput@@@ATL@@6BIDXGIOutputDWM@@@ | const ATL::CComObject<class CDXGIOutput>::`vftable'{for `IDXGIOutputDWM'}
            i32_t dxgi_output = { };                //struct    runtime C...RenderTarget -> IDXGIOutputDWM

            i32_t dxgi_swap_chain = { };            //struct    static  IOverlaySwapChain -> DXGISwapChainDWM

            i32_t get_physical_back_buffer = { };   //vft       static  IOverlaySwapChain::VFTable -> GetPhysicalBackBuffer
            i32_t get_d3d11_resource = { };         //vft       static  ISwapChainBuffer::VFTable -> GetD3D11Resource


            __forceinline constexpr bool dxgi_output_vftable_known() const noexcept {
                return dxgi_output_vftable;
            }

            //dxgi swap chain path
            __forceinline constexpr bool is_legacy_available() const noexcept {
                return dxgi_swap_chain;
            }

            //swap chain buffer and d3d11 resource path
            __forceinline constexpr bool is_modern_available() const noexcept {
                return get_physical_back_buffer && get_d3d11_resource;
            }
        }offsets = { };
    };
}