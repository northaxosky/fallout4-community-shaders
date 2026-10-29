#include "SkinProfile.h"

#include "Log.h"
#include "Render/PrepassTechnique.h"

#include "RE/A/Actor.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BSRenderPass.h"
#include "RE/T/TESDataHandler.h"
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

		// FO4: no IsBeastRace keyword; races with these Fallout4.esm keywords are Base.
		struct BaseRaceKeyword
		{
			RE::TESFormID formID;
			std::string_view name;
		};
		constexpr std::array kBaseRaceKeywords{
			BaseRaceKeyword{ 0x000EAFB7, "ActorTypeGhoul" },
			BaseRaceKeyword{ 0x0006D7B6, "ActorTypeSuperMutant" },
			BaseRaceKeyword{ 0x0014F6A5, "ActorTypeSuperMutantBehemoth" },
			BaseRaceKeyword{ 0x00013795, "ActorTypeCreature" },
			BaseRaceKeyword{ 0x00013798, "ActorTypeAnimal" }
		};

		// Written once at DataLoaded, before the first render.
		std::array<const RE::BGSKeyword*, kBaseRaceKeywords.size()> g_baseRaceKeywords{};
		bool g_baseRaceKeywordsResolved = false;
	}

	void ResolveBaseRaceKeywords()
	{
		g_baseRaceKeywordsResolved = false;
		auto* handler = RE::TESDataHandler::GetSingleton();
		for (std::size_t i = 0; i < kBaseRaceKeywords.size(); ++i) {
			g_baseRaceKeywords[i] = handler ? handler->LookupForm<RE::BGSKeyword>(kBaseRaceKeywords[i].formID, "Fallout4.esm") : nullptr;
			if (!g_baseRaceKeywords[i]) {
				L->warn("{} keyword is unavailable; using the default SSS skin classification", kBaseRaceKeywords[i].name);
				return;
			}
		}
		g_baseRaceKeywordsResolved = true;
	}

	bool IsHumanSkinDraw(RE::BSRenderPass* a_pass, cs::engine::PrepassBakePath) noexcept
	{
		namespace pt = cs::engine::prepass_technique;
		// FO4: the pass enum carries the skin flags; every prepass draw is deferred.
		if (!g_baseRaceKeywordsResolved || !a_pass || !(a_pass->passEnum & (pt::kSkinTint | pt::kFace)))
			return false;

		bool isBeastRace = true;

		// FO4: the actor is the geometry's scene-graph owner; there is no user data.
		if (auto* reference = RE::TESObjectREFR::FindReferenceFor3D(a_pass->geometry))
			if (auto* actor = reference->As<RE::Actor>())
				if (auto* race = actor->race)
					isBeastRace = std::ranges::any_of(g_baseRaceKeywords, [race](const RE::BGSKeyword* a_keyword) { return race->HasKeyword(a_keyword); });

		return !isBeastRace;
	}
}
