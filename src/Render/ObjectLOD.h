#pragma once

namespace RE
{
	class BSRenderPass;
}

namespace cs::engine
{
	// The kLODObjects flag and LOD material features miss BTO shapes, so the land LOD root decides.
	[[nodiscard]] bool IsObjectLODShape(RE::BSRenderPass* a_pass) noexcept;
}
