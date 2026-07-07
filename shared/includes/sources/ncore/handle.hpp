#pragma once

#ifndef _NCORE_HANDLE
#define _NCORE_HANDLE
#endif

namespace ncore {
	namespace types {
		using id_t = unsigned long;
	}

	using namespace types;

	namespace handle {
		using native_t = void*;

		template<typename _t = native_t> class handle_t {
		public:
			using closer_t = void(*)(_t value);

		private:
			_t _value;
			closer_t _closer;
			bool _close_on_destroy;

		public:
			__forceinline constexpr handle_t(const _t value = _t(), closer_t closer = nullptr, bool close_on_destroy = false) {
				_value = value;
				_closer = closer;
				_close_on_destroy = close_on_destroy;
			}

			__forceinline constexpr ~handle_t() {
				if (!(_value && _close_on_destroy)) return;

				close();
			}

			__forceinline bool close_on_destroy() {
				return _close_on_destroy;
			}

			__forceinline bool close_on_destroy(bool state) {
				return !(_close_on_destroy = state);
			}

			__forceinline constexpr _t get() const noexcept {
				return _value;
			}

			__forceinline void close() noexcept {
				auto value = _value;

				_value = nullptr;
				_close_on_destroy = false;

				if (_closer && value) return _closer(value);
			}

			__forceinline constexpr bool operator!() const noexcept {
				return !_value;
			}

			__forceinline constexpr bool operator==(const _t value) const noexcept {
				return _value == value;
			}
		};

		using native_handle_t = handle_t<native_t>;
	}
}
