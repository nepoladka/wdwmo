//windows dwm overlay sample loader

#include "../../../ncore/source/ncore.h"

#define WDWMO_WORKING_SET_PUBLIC

#include "../dumper/wdwmcd.hpp"
#include "../core/wdwmo.hpp"

#pragma comment(lib, "wdwmcd.lib")
#pragma comment(lib, "nweb.lib")

#define conlog wdwmcd::debug::conlogf

wdwmcd::status_t get_configuration(std::string dwmcore_path, std::string dxgi_path, wdwmo::configuration_t& _result) {
	auto directory = ncore::system_directory();
	//auto directory = std::string("G:\\work\\dwm\\win11-25h2\\");

	if (dwmcore_path.empty()) {
		dwmcore_path = directory + "dwmcore.dll";
	}

	if (dxgi_path.empty()) {
		dxgi_path = directory + "dxgi.dll";
	}

	conlog("[l] expecting dwmcore.dll file at %s\n", dwmcore_path.c_str());
	conlog("[l] expecting dxgi.dll file at %s\n", dxgi_path.c_str());

	if (!ncore::file_exists(dwmcore_path)) {
		conlog("[l] file at %s not found\n", dwmcore_path.c_str());

		return wdwmcd::status_t::error;
	}

	if (!ncore::file_exists(dxgi_path)) {
		conlog("[l] file at %s not found\n", dxgi_path.c_str());

		return wdwmcd::status_t::error;
	}

	auto dwmcore_file = ncore::read_file(dwmcore_path);
	auto dxgi_file = ncore::read_file(dxgi_path);

	auto dwmcore_pdb = std::string();
	auto dxgi_pdb = std::string();

	{
		auto pdb_url = std::string();
		if (auto status = wdwmcd::get_microsoft_server_pdb_url(dwmcore_file.data(), dwmcore_file.size(), pdb_url)) {
			conlog("[l] file doesn't contains right pdb info, status: %d\n", status);

			return status;
		}

		conlog("[l] loading pdb from %s\n", pdb_url.c_str());

		auto response = ncore::web::request::send(ncore::web::request::m_get, pdb_url);
		if (response.data.empty()) {
			conlog("[l] can't load pdb from %s, response: %d\n", pdb_url.c_str(), response.code);

			return wdwmcd::status_t::error;
		}

		dwmcore_pdb.swap(response.data);
	}

	{
		auto pdb_url = std::string();
		if (auto status = wdwmcd::get_microsoft_server_pdb_url(dxgi_file.data(), dxgi_file.size(), pdb_url)) {
			conlog("[l] file doesn't contains right pdb info, status: %d\n", status);

			return status;
		}

		conlog("[l] loading pdb from %s\n", pdb_url.c_str());

		auto response = ncore::web::request::send(ncore::web::request::m_get, pdb_url);
		if (response.data.empty()) {
			conlog("[l] can't load pdb from %s, response: %d\n", pdb_url.c_str(), response.code);

			return wdwmcd::status_t::error;
		}

		dxgi_pdb.swap(response.data);
	}

	auto result = wdwmcd::detect_configuration(
		{ .image = {.data = dwmcore_file.data(), .size = dwmcore_file.size()}, .pdb = {.data = dwmcore_pdb.data(), .size = dwmcore_pdb.size()} },
		{ .image = {.data = dxgi_file.data(), .size = dxgi_file.size()}, .pdb = {.data = dxgi_pdb.data(), .size = dxgi_pdb.size()} },
		_result);

	if (auto dwm = ncore::process::get_by_name("dwm"); dwm.alive()) {
		auto handle = dwm.handle(ncore::__defaultProcessOpenAccess);

		auto status = wdwmcd::experimental::try_detect_runtime_offsets_externally(
			{ .image = {.data = dwmcore_file.data(), .size = dwmcore_file.size()}, .pdb = {.data = dwmcore_pdb.data(), .size = dwmcore_pdb.size()} },
			handle,
			_result);

		conlog("[l] wdwmcd::experimental::try_detect_runtime_offsets_externally returned %d (%s)\n", status, status.c_str());
	}
	else {
		conlog("[l] can't try experimental features, dwm.exe isn't alive or not found\n");
	}

	return result;
}

bool check_initialization(ncore::process& process, wdwmo::working_set* _current_set) {
	auto environment = process.get_environment();
	auto pointer = environment.TelemetryCoverageHeader;
	if (!pointer) return false;

	auto storage_buffer = ncore::static_array<ncore::types::byte_t, sizeof(wdwmo::initialization_guard::shared_storage_t)>(); {
		process.read_memory(pointer, sizeof(wdwmo::initialization_guard::shared_storage_t), storage_buffer.data());
	}

	auto storage = (wdwmo::initialization_guard::shared_storage_t*)storage_buffer.data();
	if (storage->identifier != process.get_environment_address<ncore::types::ui64_t>()) return false;

	if (_current_set) {
		if (auto set_address = storage->existing_set_info.instance) {
			process.read_memory(set_address, sizeof(wdwmo::working_set), _current_set);
		}
	}

	return true;
}

