#pragma once
#include "defines.hpp"

#include <vector>
#include <string>

#ifndef BEA_ENGINE_STATIC
#define BEA_ENGINE_STATIC
#endif

#ifndef BEA_USE_STDCALL
#define BEA_USE_STDCALL
#endif

#include "includes/beaengine/beaengine.h"

#ifdef NCORE_DISASSEMBLY_INCLUDE_BEA_LIBRARY
#pragma comment(lib, "beaengine.lib")
#endif

namespace ncore::disassembly {
	begin_unaligned struct instruction {
	public:
		using collection = std::vector<instruction>;

		ui64_t eip = { }, offset = { };
		ui32_t security_block = { };
		char complete_instruction[INSTRUCT_LENGTH] = { };
		ui32_t architecture = { };
		ui64_t options = { };
		INSTRTYPE info = { };
		OPTYPE operands[9] = { };
		PREFIXINFO prefix = { };
		i32_t error = { };

	protected:
		InternalDatas _internal = { };

	public:
		address_t address = { };
		i32_t length = { };
		byte_t* saved_bytes = { };

		__forceinline void __fastcall save_bytes(byte_t* bytes) noexcept {
			auto pointer = saved_bytes ? saved_bytes : nullptr;

			memcpy(saved_bytes = (byte_t*)malloc(length), bytes, length);

			if (pointer) return free(pointer);
		}

		__forceinline constexpr void __fastcall release_bytes() noexcept {
			if (!saved_bytes) return;

			auto pointer = saved_bytes;
			saved_bytes = nullptr;

			return free(pointer);
		}
	} end_unaligned;

	class code : private instruction::collection {
	public:
		struct info_t {
			std::string name;
			std::vector<instruction> references;
		};

		address_t address;

		__forceinline code(const byte_p code, size_t length, address_t address = nullptr, bool save_bytes = false) noexcept {
			this->address = address = (address ? address : address_t(code));

			auto instruction = disassembly::instruction();
			instruction.architecture = 64;
			instruction.eip = ui64_t(code);

			instruction.offset = 0;
			while (instruction.offset < length) {
				if ((instruction.length = Disasm(LPDISASM(&instruction))) <= 0) return;

				if (save_bytes) {
					instruction.save_bytes(code + instruction.offset);
				}

				instruction.address = address_t(ui64_t(address) + instruction.offset);
				instruction::collection::push_back(instruction);

				instruction.eip += instruction.length;
				instruction.offset += instruction.length;
			}
		}

		__forceinline constexpr const size_t length() const noexcept {
			auto result = size_t(0);
			for (auto& part : *((instruction::collection*)this)) {
				result += part.length;
			}
			return result;
		}

		__forceinline constexpr const size_t size() const noexcept {
			return instruction::collection::size();
		}

		__forceinline constexpr auto data() noexcept {
			return instruction::collection::data();
		}

		__forceinline constexpr const auto data() const noexcept {
			return instruction::collection::data();
		}

		__forceinline constexpr auto at(size_t index) noexcept {
			return instruction::collection::at(index);
		}

		__forceinline constexpr const auto& at(size_t index) const noexcept {
			return instruction::collection::at(index);
		}

		__forceinline constexpr const auto begin() const noexcept {
			return instruction::collection::begin();
		}

		__forceinline constexpr const auto end() const noexcept {
			return instruction::collection::end();
		}

		__forceinline instruction& operator[](size_t index) noexcept {
			return instruction::collection::operator[](index);
		}

		__forceinline constexpr const instruction& operator[](size_t index) const noexcept {
			return instruction::collection::operator[](index);
		}
	};
}
