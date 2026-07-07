#pragma once

namespace ncore {
	namespace types {
		using i8_t = __int8;
		using i16_t = __int16;
		using i32_t = __int32;
		using i64_t = __int64;
		using i8_p = i8_t*;
		using i16_p = i16_t*;
		using i32_p = i32_t*;
		using i64_p = i64_t*;

		using ui8_t = unsigned __int8;
		using ui16_t = unsigned __int16;
		using ui32_t = unsigned __int32;
		using ui64_t = unsigned __int64;
		using ui8_p = ui8_t*;
		using ui16_p = ui16_t*;
		using ui32_p = ui32_t*;
		using ui64_p = ui64_t*;

		using byte_t = ui8_t;
		using byte_p = byte_t*;

		using sbyte_t = i8_t;
		using sbyte_p = sbyte_t*;

		using lssize_t = i32_t;
		using lsize_t = ui32_t;

		using ssize_t = i64_t;
		using ssize_p = ssize_t*;

		using size_t = ui64_t;
		using size_p = size_t*;

		using scount_t = i64_t;
		using count_t = ui64_t;

		using sindex_t = i64_t;
		using index_t = ui64_t;

		using soffset_t = i64_t;
		using offset_t = ui64_t;
		using loffset_t = ui32_t;

		using address_t = void*;
		using address_p = address_t*;

		template<typename _t, bool _minEquals = true, bool _maxEquals = true> struct limit {
			_t min, max;
			limit* second;
			char second_comparsion_type;

			__forceinline constexpr limit(const _t& min = _t(), const _t& max = _t(), limit* second = nullptr, char second_comparsion_type = '&') noexcept {
				this->min = min;
				this->max = max;
				this->second = second;
				this->second_comparsion_type = second_comparsion_type;
			}

			__forceinline constexpr auto in_range(const _t& value, bool check_second = true) const noexcept {
				auto result = false;

				if constexpr (_minEquals) {
					result = *((_t*)&value) >= min;
				}
				else {
					result = *((_t*)&value) > min;
				}
				if (!result) return false;

				if constexpr (_maxEquals) {
					result = *((_t*)&value) <= max;
				}
				else {
					result = *((_t*)&value) < max;
				}
				if (!result) return false;

				if (second && check_second) switch (second_comparsion_type) {
				case '&': default: result = result && second->in_range(value); break;
				case '|': result = result || second->in_range(value); break;
				}

				return result;
			}

			__forceinline constexpr const auto difference() const noexcept {
				return max - min;
			}

			__forceinline constexpr auto& operator[](bool get_max) noexcept {
				return get_max ? max : min;
			}

			__forceinline constexpr const auto& operator[](bool get_max) const noexcept {
				return get_max ? max : min;
			}

			__forceinline constexpr auto begin() noexcept {
				return min;
			}

			__forceinline constexpr auto end() noexcept {
				return max;
			}

			__forceinline constexpr const auto begin() const noexcept {
				return min;
			}

			__forceinline constexpr const auto end() const noexcept {
				return max;
			}
		};

		template<typename _t> using bound = limit<_t, true, true>;
		template<typename _t> using range = limit<_t, false, false>;

		template<typename part_t, typename data_t = void*> struct aligned {
			using callback_t = void(*)(index_t index, part_t value, data_t data);

			part_t low, high, divided;
			count_t count;

			__forceinline constexpr aligned(part_t value, part_t step, callback_t callback, data_t data = data_t()) noexcept {
				if (!callback) return;

				count = count_t();
				divided = step;

				auto remaining = value;
				for (; remaining > step; remaining -= step, high += step, count++) {
					callback(count, step, data);
				}

				if (low = remaining) {
					callback(count, remaining, data);
				}
			}

			__forceinline constexpr aligned(part_t number, part_t maximal) noexcept {
				if (maximal) {
					low = number % maximal;
					high = number - low;
					count = high / maximal;
					divided = high / count;

					return;
				}

				low = number;
				high = part_t();
				count = count_t();
				divided = part_t();
			}

			__forceinline constexpr auto part() const noexcept {
				return divided;
			}
		};
	}

	using namespace types;
}

#ifdef NCORE_TYPES_GLOBAL
using namespace ncore::types;
#endif