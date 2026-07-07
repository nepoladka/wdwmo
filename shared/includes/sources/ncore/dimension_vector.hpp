#pragma once
#define VEC_NORMALIZE(VAL, MIN, MAX) (((VAL) - (MIN)) / ((MAX) - (MIN)))

#define NCORE_VEC_TYPE(NUM)							\
template<typename _t> using vec##NUM = vec<_t, NUM>;\
typedef vec##NUM<__int8> vec##NUM##i8;              \
typedef vec##NUM<unsigned __int8> vec##NUM##ui8;    \
typedef vec##NUM<__int16> vec##NUM##i16;            \
typedef vec##NUM<unsigned __int16> vec##NUM##ui16;  \
typedef vec##NUM<__int32> vec##NUM##i32;            \
typedef vec##NUM<unsigned __int32> vec##NUM##ui32;  \
typedef vec##NUM<__int64> vec##NUM##i64;            \
typedef vec##NUM<unsigned __int64> vec##NUM##ui64;  \
typedef vec##NUM<float> vec##NUM##f;                \
typedef vec##NUM<long float> vec##NUM##lf;

namespace ncore {
    namespace types {
		template<typename _t, __int64 _dimension>
		struct vec_storage {
			_t array[_dimension] = { };

			__forceinline constexpr vec_storage() noexcept = default;

			template<typename... arguments> requires (sizeof...(arguments) == _dimension)
				__forceinline constexpr vec_storage(arguments... args) noexcept
				: array{ static_cast<_t>(args)... } {
				return;
			}
		};

		template<typename _t>
		struct vec_storage<_t, 1> {
			union {
				struct {
					_t x;
				};

				_t array[2];
			};

			__forceinline constexpr vec_storage() noexcept : array{ } {
				return;
			}

			__forceinline constexpr vec_storage(const _t& x) noexcept
				: x(x) {
				return;
			}
		};

		template<typename _t>
		struct vec_storage<_t, 2> {
			union {
				struct {
					_t x, y;
				};

				_t array[2];
			};

			__forceinline constexpr vec_storage() noexcept : array{ } {
				return;
			}

			__forceinline constexpr vec_storage(const _t& x, const _t& y) noexcept
				: x(x), y(y) {
				return;
			}
		};

		template<typename _t>
		struct vec_storage<_t, 3> {
			union {
				struct {
					_t x, y, z;
				};

				_t array[3];
			};

			__forceinline constexpr vec_storage() noexcept : array{ } {
				return;
			}

			__forceinline constexpr vec_storage(const _t& x, const _t& y, const _t& z) noexcept
				: x(x), y(y), z(z) {
				return;
			}
		};

		template<typename _t>
		struct vec_storage<_t, 4> {
			union {
				struct {
					_t x, y, z, w;
				};

				_t array[4];
			};

			__forceinline constexpr vec_storage() noexcept : array{ } {
				return;
			}

			__forceinline constexpr vec_storage(const _t& x, const _t& y, const _t& z, const _t& w) noexcept
				: x(x), y(y), z(z), w(w) {
				return;
			}
		};

		template<typename _t, __int64 _dimension>
		struct vec : vec_storage<_t, _dimension> {
			using base_t = vec_storage<_t, _dimension>;
			using value_t = _t;

			static inline constexpr const auto __dimension = _dimension;

			__forceinline constexpr vec() noexcept = default;

			template<typename... arguments> requires (sizeof...(arguments) == _dimension)
				__forceinline constexpr vec(arguments... args) noexcept
				: base_t(static_cast<_t>(args)...) {
				return;
			}

			__forceinline constexpr vec(const _t (&arr)[_dimension]) noexcept {
				for (auto i = 0i64; i < _dimension; i++) {
					this->array[i] = arr[i];
				}
			}

			__forceinline constexpr _t* data() noexcept {
				return this->array;
			}

			__forceinline constexpr const _t* data() const noexcept {
				return this->array;
			}

			__forceinline constexpr _t* begin() noexcept {
				return this->array;
			}

