#pragma once

namespace RE
{
	class BSRenderPass;
}

namespace cs::engine
{
	// kLODObjects and LOD material features miss BTO shapes.
	[[nodiscard]] bool IsObjectLODShape(RE::BSRenderPass* a_pass) noexcept;
}
