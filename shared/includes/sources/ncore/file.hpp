#pragma once
#include "defines.hpp"
#include "handle.hpp"
#include "strings.hpp"
#include "static_array.hpp"
#include "environment.hpp"

#include <windows.h>
#include <shlobj.h>
#include <vector>
#include <filesystem>
#include <functional>

namespace ncore {
	static const handle::native_handle_t::closer_t const __fileHandleCloser = handle::native_handle_t::closer_t(NtClose);
	static constexpr const unsigned const __defaultFileOpenAccess = FILE_ALL_ACCESS;
	static constexpr const unsigned const __defaultFileShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
	static constexpr const lsize_t const __fileReadWriteLimit = __gb(2);  //1024 * 1024 * 1024 * 2; //2gb
	static constexpr const size_t const __filePathLimit = MAX_PATH;

	class path {
	public:
		static __forceinline auto get_absolute_path(const strings::compatible_string& local) noexcept {
			auto wstring = local.wstring();
			auto buffer = UNICODE_STRING();

			RtlDosPathNameToNtPathName_U(wstring.c_str(), &buffer, nullptr, nullptr);

			auto result = strings::compatible_string(buffer.Buffer);

			RtlFreeUnicodeString(&buffer);

			return result;
		}

	private:
		strings::compatible_string _value;

	public:
		__forceinline path() = default;

		__forceinline path(const strings::compatible_string& value) noexcept {
			_value = get_absolute_path(value);
		}

		__forceinline path(const std::string& value) noexcept {
			_value = get_absolute_path(value);
		}

		__forceinline path(const char* value) noexcept {
			_value = get_absolute_path(value);
		}

		__forceinline path(const std::wstring& value) noexcept {
			_value = get_absolute_path(value);
		}

		__forceinline path(const wchar_t* value) noexcept {
			_value = get_absolute_path(value);
		}

		__forceinline constexpr auto& value() const noexcept {
			return _value;
		}

		__forceinline constexpr auto string() const noexcept {
			return _value.string();
		}

		__forceinline constexpr auto wstring() const noexcept {
			return _value.wstring();
		}

		__forceinline auto parts(strings::compatible_string* _drive, strings::compatible_string* _folder, strings::compatible_string* _name, strings::compatible_string* _extension) const noexcept {
			auto drive = static_array<char, __filePathLimit>();
			auto folder = static_array<char, __filePathLimit>();
			auto name = static_array<char, __filePathLimit>();
			auto extension = static_array<char, __filePathLimit>();

			_splitpath(_value.string().c_str(), 
				_drive ? drive.data() : nullptr, 
				_folder ? folder.data() : nullptr, 
				_name ? name.data() : nullptr,
				_extension ? extension.data() : nullptr);

			if (_drive) {
				*_drive = strings::compatible_string(drive.data());
			}

			if (_folder) {
				*_folder = strings::compatible_string(folder.data());
			}

			if (_name) {
				*_name = strings::compatible_string(name.data());
			}

			if (_extension) {
				*_extension = strings::compatible_string(extension.data());
			}
		}

		__forceinline auto parts(strings::compatible_string* _directory, strings::compatible_string* _name) const noexcept {
			auto drive = static_array<char, __filePathLimit>();
			auto folder = static_array<char, __filePathLimit>();
			auto name = static_array<char, __filePathLimit>();
			auto extension = static_array<char, __filePathLimit>();

			_splitpath(_value.string().c_str(),
				_directory ? drive.data() : nullptr, 
				_directory ? folder.data() : nullptr, 
				_name ? name.data() : nullptr,
				_name ? extension.data() : nullptr);

			if (_directory) {
				*_directory = strings::compatible_string(std::string(drive.data()) + folder.data());
			}

			if (_name) {
				*_name = strings::compatible_string(std::string(name.data()) + extension.data());
			}
		}

		__forceinline auto name() const noexcept {
			auto result = strings::compatible_string();
			return parts(nullptr, &result), result;
		}

