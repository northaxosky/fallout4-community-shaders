#include "Render/ShaderSubclassContext.h"

namespace cs::engine::shader_context
{
	namespace
	{
		thread_local Context g_current{};
	}

	Context Current() noexcept
	{
		return g_current;
	}

	Scope::Scope(
		void* a_shader,
		const char* a_name,
		std::optional<std::uint32_t> a_techniqueBits) noexcept :
		_previous(g_current)
	{
		g_current.shader = a_shader;
		g_current.subclassName = a_name;
		g_current.techniqueBits = a_techniqueBits.value_or(0);
		g_current.active = a_name != nullptr;
		g_current.techniqueKnown =
			a_name != nullptr && a_techniqueBits.has_value();
	}

	Scope::~Scope() noexcept
	{
		g_current = _previous;
	}
}
