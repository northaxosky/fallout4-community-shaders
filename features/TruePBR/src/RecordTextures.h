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

	// Null where the file is missing or failed; warns once per record.
	[[nodiscard]] RecordTextureSet RequestRecordTextures(const MaterialRecord& a_record);
}