		__forceinline auto directory() const noexcept {
			auto result = strings::compatible_string();
			return parts(&result, nullptr), result;
		}
	};

	class file {
	public:
		enum class create_disposion : ui32_t {
			create_force = FILE_SUPERSEDE,
			create_new = FILE_CREATE,
			open_existing = FILE_OPEN,
			open_or_create = FILE_OPEN_IF,
			overwrite_existing = FILE_OVERWRITE,
			overwrite_or_create = FILE_OVERWRITE_IF
		};

		using status_t = IO_STATUS_BLOCK;

		static __forceinline handle::native_t __fastcall get_handle(const path& path, ui32_t open_access = __defaultFileOpenAccess, create_disposion disposion = create_disposion::open_existing, ui32_t share_access = __defaultFileShareAccess, ui32_t create_options = null, status_t* _status = nullptr) noexcept {
			static constexpr const auto __defaultFileAttributes = ui32_t(FILE_ATTRIBUTE_NORMAL);
			static constexpr const auto __defaultCreateOptions = ui32_t(FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);

			auto result = handle::native_t();
			auto attributes = OBJECT_ATTRIBUTES();

			if (!_status) {
				auto status = status_t();
				_status = &status;
			}

			auto upath = UNICODE_STRING();
			RtlInitUnicodeString(&upath, path.value().wstring().c_str());

			InitializeObjectAttributes(&attributes, PUNICODE_STRING(&upath), OBJ_CASE_INSENSITIVE, NULL, NULL);

			const auto status = NtCreateFile(
				&result, open_access, &attributes, _status, null, __defaultFileAttributes, share_access, ui32_t(disposion),
				create_options ? __defaultCreateOptions | create_options : __defaultCreateOptions, null, null);

			//RtlFreeUnicodeString(&upath);

			return result;
		}

		static __forceinline auto get_path(handle::native_t handle) noexcept {
			auto result = ncore::path();

			if (!handle) _Exit: return result;

			auto status = status_t();

			auto size = lsize_t(sizeof(FILE_NAME_INFORMATION::FileNameLength) + __filePathLimit * sizeof(wchar_t));
			auto info = PFILE_NAME_INFORMATION(memset(malloc(size), null, size));

			if (NT_SUCCESS(NtQueryInformationFile(handle, &status, info, size, FILE_INFORMATION_CLASS::FileNameInformation))) {
				result = ncore::path(info->FileName);
			}

			free(info);

			goto _Exit;
		}

	private:
		using handle_t = handle::native_handle_t;

		path _path;

		static __forceinline auto __fastcall temp_handle(const path& path, const handle_t& source, ui32_t open_access, create_disposion disposion, ui32_t share_access = __defaultFileShareAccess, ui32_t create_options = null, status_t* _status = nullptr) noexcept {
			auto result = source;
			if (!result) {
				(result = handle_t(get_handle(path, open_access, disposion, share_access, create_options, _status), __fileHandleCloser, false)).close_on_destroy(true);
			}
			return result;
		}

		__forceinline size_t get_size(handle::native_t handle) const noexcept {
			if (!handle) return null;

			auto status = status_t();
			auto info = FILE_STANDARD_INFORMATION();
			NtQueryInformationFile(handle, &status, &info, sizeof(info), FILE_INFORMATION_CLASS::FileStandardInformation);

			return *size_p(&info.EndOfFile);
		}

	public:
		__forceinline __fastcall file(const path& path = ncore::path()) noexcept {
			_path = path;
		}

		__forceinline __fastcall file(const std::string& path = std::string()) noexcept {
			_path = path;
		}

		__forceinline __fastcall file(const char* path = nullptr) noexcept {
			_path = path;
		}

		__forceinline __fastcall file(const std::wstring& path = std::wstring()) noexcept {
			_path = path;
		}

		__forceinline __fastcall file(const wchar_t* path = nullptr) noexcept {
			_path = path;
		}