			__forceinline constexpr _t* end() noexcept {
				return this->array + _dimension;
			}

			__forceinline constexpr const _t* begin() const noexcept {
				return this->array;
			}

			__forceinline constexpr const _t* end() const noexcept {
				return this->array + _dimension;
			}

			//__forceinline constexpr _t* operator&() noexcept {
			//	return array;
			//}

			//__forceinline constexpr const _t* operator&() const noexcept {
			//	return array;
			//}

			__forceinline constexpr _t& operator[](__int64 idx) noexcept {
				return this->array[idx];
			}

			__forceinline constexpr const _t& operator[](__int64 idx) const noexcept {
				return this->array[idx];
			}

			__forceinline constexpr bool operator==(const vec& right) const noexcept {
				for (auto i = 0i64; i < _dimension; i++) {
					if (this->array[i] != right.array[i]) return false;
				}

				return true;
			}

			__forceinline constexpr bool operator!=(const vec& right) const noexcept {
				return !(*this == right);
			}

			__forceinline constexpr vec& operator+=(const vec& right) noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					this->array[i] += right.array[i];
				}

				return *this;
			}

			__forceinline constexpr vec& operator-=(const vec& right) noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					this->array[i] -= right.array[i];
				}

				return *this;
			}

			__forceinline constexpr vec& operator*=(const vec& right) noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					this->array[i] *= right.array[i];
				}

				return *this;
			}

			__forceinline constexpr vec& operator/=(const vec& right) noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					this->array[i] /= right.array[i];
				}

				return *this;
			}

			__forceinline constexpr vec operator+(const vec& right) const noexcept {
				auto result = *this;
				result += right;
				return result;
			}

			__forceinline constexpr vec operator-(const vec& right) const noexcept {
				auto result = *this;
				result -= right;
				return result;
			}

			__forceinline constexpr vec operator*(const vec& right) const noexcept {
				auto result = *this;
				result *= right;
				return result;
			}

			__forceinline constexpr vec operator/(const vec& right) const noexcept {
				auto result = *this;
				result /= right;
				return result;
			}

			__forceinline constexpr vec& operator+=(const _t& value) noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					this->array[i] += value;
				}

				return *this;
			}

			__forceinline constexpr vec& operator-=(const _t& value) noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					this->array[i] -= value;
				}

				return *this;
			}

			__forceinline constexpr vec& operator*=(const _t& value) noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					this->array[i] *= value;
				}

				return *this;
			}

			__forceinline constexpr vec& operator/=(const _t& value) noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					this->array[i] /= value;
				}

				return *this;
			}

			__forceinline constexpr vec operator+(const _t& value) const noexcept {
				auto result = *this;
				result += value;
				return result;
			}

			__forceinline constexpr vec operator-(const _t& value) const noexcept {
				auto result = *this;
				result -= value;
				return result;
			}

			__forceinline constexpr vec operator*(const _t& value) const noexcept {
				auto result = *this;
				result *= value;
				return result;
			}

			__forceinline constexpr vec operator/(const _t& value) const noexcept {
				auto result = *this;
				result /= value;
				return result;
			}

			__forceinline constexpr bool operator<(const vec& right) const noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					if (!(this->array[i] < right.array[i])) return false;
				}
				return true;
			}

			__forceinline constexpr bool operator>(const vec& right) const noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					if (!(this->array[i] > right.array[i])) return false;
				}

				return true;
			}

			__forceinline constexpr bool operator<=(const vec& right) const noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					if (!(this->array[i] <= right.array[i])) return false;
				}

				return true;
			}

			__forceinline constexpr bool operator>=(const vec& right) const noexcept {
				for (auto i = 0i64; i < _dimension; ++i) {
					if (!(this->array[i] >= right.array[i])) return false;
				}

				return true;
			}
		};

		NCORE_VEC_TYPE(1);
		NCORE_VEC_TYPE(2);
		NCORE_VEC_TYPE(3);
		NCORE_VEC_TYPE(4);
    }
    
    using namespace types;
}