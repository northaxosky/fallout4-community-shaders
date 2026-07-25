#pragma once

#include "Render/PixelShaderSwapBroker.h"

#include <cstdint>
#include <optional>

namespace cs::engine::shader_context
{
	struct Context
	{
		const char* subclassName = nullptr;
		std::uint32_t techniqueBits = 0;
		bool active = false;
		bool techniqueKnown = false;
	};

	Context Current() noexcept;
	Context CurrentOrSticky() noexcept;
	std::optional<ShaderVariantKeyView> CurrentVariant() noexcept;

	void SetSticky(const char* a_name, std::uint32_t a_techniqueBits) noexcept;
	void ClearSticky() noexcept;

	class Scope
	{
	public:
		Scope(
			const char* a_name,
			std::optional<std::uint32_t> a_techniqueBits) noexcept;
		~Scope() noexcept;

		Scope(const Scope&) = delete;
		Scope(Scope&&) = delete;
		Scope& operator=(const Scope&) = delete;
		Scope& operator=(Scope&&) = delete;

	private:
		Context _previous;
	};
}