		__forceinline __fastcall file(handle::native_t handle) noexcept {
			_path = get_path(handle);
		}

		static __forceinline file open(const path& path) noexcept {
			return file(path);
		}

		static __forceinline file create(const path& path) noexcept {
			temp_handle(path, handle_t(), SYNCHRONIZE, create_disposion::create_force);

			return file(path);
		}

		__forceinline constexpr auto& path() const noexcept {
			return _path;
		}

		__forceinline bool exists() const noexcept {
			return temp_handle(_path, handle_t(), SYNCHRONIZE, create_disposion::open_existing).get();
		}

		__forceinline auto get_size() const noexcept {
			return get_size(temp_handle(_path, handle_t(), SYNCHRONIZE, create_disposion::open_existing).get());
		}

		__forceinline bool read_memory(offset_t offset, size_t size, void* _data) const noexcept {
			if (!_data) return false;

			auto handle = temp_handle(_path, handle_t(), FILE_READ_DATA | SYNCHRONIZE, create_disposion::open_existing, __defaultFileShareAccess);
			if (!handle) return false;

			auto file_size = get_size(handle.get());
			if (file_size < size || offset > file_size) return false;

			if (!size) {
				size = file_size;
			}

			static constexpr const auto read = [](const handle::native_t handle, const offset_t offset, const lsize_t size, byte_p buffer) noexcept {
				auto status = status_t();
				return NtReadFile(handle, nullptr, nullptr, nullptr, &status, buffer, size, PLARGE_INTEGER(&offset), nullptr);
			};

			if (size > __fileReadWriteLimit) {
				auto parts = aligned<size_t>(size, __fileReadWriteLimit);
				for (size_t i = 0; i < parts.count; i++) {
					const auto current_offset = parts.divided * i;
					read(handle.get(), offset + current_offset, parts.divided, byte_p(_data) + current_offset);
				}

				if (parts.low) {
					const auto current_offset = parts.divided * parts.count;
					read(handle.get(), offset + current_offset, parts.divided, byte_p(_data) + current_offset);
				}
			}
			else {
				read(handle.get(), offset, size, byte_p(_data));
			}

			return true;
		}

		template<typename _t> auto read(offset_t offset = null) const noexcept {
			auto result = _t();
			read_memory(offset, sizeof(_t), &result);
			return result;
		}

		__forceinline auto read() const noexcept {
			auto result = std::vector<byte_t>();

			auto handle = temp_handle(_path, handle_t(), FILE_READ_DATA | SYNCHRONIZE, create_disposion::open_existing);
			if (!handle) _Exit: return result;

			auto size = get_size(handle.get());
			if (!size) goto _Exit;

			result.resize(size);
			if (!read_memory(null, size, result.data())) {
				result.clear();
			}

			goto _Exit;
		}

		__forceinline auto read(byte_p* _data) const noexcept {
			auto size = size_t();
			if (!_data) _Exit: return size;

			auto handle = temp_handle(_path, handle_t(), FILE_READ_DATA | SYNCHRONIZE, create_disposion::open_existing);
			if (!handle) goto _Exit;

			if (!(size = get_size(handle.get()))) goto _Exit;

			auto useless = byte_p(nullptr);
			auto buffer = byte_p(malloc(size));

			if (read_memory(null, size, buffer)) {
				useless = *_data;
				*_data = buffer;
			}
			else {
				useless = buffer;
			}

			if (useless) {
				free(useless);
			}

			goto _Exit;
		}
	};

	static __forceinline auto file_exists(const std::string& path) noexcept {
		return file(path).exists();
	}

	static __forceinline auto read_file(const std::string& path) noexcept {
		return file(path).read();
	}

	static __forceinline std::string system_directory() noexcept {
		static char path[__filePathLimit + 1]{ 0 };
		if (!*path) {
			GetSystemDirectoryA(path, __filePathLimit);

			strcat(path, "\\");
		}
		return path;
	}
}
