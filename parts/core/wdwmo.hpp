#pragma once
#include "types.hpp"
#include "configuration.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

//windows dwm overlay
namespace wdwmo {
    using namespace types;

    struct status_t {
        enum enum_t : byte_t {
            none = 0,
            success = 0,
            error = 1,
            aborted,
            invalid_argument_passed,
			unexpected_state,
            guard_not_set,
			callback_not_set,
            not_initialized,
			cant_access_current_process,
            core_module_not_found,
			unexpected_system_build,
			hook_initialization_failure,
			hook_creating_failed,
			hook_enabling_failed, 
			hook_disabling_failed,
			cant_retrieve_dxgi_chain,
			chain_environment_not_found,
			wrong_configuration_target,
			cant_get_pe_info,
			cant_get_chain_buffer,
			cant_get_resource,
			cant_get_device,
			cant_get_device_context,
			cant_get_target_view,
			cant_get_display_config_buffer_sizes,
			cant_get_display_config,

			wrong_image_timestamp,
			wrong_image_size,
			wrong_image_checksum,

			_total_count
        } value;

        struct extended_t {
            enum_t status;
            ui64_t additional;
        };

        __forceinline status_t(byte_t value = {}) noexcept : value(enum_t(value)) { return; }
        __forceinline status_t(enum_t value) noexcept : value(value) { return; }
        __forceinline operator byte_t() const noexcept { return value; }

		__forceinline const char* c_str() const noexcept {
			static constexpr const char* const __values[] = {
				"success",
				"error",
				"aborted",
				"invalid_argument_passed",
				"unexpected_state",
				"guard_not_set",
				"callback_not_set",
				"not_initialized",
				"cant_access_current_process",
				"core_module_not_found",
				"unexpected_system_build",
				"hook_initialization_failure",
				"hook_creating_failed",
				"hook_enabling_failed",
				"hook_disabling_failed",
				"cant_retrieve_dxgi_chain",
				"chain_environment_not_found",
				"wrong_configuration_target",
				"cant_get_pe_info",
				"cant_get_chain_buffer",
				"cant_get_resource",
				"cant_get_device",
				"cant_get_device_context",
				"cant_get_target_view",
				"cant_get_display_config_buffer_sizes",
				"cant_get_display_config",
				"wrong_image_timestamp",
				"wrong_image_size",
				"wrong_image_checksum"
			};

			return (value < success || value >= _total_count) ?
				"unknown" :
				__values[value];
		}
    };

	struct output_display_info_t {
		void* handle = { }; //HMONITOR

		bool active = { };
		bool primary = { };

		rect_t rect = { };
		float scale[2] = { };
		ui32_t rotation = { }; //DISPLAYCONFIG_ROTATION
		ui32_t output_technology = { }; //DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY

		std::string monitor_path = { };
		std::string gdi_name = { };
		std::string friendly_name = { };

		luid_t adapter_id = { };

		struct {
			ui32_t source = { }, target = { };
		}path_id = { };


		struct identifier_t {
			ui32_t path_hash;
			decltype(handle) handle;
			decltype(adapter_id) adapter_id;
			decltype(path_id) path_id;
		};
	};

	struct adapter_info_t {
		luid_t id = { };

		ui32_t vendor_id = { };
		ui32_t device_id = { };
		ui32_t subsys_id = { };
		ui32_t revision = { };

		ui64_t dedicated_video_memory = { };
		ui64_t dedicated_system_memory = { };
		ui64_t shared_system_memory = { };

		std::string name = { };
	};

	struct chain_info_t {
		adapter_info_t adapter = { };

		struct {
			ui32_t width = { }, height = { };
			ui32_t format = { };
		}buffer = { };

		ui32_t dxgi_output_rotation = { }; //DXGI_MODE_ROTATION
		output_display_info_t output_display = { };
	};

	struct context_t {
		struct environment_t {
			bool reinitialize = false;

			void* output = nullptr;				//IDXGIOutputDWM*
			void* device = nullptr;				//ID3D11Device*
			void* device_context = nullptr;		//ID3D11DeviceContext*
			void* texture = nullptr;			//ID3D11Texture2D*
			void* render_target_view = nullptr; //ID3D11RenderTargetView*

			union references_info_t {
				struct {
					bool device : 1;
					bool device_context : 1;
					bool texture : 1;
					bool render_target_view : 1;
				}was_referenced;

				ui8_t flag_value = { };
			}resources_info = { };

			chain_info_t chain_info = { };

			std::unordered_map<ui64_t, ui64_t> linked_pool = { }; //local variables that must be linked with context


