#pragma once
#include <cstdarg>

namespace ncore {
	namespace types {
		template<typename _t, size_t _capacity> class static_array {
		protected:
			_t _values[_capacity];

		public:
			__forceinline constexpr static_array(const _t data[_capacity] = nullptr, size_t length = _capacity) noexcept {
				set(data, length);
			}

			__forceinline static_array(const _t data, ...) noexcept {
				va_list list;
				va_start(list, _capacity);

				for (index_t i = 0; i < _capacity; i++) {
					_values[i] = va_arg(list, _t);
				}

				va_end(list);
			}

			__forceinline constexpr const void set(const _t data, ...) noexcept {
				va_list list;
				va_start(list, _capacity);

				for (index_t i = 0; i < _capacity; i++) {
					_values[i] = va_arg(list, _t);
				}

				va_end(list);
			}

			__forceinline const void set(const _t data[_capacity], size_t length) noexcept {
				for (index_t i = 0; i < _capacity; i++) {
					__try {
						_values[i] = (data && i < length) ? data[i] : _t();
					} __catch{
						break;
					}
				}
			}

			__forceinline constexpr _t* data() noexcept {
				return _values;
			}

			__forceinline constexpr const _t* data() const noexcept {
				return _values;
			}

			__forceinline constexpr size_t size() const noexcept {
				return _capacity;
			}

			__forceinline constexpr size_t capacity() const noexcept {
				return _capacity;
			}

			__forceinline constexpr _t* begin() noexcept {
				return _values;
			}

			__forceinline constexpr const _t* begin() const noexcept {
				return _values;
			}

			__forceinline constexpr _t* end() noexcept {
				return _values + _capacity;
			}

			__forceinline constexpr const _t* end() const noexcept {
				return _values + _capacity;
			}

			__forceinline auto length(_t ending = _t()) const noexcept {
				auto result = size_t(0);

				for (auto& item : _values) {
					if (item == ending) break;
					result++;
				}

				return result;
			}

			__forceinline auto count(_t ending = _t()) const noexcept {
				return count_t(length(ending));
			}

			__forceinline constexpr auto& at(size_t index) noexcept {
				return _values[index];
			}

			__forceinline constexpr const auto& at(size_t index) const noexcept {
				return _values[index];
			}

			__forceinline constexpr const void copy(static_array& destination) const noexcept {
				for (auto i = index_t(), j = __min(capacity(), destination.capacity()); i < j; i++) {
					destination._values[i] = _values[i];
				}
			}

			__forceinline constexpr _t& operator[](size_t index) noexcept {
				return at(index);
			}

			__forceinline constexpr const _t& operator[](size_t index) const noexcept {
				return at(index);
			}

			__forceinline bool operator==(const static_array& second) const noexcept {
				for (index_t i = 0; i < _capacity; i++)
					if (_values[i] != second._values[i]) return false;

				return true;
			}

			__forceinline bool operator!=(const static_array& second) const noexcept {
				return !this->operator==(second);
			}

			__forceinline constexpr _t& operator*() noexcept {
				return _values[0];
			}
		};

	}

	using namespace types;
}
