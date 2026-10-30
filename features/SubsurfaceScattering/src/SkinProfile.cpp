#include "SkinProfile.h"

#include "Log.h"
#include "Render/PrepassTechnique.h"

#include "RE/A/Actor.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BSRenderPass.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESRace.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace cs::features::subsurface_scattering
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.subsurfacescattering");

		// FO4: no IsBeastRace keyword; races with any of these are Base.
		constexpr std::array<std::string_view, 5> kBaseRaceKeywords{
			"ActorTypeGhoul",
			"ActorTypeSuperMutant",
			"ActorTypeSuperMutantBehemoth",
			"ActorTypeCreature",
			"ActorTypeAnimal"
		};

		std::array<const RE::BGSKeyword*, kBaseRaceKeywords.size()> g_baseRaceKeywords{};
		bool g_baseRaceKeywordsResolved = false;
	}

	void ResolveBaseRaceKeywords()
	{
		g_baseRaceKeywordsResolved = false;
		for (std::size_t i = 0; i < kBaseRaceKeywords.size(); ++i) {
			g_baseRaceKeywords[i] = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>(kBaseRaceKeywords[i]);
			if (!g_baseRaceKeywords[i]) {
				L->warn("{} keyword is unavailable; using the default SSS skin classification", kBaseRaceKeywords[i]);
				return;
			}
		}
		g_baseRaceKeywordsResolved = true;
	}

	bool IsHumanSkinDraw(cs::engine::PrepassDrawContext& a_draw) noexcept
	{
		auto* pass = a_draw.pass;
		namespace pt = cs::engine::prepass_technique;
		// FO4: the pass enum carries the skin flags; every prepass draw is deferred.
		if (!g_baseRaceKeywordsResolved || !pass || !(pass->passEnum & (pt::kSkinTint | pt::kFace)))
			return false;

		bool isBeastRace = true;

		// FO4: the actor is the geometry's scene-graph owner; there is no user data.
		if (auto* reference = RE::TESObjectREFR::FindReferenceFor3D(pass->geometry))
			if (auto* actor = reference->As<RE::Actor>())
				if (auto* race = actor->race)
					isBeastRace = std::ranges::any_of(g_baseRaceKeywords, [race](const RE::BGSKeyword* a_keyword) { return race->HasKeyword(a_keyword); });

		return !isBeastRace;
	}
}
