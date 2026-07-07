#pragma once
extern"C" void* _ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)

#define __return_address _ReturnAddress()

#define CURRENT_PROCESS_HANDLE ((void*)(-1))
#define CURRENT_THREAD_HANDLE ((void*)(-2))

#define null (0)
#define NULL (0)

#define __b(NUM) size_t(NUM)				//byte
#define __kb(NUM) size_t(__b(1024) * NUM)	//kilo
#define __mb(NUM) size_t(__kb(1024) * NUM)	//mega
#define __gb(NUM) size_t(__mb(1024) * NUM)	//giga
#define __tb(NUM) size_t(__gb(1024) * NUM)	//tera
#define __pb(NUM) size_t(__tb(1024) * NUM)	//peta

#define sizeofarr(ARRAY) (sizeof(ARRAY) / sizeof(*(ARRAY)))
#define size_of_array sizeofarr

#define begin_unaligned _Pragma("pack(push, 1)")
#define end_unaligned _Pragma("pack(pop)")
#define BEGIN_UNALIGNED begin_unaligned
#define END_UNALIGNED end_unaligned

#define typeof decltype

#if defined(_XTR1COMMON_)
namespace ncore::types {
	template<typename _t> using remove_refp_t = std::remove_reference_t<std::remove_pointer_t<_t>>;
	template<typename _t> using remove_cvrefp_t = std::remove_cvref_t<std::remove_pointer_t<_t>>;
}
#define typeofr(TYPE) std::remove_cv_t<decltype(TYPE)> //raw
#define typeofn(TYPE) ncore::remove_refp_t<decltype(TYPE)> //naked
#define typeofrn(TYPE) ncore::remove_refp_t<std::remove_cv_t<decltype(TYPE)>> //raw naked
#endif

#define __tryif(STATE) if (STATE) __try
#define __catch __except(true)
#define __endtry __catch { }

#define __likely [[likely]]
#define __unlikely [[unlikely]]

#define __deprecated(WHY) [[deprecated(WHY)]]

#include "types.hpp"

namespace ncore {
	static __forceinline constexpr bool is_little_endian(ui64_t value) noexcept {
		const auto bytes = (const ui8_t*)(&value);
		return *bytes == (value & 0xFFui8);
	}

	static __forceinline constexpr bool is_big_endian(ui64_t value) noexcept {
		const auto bytes = (const ui8_t*)(&value);
		return *bytes != (value & 0xFFui8);
	}

	static __forceinline constexpr ui64_t swap_endian(ui64_t value) noexcept {
		return ((value & 0xFF00000000000000ui64) >> 56) |
			((value & 0x00FF000000000000ui64) >> 40) |
			((value & 0x0000FF0000000000ui64) >> 24) |
			((value & 0x000000FF00000000ui64) >> 8) |
			((value & 0x00000000FF000000ui64) << 8) |
			((value & 0x0000000000FF0000ui64) << 24) |
			((value & 0x000000000000FF00ui64) << 40) |
			((value & 0x00000000000000FFui64) << 56);
	}

	static __forceinline constexpr ui32_t swap_endian(ui32_t value) noexcept {
		return ((value & 0xFF000000ui32) >> 24) |
			((value & 0x00FF0000ui32) >> 8) |
			((value & 0x0000FF00ui32) << 8) |
			((value & 0x000000FFui32) << 24);
	}

	static __forceinline constexpr ui16_t swap_endian(ui16_t value) noexcept {
		return ((value & 0xFF00ui16) >> 8) |
			((value & 0x00FFui16) << 8);
	}

	static __forceinline constexpr ui8_t swap_endian(ui8_t value) noexcept {
		return value;
	}
}
