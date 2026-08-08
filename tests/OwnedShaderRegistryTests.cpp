#include "Render/OwnedShaderRegistry.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace cs::engine;
	namespace sha1 = cs::sha1;

	constexpr std::string_view kSubclass = "BSLightingShader";

	std::string StockSha(char a_fill)
	{
		return std::string(40, a_fill);
	}

	sha1::Sha1Result StockShaBytes(char a_fill)
	{
		sha1::Sha1Result result;
		sha1::Sha1FromHex(StockSha(a_fill), result);
		return result;
	}

	ShaderVariantKey VariantKey(std::uint32_t a_psid)
	{
		return {
			std::string(kSubclass),
			ShaderStage::kPixel,
			ShaderVariantId{ a_psid }
		};
	}

	ShaderVariantKeyView VariantKeyView(std::uint32_t a_psid)
	{
		return { kSubclass, ShaderStage::kPixel, ShaderVariantId{ a_psid } };
	}

	OwnedShaderSourceDesc MakeSource(std::string a_name)
	{
		return {
			std::move(a_name),
			L"lighting\\bsdf_light_deferred.hlsl",
			"main",
			"ps_5_0"
		};
	}

	OwnedShaderIdentityDesc MakeIdentity(
		std::string a_name,
		std::string a_source,
		std::string a_stockSha1)
	{
		OwnedShaderIdentityDesc identity;
		identity.name = std::move(a_name);
		identity.subclass = std::string(kSubclass);
		identity.stage = ShaderStage::kPixel;
		identity.sourceName = std::move(a_source);
		identity.nativeDefines = { { "DIRSPLITS2", "1" } };
		identity.expectedStockSha1 = std::move(a_stockSha1);
		identity.runtime = OwnedShaderRuntime::kAe1_11_221;
		return identity;
	}

	OwnedShaderDeliveryDesc MakeDelivery(
		std::string a_identity,
		std::string a_group,
		std::uint32_t a_psid,
		OwnedShaderBindAnchor a_anchor = OwnedShaderBindAnchor::kNone)
	{
		OwnedShaderDeliveryDesc delivery;
		delivery.identityName = std::move(a_identity);
		delivery.group = std::move(a_group);
		delivery.variantKey = VariantKey(a_psid);
		delivery.bindAnchor = a_anchor;
		return delivery;
	}

	OwnedShaderContributionDesc MakeContribution(
		std::string a_group,
		std::string a_contributor,
		ShaderInjectionDefines a_defines = {},
		std::vector<ShaderSlotClaim> a_slotClaims = {})
	{
		OwnedShaderContributionDesc contribution;
		contribution.group = std::move(a_group);
		contribution.contributor = std::move(a_contributor);
		contribution.defines = std::move(a_defines);
		contribution.slotClaims = std::move(a_slotClaims);
		return contribution;
	}

	ShaderSlotClaim SrvClaim(std::uint32_t a_slot)
	{
		return {
			ShaderStage::kPixel,
			ShaderResourceType::kShaderResource,
			a_slot
		};
	}

	bool Check(bool a_condition, std::string_view a_failure)
	{
		if (a_condition)
			return true;
		std::cerr << "FAIL: " << a_failure << '\n';
		return false;
	}

	template <class Seed>
	bool ExpectFreeze(
		std::string_view a_case,
		OwnedShaderFreezeStatus a_expected,
		Seed a_seed,
		std::span<const std::string> a_legacyStockSha1 = {})
	{
		OwnedShaderRegistry registry;
		a_seed(registry);

		const auto result = registry.Freeze(a_legacyStockSha1);
		if (result.status != a_expected) {
			std::cerr << "FAIL: " << a_case << " expected "
					  << OwnedShaderFreezeStatusName(a_expected) << " got "
					  << OwnedShaderFreezeStatusName(result.status) << " ("
					  << result.subject << ")\n";
			return false;
		}

		const bool frozen = registry.Frozen();
		if (frozen != result.Ok()) {
			std::cerr << "FAIL: " << a_case
					  << " freeze status and Frozen() disagree\n";
			return false;
		}
		if (!frozen
			&& registry.Resolve(StockShaBytes('1'), std::nullopt).kind
				!= OwnedShaderResolutionKind::kNoMatch) {
			std::cerr << "FAIL: " << a_case
					  << " rejected registry still resolves\n";
			return false;
		}
		return true;
	}

	void SeedTiles(OwnedShaderRegistry& a_registry)
	{
		a_registry.AddSource(MakeSource("bsdf"));
		a_registry.AddSource(MakeSource("ambient"));
		a_registry.AddIdentity(
			MakeIdentity("tile-a", "bsdf", StockSha('1')));
		a_registry.AddIdentity(
			MakeIdentity("tile-b", "bsdf", StockSha('2')));
		a_registry.AddIdentity(
			MakeIdentity("ibl", "ambient", StockSha('3')));
		a_registry.AddDelivery(MakeDelivery(
			"tile-a", "sss", 0x2001,
			OwnedShaderBindAnchor::kPreSunLightDraw));
		a_registry.AddDelivery(MakeDelivery(
			"tile-b", "sss", 0x2002,
			OwnedShaderBindAnchor::kPreSunLightDraw));
		a_registry.AddDelivery(MakeDelivery("ibl", "ibl-pass", 0x3001));
		a_registry.AddContribution(MakeContribution(
			"sss",
			"ScreenSpaceShadows",
			{ { "SSS_ENABLED", "1" } },
			{ SrvClaim(41) }));
		a_registry.AddContribution(MakeContribution(
			"sss",
			"WetnessEffects",
			{ { "SSS_ENABLED", "1" }, { "WETNESS", "1" } },
			{ SrvClaim(42) }));
	}

	bool FreezeGuards()
	{
		const std::vector<std::string> legacy{ StockSha('2') };
		const std::vector<std::string> malformedLegacy{ "not-a-sha1" };

		bool ok = ExpectFreeze(
			"valid registry",
			OwnedShaderFreezeStatus::kOk,
			SeedTiles);

		ok &= ExpectFreeze(
			"duplicate source",
			OwnedShaderFreezeStatus::kDuplicateSource,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddSource(MakeSource("bsdf"));
			});

		ok &= ExpectFreeze(
			"duplicate identity",
			OwnedShaderFreezeStatus::kDuplicateIdentity,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddIdentity(
					MakeIdentity("tile-a", "bsdf", StockSha('4')));
			});

		ok &= ExpectFreeze(
			"unknown source",
			OwnedShaderFreezeStatus::kUnknownSource,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddIdentity(
					MakeIdentity("orphan", "missing", StockSha('4')));
			});

		ok &= ExpectFreeze(
			"malformed identity sha1",
			OwnedShaderFreezeStatus::kMalformedStockSha1,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddIdentity(
					MakeIdentity("short-sha", "bsdf", "abcdef"));
			});

		ok &= ExpectFreeze(
			"zero identity sha1",
			OwnedShaderFreezeStatus::kMalformedStockSha1,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddIdentity(
					MakeIdentity("zero-sha", "bsdf", StockSha('0')));
			});

		ok &= ExpectFreeze(
			"duplicate stock sha1",
			OwnedShaderFreezeStatus::kDuplicateStockSha1,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddIdentity(
					MakeIdentity("clone", "bsdf", StockSha('1')));
			});

		ok &= ExpectFreeze(
			"legacy stock sha1 overlap",
			OwnedShaderFreezeStatus::kLegacyStockSha1Overlap,
			SeedTiles,
			legacy);

		ok &= ExpectFreeze(
			"malformed legacy sha1",
			OwnedShaderFreezeStatus::kMalformedStockSha1,
			SeedTiles,
			malformedLegacy);

		ok &= ExpectFreeze(
			"unknown delivery identity",
			OwnedShaderFreezeStatus::kUnknownDeliveryIdentity,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddDelivery(
					MakeDelivery("tile-c", "sss", 0x2003));
			});

		ok &= ExpectFreeze(
			"duplicate delivery identity",
			OwnedShaderFreezeStatus::kDuplicateDeliveryIdentity,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddDelivery(
					MakeDelivery("tile-a", "sss", 0x2004));
			});

		ok &= ExpectFreeze(
			"conflicting delivery key",
			OwnedShaderFreezeStatus::kConflictingDeliveryKey,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddIdentity(
					MakeIdentity("tile-c", "bsdf", StockSha('4')));
				a_registry.AddDelivery(
					MakeDelivery("tile-c", "sss", 0x2001));
			});

		ok &= ExpectFreeze(
			"contribution group without delivery",
			OwnedShaderFreezeStatus::kGroupWithoutDelivery,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddContribution(
					MakeContribution("skylighting", "Skylighting"));
			});

		ok &= ExpectFreeze(
			"duplicate contributor",
			OwnedShaderFreezeStatus::kDuplicateContributor,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddContribution(
					MakeContribution("sss", "ScreenSpaceShadows"));
			});

		ok &= ExpectFreeze(
			"conflicting define",
			OwnedShaderFreezeStatus::kConflictingDefine,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddContribution(MakeContribution(
					"sss",
					"Skylighting",
					{ { "SSS_ENABLED", "2" } }));
			});

		ok &= ExpectFreeze(
			"conflicting slot across contributors",
			OwnedShaderFreezeStatus::kConflictingSlot,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddContribution(MakeContribution(
					"sss", "Skylighting", {}, { SrvClaim(41) }));
			});

		ok &= ExpectFreeze(
			"conflicting slot within a contributor",
			OwnedShaderFreezeStatus::kConflictingSlot,
			[](OwnedShaderRegistry& a_registry) {
				SeedTiles(a_registry);
				a_registry.AddContribution(MakeContribution(
					"sss",
					"Skylighting",
					{},
					{ SrvClaim(50), SrvClaim(50) }));
			});

		return ok;
	}

	bool FrozenShape()
	{
		OwnedShaderRegistry registry;
		SeedTiles(registry);
		bool ok = Check(registry.Freeze().Ok(), "tile registry froze dirty");
		if (!ok)
			return false;

		ok &= Check(
			registry.Identities().size() == 3,
			"identity count mismatch");
		ok &= Check(registry.Groups().size() == 2, "group count mismatch");

		const auto* sourceA = registry.SourceFor("tile-a");
		const auto* sourceB = registry.SourceFor("tile-b");
		const auto* sourceIbl = registry.SourceFor("ibl");
		ok &= Check(
			sourceA != nullptr && sourceA == sourceB,
			"tile siblings did not share one source");
		ok &= Check(
			sourceA != nullptr && sourceA->name == "bsdf"
				&& sourceA->entryPoint == "main"
				&& sourceA->profile == "ps_5_0"
				&& sourceA->relativePath == L"lighting\\bsdf_light_deferred.hlsl",
			"shared source payload mismatch");
		ok &= Check(
			sourceIbl != nullptr && sourceIbl->name == "ambient",
			"second source did not resolve");
		ok &= Check(
			registry.SourceFor("tile-c") == nullptr,
			"unknown identity produced a source");

		const auto* group = registry.Group("sss");
		ok &= Check(group != nullptr, "sss group missing");
		if (group) {
			ok &= Check(
				group->identities.size() == 2
					&& group->identities[0] == "tile-a"
					&& group->identities[1] == "tile-b",
				"sss group membership mismatch");
			ok &= Check(
				group->contributors.size() == 2,
				"sss contributor count mismatch");
			ok &= Check(
				group->defines.size() == 2
					&& group->defines.at("SSS_ENABLED") == "1"
					&& group->defines.at("WETNESS") == "1",
				"sss merged defines mismatch");
			ok &= Check(
				group->slotClaims.size() == 2,
				"sss merged slot claims mismatch");
		}

		const auto* iblGroup = registry.Group("ibl-pass");
		ok &= Check(
			iblGroup != nullptr && iblGroup->identities.size() == 1
				&& iblGroup->contributors.empty(),
			"ibl group shape mismatch");
		ok &= Check(
			registry.Group("skylighting") == nullptr,
			"unknown group resolved");

		ok &= Check(
			registry.Identities()[0].runtime
				== OwnedShaderRuntime::kAe1_11_221,
			"identity runtime profile lost");
		ok &= Check(
			OwnedShaderRuntimeName(OwnedShaderRuntime::kAe1_11_221)
				== "1.11.221",
			"runtime profile name mismatch");
		ok &= Check(
			OwnedShaderRuntimeName(OwnedShaderRuntime::kNg1_10_980)
				== "1.10.980",
			"NG runtime profile name mismatch");
		return ok;
	}

	bool ResolutionFlow()
	{
		OwnedShaderRegistry registry;
		SeedTiles(registry);
		bool ok = Check(registry.Freeze().Ok(), "tile registry froze dirty");
		if (!ok)
			return false;

		ok &= Check(
			registry.Resolve(StockShaBytes('9'), VariantKeyView(0x2001)).kind
				== OwnedShaderResolutionKind::kNoMatch,
			"unknown stock sha1 matched");

		const auto notReady =
			registry.Resolve(StockShaBytes('1'), VariantKeyView(0x2001));
		ok &= Check(
			notReady.kind == OwnedShaderResolutionKind::kBaselineNotReady,
			"unprepared baseline did not report not-ready");
		ok &= Check(
			notReady.identity == "tile-a" && notReady.group == "sss"
				&& notReady.bindAnchor
					== OwnedShaderBindAnchor::kPreSunLightDraw,
			"not-ready result lost identity, group or bind anchor");
		ok &= Check(
			!OwnedShaderSubstitutes(notReady.kind),
			"not-ready baseline claimed substitution");

		ok &= Check(
			registry.SetBaselineReady("tile-a", true)
				&& registry.SetBaselineReady("tile-b", true)
				&& registry.SetBaselineReady("ibl", true),
			"baseline readiness setters failed");
		ok &= Check(
			!registry.SetBaselineReady("tile-c", true),
			"baseline readiness accepted an unknown identity");
		ok &= Check(
			!registry.SetGroupReady("skylighting", true),
			"group readiness accepted an unknown group");

		const auto baseline =
			registry.Resolve(StockShaBytes('1'), VariantKeyView(0x2001));
		ok &= Check(
			baseline.kind == OwnedShaderResolutionKind::kBaseline
				&& OwnedShaderSubstitutes(baseline.kind),
			"exact stock sha1 did not select the baseline");

		ok &= Check(
			registry.SetGroupReady("sss", true)
				&& registry.SetOverlayReady("tile-a", true),
			"overlay readiness setters failed");
		ok &= Check(
			registry.Resolve(StockShaBytes('1'), VariantKeyView(0x2001)).kind
				== OwnedShaderResolutionKind::kBaseline,
			"overlay ran with one tile sibling unprepared");

		ok &= Check(
			registry.SetOverlayReady("tile-b", true),
			"sibling overlay readiness setter failed");

		const auto overlay =
			registry.Resolve(StockShaBytes('1'), VariantKeyView(0x2001));
		ok &= Check(
			overlay.kind == OwnedShaderResolutionKind::kOverlay
				&& overlay.identity == "tile-a" && overlay.group == "sss"
				&& overlay.bindAnchor
					== OwnedShaderBindAnchor::kPreSunLightDraw,
			"route-gated overlay selection failed");
		ok &= Check(
			registry.Resolve(StockShaBytes('2'), VariantKeyView(0x2002)).kind
				== OwnedShaderResolutionKind::kOverlay,
			"sibling tile did not reach the overlay");

		ok &= Check(
			registry.Resolve(StockShaBytes('1'), std::nullopt).kind
				== OwnedShaderResolutionKind::kBaseline,
			"missing runtime key did not fall back to the baseline");
		ok &= Check(
			registry.Resolve(StockShaBytes('1'), VariantKeyView(0x2002)).kind
				== OwnedShaderResolutionKind::kBaseline,
			"wrong runtime key did not fall back to the baseline");

		ok &= Check(
			registry.SetOverlayReady("tile-b", false),
			"sibling overlay teardown failed");
		ok &= Check(
			registry.Resolve(StockShaBytes('1'), VariantKeyView(0x2001)).kind
				== OwnedShaderResolutionKind::kBaseline,
			"one failed tile sibling did not force the group to baseline");

		ok &= Check(
			registry.SetOverlayReady("tile-b", true)
				&& registry.SetGroupReady("sss", false),
			"group readiness teardown failed");
		ok &= Check(
			registry.Resolve(StockShaBytes('1'), VariantKeyView(0x2001)).kind
				== OwnedShaderResolutionKind::kBaseline,
			"unready contribution group did not fall back to the baseline");

		const auto ibl =
			registry.Resolve(StockShaBytes('3'), VariantKeyView(0x3001));
		ok &= Check(
			ibl.kind == OwnedShaderResolutionKind::kBaseline
				&& ibl.group == "ibl-pass"
				&& ibl.bindAnchor == OwnedShaderBindAnchor::kNone,
			"anchorless route lost its bind anchor or group");

		ok &= Check(
			registry.SetBaselineReady("tile-a", false),
			"baseline teardown failed");
		ok &= Check(
			registry.SetGroupReady("sss", true),
			"group readiness restore failed");
		ok &= Check(
			registry.Resolve(StockShaBytes('1'), VariantKeyView(0x2001)).kind
				== OwnedShaderResolutionKind::kBaselineNotReady,
			"overlay ran without its baseline fallback");
		ok &= Check(
			registry.Resolve(StockShaBytes('2'), VariantKeyView(0x2002)).kind
				== OwnedShaderResolutionKind::kBaseline,
			"one missing sibling baseline did not demote the whole group");
		return ok;
	}

	bool LateMutation()
	{
		OwnedShaderRegistry collecting;
		SeedTiles(collecting);
		bool ok = Check(
			!collecting.SetBaselineReady("tile-a", true)
				&& !collecting.SetOverlayReady("tile-a", true)
				&& !collecting.SetGroupReady("sss", true),
			"readiness setters ran before freeze");
		ok &= Check(
			collecting.SourceFor("tile-a") == nullptr
				&& collecting.Group("sss") == nullptr
				&& collecting.Identities().empty()
				&& collecting.Groups().empty(),
			"frozen accessors answered before freeze");
		ok &= Check(
			collecting.Resolve(StockShaBytes('1'), VariantKeyView(0x2001)).kind
				== OwnedShaderResolutionKind::kNoMatch,
			"resolution answered before freeze");

		OwnedShaderRegistry registry;
		SeedTiles(registry);
		ok &= Check(registry.Freeze().Ok(), "tile registry froze dirty");
		ok &= Check(
			registry.Freeze().status
				== OwnedShaderFreezeStatus::kAlreadyFrozen,
			"freeze was not one-shot");
		ok &= Check(
			!registry.AddSource(MakeSource("late"))
				&& !registry.AddIdentity(
					MakeIdentity("late", "bsdf", StockSha('4')))
				&& !registry.AddDelivery(
					MakeDelivery("late", "sss", 0x2005))
				&& !registry.AddContribution(
					MakeContribution("sss", "Late")),
			"registration survived the freeze");
		ok &= Check(
			registry.Identities().size() == 3,
			"late registration mutated the frozen registry");

		OwnedShaderRegistry rejected;
		SeedTiles(rejected);
		rejected.AddIdentity(MakeIdentity("tile-a", "bsdf", StockSha('4')));
		ok &= Check(
			!rejected.Freeze().Ok(),
			"invalid registry froze clean");
		ok &= Check(
			rejected.Identities().empty() && rejected.Groups().empty(),
			"rejected registry exposed its registrations");
		ok &= Check(
			rejected.Freeze().status
				== OwnedShaderFreezeStatus::kAlreadyFrozen,
			"rejected registry accepted a second freeze");
		ok &= Check(
			!rejected.AddSource(MakeSource("late")),
			"rejected registry accepted registration");
		return ok;
	}
}

int main()
{
	bool ok = FreezeGuards();
	ok &= FrozenShape();
	ok &= ResolutionFlow();
	ok &= LateMutation();
	if (!ok)
		return 1;
	std::cout << "PASS: owned shader registry\n";
	return 0;
}