			template<typename _t> __forceinline _t*& output_as() noexcept {
				return *(_t**)&output;
			}

			template<typename _t> __forceinline _t*& device_as() noexcept {
				return *(_t**)&device;
			}

			template<typename _t> __forceinline _t*& device_context_as() noexcept {
				return *(_t**)&device_context;
			}

			template<typename _t> __forceinline _t*& texture_as() noexcept {
				return *(_t**)&texture;
			}

			template<typename _t> __forceinline _t*& render_target_view_as() noexcept {
				return *(_t**)&render_target_view;
			}

			void release_resources() noexcept;
		};

		struct call_t {
			void* instance = { }; //coverlaycontext*
			void* chain = { }; //ioverlayswapchain*
			const std::vector<rect_t>& dirty_rects = { };
			const bool legacy_present = { };
		};

		environment_t& info; //static cacheable data
		call_t& call; //dynamic call data
	};

	using initialization_callback_t = status_t(*)(const context_t& context);
	using render_callback_t = status_t(*)(const context_t& context);

	struct working_set final {
	public:
		using present_callback_t = status_t(*)(working_set* self, void* overlay_swap_chain, context_t::call_t& call_info);

#ifndef WDWMO_WORKING_SET_PUBLIC
	private:
#endif
		configuration_t _configuration = { };

		present_callback_t _present_callback = { };
		initialization_callback_t _initialization_callback = { };
		render_callback_t _render_callback = { };

		std::unordered_map<void*, context_t::environment_t*>* _context_registry = { }; //dwm chain, context environment
		std::unordered_map<ui64_t, ui64_t>* _linked_pool = { };

		struct {
			struct {
				void* original = { };
			}present;
		}_targets = { };

		bool _active = true;
		bool _response = false;

	public:
		__forceinline working_set() noexcept = default;

		__forceinline working_set(
			const configuration_t& configuration,
			present_callback_t present_callback,
			initialization_callback_t initialization_callback,
			render_callback_t render_callback,
			void* original_present,
			bool linked_pool = true) noexcept :
			_configuration(configuration),
			_present_callback(present_callback),
			_initialization_callback(initialization_callback),
			_render_callback(render_callback) {
			_targets.present.original = original_present;

			_context_registry = new std::unordered_map<void*, context_t::environment_t*>();
			_linked_pool = linked_pool ? 
				new std::unordered_map<ui64_t, ui64_t>() : 
				nullptr;
		}

		void reset(present_callback_t present_callback, initialization_callback_t initialization_callback, render_callback_t render_callback) noexcept;

		__forceinline auto& configuration() noexcept {
			return _configuration;
		}

		__forceinline const auto& configuration() const noexcept {
			return _configuration;
		}

		__forceinline bool& active() noexcept {
			return _active;
		}

		__forceinline bool active() const noexcept {
			return _active;
		}

		__forceinline bool suspended() const noexcept {
			return not _active;
		}

		__forceinline bool response(bool value) noexcept {
			auto previous = _response;

			if (previous != value) {
				_response = value;
			}

			return previous;
		}

		__forceinline auto& targets() noexcept {
			return _targets;
		}

		__forceinline const auto& targets() const noexcept {
			return _targets;
		}

		__forceinline status_t get_context_environment(void* identifier, ui32_t thread_id, context_t::environment_t*& _context, bool& _new) noexcept {
			auto registry = _context_registry;
			if (!registry) return status_t::not_initialized;

			auto it = registry->find(identifier);
			if (it == registry->end() || it->second == nullptr) {
				_new = true;

				registry->emplace(identifier, _context = new context_t::environment_t());
			}
			else {
				_new = false;

				_context = it->second;
			}

			return status_t::success;
		}

		__forceinline auto linked_pool() noexcept {
			return _linked_pool;
		}

		__forceinline const auto linked_pool() const noexcept {
			return _linked_pool;
		}

		__forceinline auto on_present(void* overlay_swap_chain, context_t::call_t& call_info) noexcept {
			return _present_callback(this, overlay_swap_chain, call_info);
		}

		template<typename _t>
		__forceinline _t get_original_present() noexcept {
			return _t(_targets.present.original);
		}

		__forceinline status_t initialize(ui32_t thread_id, context_t::environment_t& context, context_t::call_t& call) noexcept {
			return _initialization_callback ?
				_initialization_callback({ .info = context, .call = call }) :
				status_t::success;
		}

		__forceinline status_t render(context_t::environment_t& context, context_t::call_t& call) noexcept {
			return _render_callback ?
				_render_callback({ .info = context, .call = call }) :
				status_t::callback_not_set;
		}

