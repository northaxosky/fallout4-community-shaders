#pragma once

namespace RE
{
	class BSRenderPass;
}

namespace cs::engine
{
	// BTO shapes: non-kLODLandscape shaders of geometry under the land LOD root.
	// The kLODObjects property flag and the LOD material features miss BTO shapes, so they cannot gate.
	[[nodiscard]] bool IsObjectLODShape(RE::BSRenderPass* a_pass) noexcept;
}
