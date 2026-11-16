#pragma once

#include "MaterialRegistry.h"
#include "Render/PrepassLane.h"

#include "RE/N/NiPointer.h"
#include "RE/N/NiTexture.h"

#include <array>

namespace cs::features::true_pbr
{
	// Indexed by engine::PrepassRecordTexture; null where there is none.
	using RecordTextureSet = std::array<RE::NiPointer<RE::NiTexture>, static_cast<std::size_t>(engine::PrepassRecordTexture::kCount)>;

	// Loads the textures the engine's material path never does. A missing or
	// failed file leaves its entry null and notes one warning per record.
	[[nodiscard]] RecordTextureSet RequestRecordTextures(const MaterialRecord& a_record);
}