		__forceinline decltype(_context_registry) release() noexcept {
			if (auto registry = _context_registry) {
				_context_registry = nullptr;

				for (auto& entry : *registry) {
					entry.second->release_resources();
				}

				return registry;
			}

			return nullptr;
		}
	};

	class initialization_guard final {
	public:
		using termination_callback_t = status_t(*)(const initialization_guard*);
		using shared_storage_provider_t = void** (*)(const initialization_guard*);
		using tls_provider_t = ui32_t(*)(const initialization_guard*);
		using shared_termination_callback_t = status_t(*)(const initialization_guard*);
		using commit_changes_procedure_t = status_t(*)();

		struct shared_storage_t {
			ui64_t identifier = { };
			ui32_t taken_tls_slot = { };

			shared_termination_callback_t shared_termination_callback;

			struct {
				working_set* instance = { };
				termination_callback_t termination_callback = { };
			}existing_set_info = { };

			std::unordered_map<ui64_t, ui64_t> user_pool = { };
		};

	private:
		shared_storage_provider_t _shared_storage_slot_provider = { };
		ui64_t _storage_identifier = { };

		commit_changes_procedure_t _commit_changes = { };

		/*
			[](const initialization_guard*) {
				return &__process_environment->TelemetryCoverageHeader;
			}
		*/

		__forceinline shared_storage_t*& get_store() noexcept {
			return *(shared_storage_t**)_shared_storage_slot_provider(this);
		}

	public:
		__forceinline initialization_guard(
			shared_storage_provider_t shared_storage_slot_provider,
			ui64_t storage_identifier,
			commit_changes_procedure_t commit_changes = nullptr) noexcept :
			_shared_storage_slot_provider(shared_storage_slot_provider),
			_storage_identifier(storage_identifier),
			_commit_changes(commit_changes) {
			return;
		}

		__forceinline shared_storage_t* get_storage() noexcept {
			auto& store = get_store();

			return store && store->identifier == _storage_identifier ?
				store :
				nullptr;
		}

		__forceinline shared_storage_t* create_storage(ui32_t reserved_tls_slot = ui32_t()) noexcept {
			auto result = new shared_storage_t{
				.identifier = _storage_identifier,
				.taken_tls_slot = reserved_tls_slot,
				.existing_set_info = { }
			};

			auto& store = get_store();

			return store = result;
		}

		__forceinline shared_storage_t* get_or_create_storage(tls_provider_t tls_provider = nullptr) noexcept {
			if (auto storage = get_storage()) return storage;

			return create_storage(tls_provider ? tls_provider(this) : ui32_t());
		}

		__forceinline shared_storage_t* erase_storage() noexcept {
			auto& store = get_store();

			auto storage = store;

			store = nullptr;

			return storage;
		}

		__forceinline status_t commit_changes() noexcept {
			return _commit_changes ?
				_commit_changes() :
				status_t::success;
		}
	};

	working_set* get_current_working_set() noexcept;
	working_set* set_current_working_set(working_set* set) noexcept; //returns previous stored value

	ui64_t get_system_build_support_status(
		ui32_t build_number, //rtlgetversion.build
		ui64_t unknown = -1,
		ui64_t not_supported = 0,
		ui64_t supported = 1,
		ui64_t supported_with_troubles = 2) noexcept;

    status_t was_initialized_before(initialization_guard* guard, bool& _result) noexcept;

	status_t::extended_t validate_configuration(
		const configuration_t& configuration,
		ui64_t dwmcore_module_identifier = 'dwmc',
		ui64_t dxgi_module_identifier = 'dxgi') noexcept;

    status_t initialize(
		const configuration_t& configuration,
        initialization_callback_t initialization_callback,
        render_callback_t render_callback,
        initialization_guard* guard = nullptr,
        bool force_hook = false, // ignored if first initialization or guard is not set
        initialization_guard::termination_callback_t termination_callback = nullptr) noexcept;

    status_t terminate(initialization_guard* guard = nullptr) noexcept;

	namespace utils {
		bool is_monitor_primary(void* monitor) noexcept;
		rect_t get_monitor_rect(void* monitor = nullptr, bool work = false) noexcept;
		ui64_t get_monitor_scale(void* monitor = nullptr) noexcept; //returns float[2], x/y
	}

    namespace debug {
		void set_message_callback(message_callback_t callback) noexcept;
		message_callback_t get_message_callback() noexcept;

        void set_console_output(void* handle) noexcept;
        void* get_console_output() noexcept;
        
		int conlogf(const char* fmt, ...) noexcept;
		ui64_t conlogfh(ui64_t delay, ui64_t previous, const char* fmt, ...) noexcept;
    }
}