int main(int argc, char** argv) {
	auto status = 1;

	wdwmcd::debug::set_console_output(GetStdHandle(STD_OUTPUT_HANDLE));

	if (!ncore::process::current().set_privilege(SE_DEBUG_NAME, true)) {
		conlog("[l] can't set debug privilege\n");

	_Exit:
		conlog("[l] execution result: %d\n", status);

		if (!IsDebuggerPresent()) {
			system("pause");
		}

		return status;
	}

	conlog("[l] windows build number: %d, arguments count: %d\n", ncore::get_system_version().build, argc);

	auto dwmcore_path = std::string();
	auto dxgi_path = std::string();
	if (argc >= 3) {
		dwmcore_path = argv[1];
		dxgi_path = argv[2];
	}
	
	auto configuration = wdwmo::configuration_t();
	auto dump_status = get_configuration(dwmcore_path, dxgi_path, configuration);

	conlog("[l] dump status is %d, raw:\n    ", dump_status);

	for (auto i = 0, j = 0; i < sizeof(configuration); i++) {
		conlog("0x%02x", ncore::types::byte_p(&configuration)[i]);

		if ((i + 1) == sizeof(configuration)) continue;

		if (((i + 1) % 4) == 0) {
			conlog(",\n    ");
		}
		else {
			conlog(", ");
		}
	}

	conlog("\n");

	if (dump_status) goto _Exit;

	conlog(
		"[l] configuration: \n"
		"    image_info.dwmcore:               %lx, %lx, %lx\n"
		"    image_info.dxgi:                  %lx, %lx, %lx\n"
		"    offsets.present:                  %08x\n"
		"    offsets.render_target:            %08x\n"
		"    offsets.dxgi_output_vftable:      %08x\n"
		"    offsets.dxgi_output:              %08x\n"
		"    offsets.dxgi_swap_chain:          %08x\n"
		"    offsets.get_physical_back_buffer: %08x\n"
		"    offsets.get_d3d11_resource:       %08x\n",
		configuration.image_info.dwmcore.timestamp, configuration.image_info.dwmcore.size, configuration.image_info.dwmcore.checksum,
		configuration.image_info.dxgi.timestamp, configuration.image_info.dxgi.size, configuration.image_info.dxgi.checksum,
		configuration.offsets.present,
		configuration.offsets.render_target,
		configuration.offsets.dxgi_output_vftable,
		configuration.offsets.dxgi_output,
		configuration.offsets.dxgi_swap_chain,
		configuration.offsets.get_physical_back_buffer,
		configuration.offsets.get_d3d11_resource);

	system("pause");

	auto dwm = ncore::process::get_by_name("dwm");
	if (!dwm.alive()) {
		auto processes = ncore::get_processes();

		conlog("[l] can't get dwm.exe, available processes: %d\n", processes.size());
		for (auto& process : processes) {
			auto name = process.get_name();

			conlog(" %d (%08x) - %s\n", process.id(), process.id(), name.c_str());
		}

		goto _Exit;
	}

	auto set_buffer = ncore::static_array<ncore::types::byte_t, sizeof(wdwmo::working_set)>();
	if (check_initialization(dwm, (wdwmo::working_set*)set_buffer.data())) {
		auto current_set = (wdwmo::working_set*)set_buffer.data();

		conlog(
			"[l] seems wdwmo is already initialized %s\n"
			"    present callback:        %#llx\n"
			"    render callback:         %#llx\n"
			"    initialization_callback: %#llx\n"
			"    present jumpout:         %#llx\n"
			"[l] set requirements: \n"
			"    image_info.dwmcore:      %lx, %lx, %lx\n"
			"    image_info.dxgi:         %lx, %lx, %lx\n"
			"    offsets.present:         %08x\n"
			"    offsets.dxgi_swap_chain: %08x\n",
			current_set->_active ? "and active" : "but suspended",
			current_set->_present_callback,
			current_set->_render_callback,
			current_set->_initialization_callback,
			current_set->_targets.present.original,
			current_set->_configuration.image_info.dwmcore.timestamp, current_set->_configuration.image_info.dwmcore.size, current_set->_configuration.image_info.dwmcore.checksum,
			current_set->_configuration.image_info.dxgi.timestamp, current_set->_configuration.image_info.dxgi.size, current_set->_configuration.image_info.dxgi.checksum,
			current_set->_configuration.offsets.present,
			current_set->_configuration.offsets.dxgi_swap_chain);

		system("pause");
	}

	auto wdwmo_path = std::string("wdwmo.dll");
	if (argc >= 4) {
		wdwmo_path = argv[3];
	}

	conlog("[l] expecting wdwmo.dll at %s\n", wdwmo_path.c_str());

	auto file = ncore::read_file(wdwmo_path);
	if (file.empty()) {
		conlog("[l] can't find wdwmo.dll file at %s\n", wdwmo_path);
	}
	else {
		auto allocated = dwm.allocate_memory(sizeof(configuration));
		if (allocated) {
			conlog("[l] memory for dumped info allocated at %#llx\n", allocated);

			dwm.write_memory(allocated, &configuration, sizeof(configuration));
		}
		else {
			conlog("[l] can't allocate memory for dumped info\n");
		}

		auto result = ncore::utils::manual_map_library(dwm.handle(), file.data(), file.size(), DLL_PROCESS_ATTACH, allocated);

		conlog("[l] injection into dwm.exe (%d) result: %#llx\n", dwm.id(), result);

		status = 0;
	}

	dwm.close_handle();

	goto _Exit;
}