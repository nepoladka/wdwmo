#pragma once

namespace ncore {
	template<typename return_t> class action {
	public:
		template<typename... parameters_t> using procedure_t = return_t(*)(parameters_t...);
	};
}