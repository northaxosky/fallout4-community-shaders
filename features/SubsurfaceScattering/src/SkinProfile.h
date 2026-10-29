#pragma once

#include "Render/ShaderSubclassHooks.h"

namespace cs::features::subsurface_scattering
{
	// Resolves the Base race keywords; on failure every skin draw stays Base.
	void ResolveBaseRaceKeywords();

	// True when the skin draw belongs to a Human-profile race.
	[[nodiscard]] bool IsHumanSkinDraw(RE::BSRenderPass* a_pass, cs::engine::PrepassBakePath a_path) noexcept;
}
