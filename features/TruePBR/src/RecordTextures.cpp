#include "RecordTextures.h"

#include "LoadNotices.h"
#include "Log.h"
#include "Render/Engine.h"

#include "RE/B/BSShaderManager.h"

#include <format>

namespace cs::features::true_pbr
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.truepbr");

		struct RecordTextureSpec
		{
			engine::PrepassRecordTexture target;
			PbrTexture source;
			bool srgb;
		};

		// Features 0 and 1 join with their consumers.
		constexpr std::array kRecordTextures{
			RecordTextureSpec{ engine::PrepassRecordTexture::kDisplacement, PbrTexture::kDisplacement, false }
		};

		void Warn(const MaterialRecord& a_record, PbrTexture a_source, const std::string& a_path)
		{
			const auto reason = std::format("the {} texture '{}' did not load", kTextureSlots[static_cast<std::size_t>(a_source)].name, a_path);
			if (Notices().Warn(a_record.name, reason))
				L->warn("TruePBR material '{}': {}; it renders without it.", a_record.name, reason);
		}
	}

	RecordTextureSet RequestRecordTextures(const MaterialRecord& a_record)
	{
		RecordTextureSet textures;
		for (const auto& spec : kRecordTextures) {
			const auto& path = a_record.material.Texture(spec.source);
			if (path.empty())
				continue;
			RE::NiPointer<RE::NiTexture> texture;
			// The engine passes allowDegrade false on its loader threads.
			RE::BSShaderManager::GetTexture(path.c_str(), true, texture, false, spec.srgb, false);
			if (!texture || engine::IsEngineDefaultTexture(texture.get())) {
				Warn(a_record, spec.source, path);
				continue;
			}
			textures[static_cast<std::size_t>(spec.target)] = std::move(texture);
		}
		return textures;
	}
}
