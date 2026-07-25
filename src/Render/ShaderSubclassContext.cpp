#include "Render/ShaderSubclassContext.h"

#include "Render/ShaderVariantRuntimeResolver.h"

namespace cs::engine::shader_context
{
	namespace
	{
		thread_local Context g_current{};
		thread_local Context g_sticky{};
	}

	Context Current() noexcept
	{
		return g_current;
	}

	Context CurrentOrSticky() noexcept
	{
		return g_current.active ? g_current : g_sticky;
	}

	std::optional<PixelShaderVariantView> CurrentVariant() noexcept
	{
		if (!g_current.active || !g_current.techniqueKnown
			|| !g_current.subclassName) {
			return std::nullopt;
		}
		return ResolvePixelShaderVariant(
			g_current.subclassName,
			g_current.techniqueBits);
	}

	void SetSticky(
		const char* a_name,
		std::uint32_t a_techniqueBits) noexcept
	{
		g_sticky.subclassName = a_name;
		g_sticky.techniqueBits = a_techniqueBits;
		g_sticky.active = a_name != nullptr;
		g_sticky.techniqueKnown = a_name != nullptr;
	}

	void ClearSticky() noexcept
	{
		g_sticky = {};
	}

	Scope::Scope(
		const char* a_name,
		std::optional<std::uint32_t> a_techniqueBits) noexcept :
		_previous(g_current)
	{
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
