#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderVariantCompilation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	std::uint32_t g_preDrawInstallRequests = 0;
}

namespace cs::log
{
	spdlog::logger* Get(const char*)
	{
		return spdlog::default_logger_raw();
	}
}

namespace cs::engine
{
	std::shared_ptr<ShaderVariantCompilationPolicy>
		CreateEagerShaderVariantCompilationPolicy()
	{
		return {};
	}

	bool RegisterPixelShaderSwapResolver(ShaderSwapResolver)
	{
		return true;
	}

	bool RegisterPixelShaderSwapResolver(
		PixelShaderSwapResolverRegistration)
	{
		return true;
	}

	bool PixelShaderSwapBrokerHooksInstalled() noexcept
	{
		return true;
	}

	void EnsurePreSunLightDrawInstalled()
	{
		++g_preDrawInstallRequests;
	}
}

namespace
{
	using namespace cs::engine;

	ShaderReplacementVariantRegistration MakeRegistration(
		std::string a_name,
		std::uint32_t a_key,
		std::string a_sha1)
	{
		ShaderReplacementVariantRegistration registration;
		registration.targetId =
			ShaderInjectionTarget::kDeferredComposite;
		registration.name = std::move(a_name);
		registration.variantKeys.push_back({
			"RegistrationTestShader",
			ShaderStage::kPixel,
			ShaderVariantId{ a_key }
		});
		registration.expectedStockSha1 = std::move(a_sha1);
		registration.compilation.sourcePath = L"registration-test.hlsl";
		registration.compilation.entryPoint = "main";
		registration.compilation.profile = "ps_5_0";
		return registration;
	}

	bool Check(
		bool a_condition,
		std::string_view a_failure)
	{
		if (a_condition)
			return true;
		std::cerr << "FAIL: " << a_failure << '\n';
		return false;
	}

	bool TestStageScopedContributions()
	{
		const auto* target = GetShaderInjectionTarget(
			ShaderInjectionTarget::kAmbientIblPass);
		if (!Check(target != nullptr, "stage-scope target metadata is missing"))
			return false;

		std::vector<ShaderReplacementRegistration> contributions;
		contributions.push_back({
			.targetId = target->id,
			.contributor = "pixel-default",
			.defines = { { "PIXEL_DEFAULT", "1" } }
		});
		contributions.push_back({
			.targetId = target->id,
			.contributor = "pixel-second",
			.defines = { { "PIXEL_SECOND", "1" } }
		});
		contributions.push_back({
			.targetId = target->id,
			.stages = ShaderStageBit(ShaderStage::kVertex),
			.contributor = "vertex",
			.defines = { { "VERTEX_ONLY", "1" } }
		});
		contributions.push_back({
			.targetId = target->id,
			.stages = ShaderStageBit(ShaderStage::kVertex)
				| ShaderStageBit(ShaderStage::kPixel),
			.contributor = "both",
			.defines = { { "BOTH_STAGES", "1" } }
		});

		ShaderReplacementVariantRegistration pixelVariant;
		pixelVariant.targetId = target->id;
		pixelVariant.stage = ShaderStage::kPixel;
		pixelVariant.compilation.sourcePath = L"stage-scope.hlsl";
		pixelVariant.compilation.entryPoint = "main";
		pixelVariant.compilation.profile = "ps_5_0";
		ShaderReplacementVariantRegistration vertexVariant = pixelVariant;
		vertexVariant.stage = ShaderStage::kVertex;
		vertexVariant.compilation.profile = "vs_5_0";

		const auto pixelRequest =
			BuildEffectiveShaderCompileRequest(
				*target,
				pixelVariant,
				contributions);
		const auto vertexRequest =
			BuildEffectiveShaderCompileRequest(
				*target,
				vertexVariant,
				contributions);
		bool ok = Check(
			pixelRequest.has_value(),
			"pixel effective compile request failed");
		ok &= Check(
			vertexRequest.has_value(),
			"vertex effective compile request failed");
		if (!pixelRequest || !vertexRequest)
			return false;

		ok &= Check(
			!vertexRequest->defines.contains("PIXEL_DEFAULT"),
			"pixel-scoped define leaked into vertex request");
		ok &= Check(
			!vertexRequest->defines.contains("PIXEL_SECOND"),
			"second pixel-scoped define leaked into vertex request");
		ok &= Check(
			vertexRequest->defines.contains("VERTEX_ONLY"),
			"vertex-scoped define is missing from vertex request");
		ok &= Check(
			!pixelRequest->defines.contains("VERTEX_ONLY"),
			"vertex-scoped define leaked into pixel request");
		ok &= Check(
			vertexRequest->defines.contains("BOTH_STAGES")
				&& pixelRequest->defines.contains("BOTH_STAGES"),
			"both-stage define is missing from an effective request");
		ok &= Check(
			pixelRequest->defines.contains("PIXEL_DEFAULT")
				&& pixelRequest->defines.contains("PIXEL_SECOND"),
			"pixel-stage contributors did not union");
		return ok;
	}

	struct EffectiveDefinePartition
	{
		std::vector<std::size_t> shape;
		std::size_t variants = 0;
		bool anySentinel = false;
		bool allSentinel = true;
	};

	std::optional<EffectiveDefinePartition> BuildVertexDefinePartition(
		const ShaderInjectionTargetMetadata& a_target,
		std::span<const ShaderReplacementVariantRegistration> a_variants,
		std::span<const ShaderReplacementRegistration> a_contributions,
		std::string_view a_sentinel)
	{
		std::vector<std::pair<ShaderInjectionDefines, std::size_t>> groups;
		EffectiveDefinePartition partition;
		for (const auto& variant : a_variants) {
			if (variant.targetId != a_target.id
				|| variant.stage != ShaderStage::kVertex) {
				continue;
			}
			const auto request = BuildEffectiveShaderCompileRequest(
				a_target,
				variant,
				a_contributions);
			if (!request)
				return std::nullopt;

			++partition.variants;
			const bool hasSentinel =
				request->defines.contains(a_sentinel);
			partition.anySentinel =
				partition.anySentinel || hasSentinel;
			partition.allSentinel =
				partition.allSentinel && hasSentinel;
			auto group = std::ranges::find_if(
				groups,
				[&request](const auto& a_group) {
					return a_group.first == request->defines;
				});
			if (group == groups.end()) {
				groups.emplace_back(request->defines, 1);
			} else {
				++group->second;
			}
		}
		partition.shape.reserve(groups.size());
		for (const auto& group : groups)
			partition.shape.push_back(group.second);
		std::ranges::sort(partition.shape);
		return partition;
	}

	bool TestVertexCompileClassPartition()
	{
		const auto* target = GetShaderInjectionTarget(
			ShaderInjectionTarget::kBsWater);
		if (!Check(
				target != nullptr,
				"BSWater partition target metadata is missing")) {
			return false;
		}

		const auto variants = GetDefaultShaderReplacementVariants();
		const auto baseline = BuildVertexDefinePartition(
			*target,
			variants,
			{},
			"PIXEL_PARTITION_SENTINEL");
		const std::array pixelContribution{
			ShaderReplacementRegistration{
				.targetId = target->id,
				.contributor = "pixel-partition",
				.defines = {
					{ "PIXEL_PARTITION_SENTINEL", "1" },
					{ "VC", "1" }
				}
			}
		};
		const auto pixelScoped = BuildVertexDefinePartition(
			*target,
			variants,
			pixelContribution,
			"PIXEL_PARTITION_SENTINEL");
		auto vertexContribution = pixelContribution;
		vertexContribution.front().stages =
			ShaderStageBit(ShaderStage::kVertex);
		const auto vertexScoped = BuildVertexDefinePartition(
			*target,
			variants,
			vertexContribution,
			"PIXEL_PARTITION_SENTINEL");

		bool ok = Check(
			baseline.has_value()
				&& pixelScoped.has_value()
				&& vertexScoped.has_value(),
			"BSWater vertex partition could not be built");
		if (!baseline || !pixelScoped || !vertexScoped)
			return false;

		auto expectedVertexShape =
			std::vector<std::size_t>(14, 1);
		expectedVertexShape.push_back(2);
		ok &= Check(
			baseline->variants == 16
				&& baseline->shape
					== std::vector<std::size_t>(16, 1),
			"BSWater baseline vertex partition shape changed");
		ok &= Check(
			pixelScoped->shape == baseline->shape,
			"pixel-scoped contribution changed BSWater vertex compile-class partition");
		ok &= Check(
			!pixelScoped->anySentinel,
			"pixel partition sentinel leaked into a vertex request");
		ok &= Check(
			vertexScoped->shape == expectedVertexShape,
			"vertex-scoped contribution produced the wrong BSWater vertex compile-class partition");
		ok &= Check(
			vertexScoped->allSentinel,
			"vertex partition sentinel is missing from a vertex request");
		return ok;
	}

	int TestBaselineOwnershipWithoutContributors()
	{
		constexpr std::array ownableTargets{
			ShaderInjectionTarget::kDeferredPrepass,
			ShaderInjectionTarget::kBsdfLightDeferredPoint,
			ShaderInjectionTarget::kAmbientIblPass,
			ShaderInjectionTarget::kBsdfLightDeferredDirectional,
			ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl,
			ShaderInjectionTarget::kBsSky,
			ShaderInjectionTarget::kBsWater,
			ShaderInjectionTarget::kBsLighting
		};

		bool ok = true;
		for (const auto target : ownableTargets) {
			ok &= Check(
				SetBaselineShaderOwnership(target, true),
				"ownable baseline target was rejected");
		}
		ok &= Check(
			!SetBaselineShaderOwnership(
				ShaderInjectionTarget::kDeferredComposite,
				true),
			"deferred composite was accepted for baseline ownership");
		ok &= Check(
			!SetBaselineShaderOwnership(
				ShaderInjectionTarget::kVlsSliceScatter,
				true),
			"VLS slice scatter was accepted for baseline ownership");

		FreezeAndCompileShaderInjections(nullptr);
		for (const auto target : ownableTargets) {
			const auto snapshot =
				GetShaderInjectionTargetSnapshot(target);
			ok &= Check(
				snapshot.requested,
				"baseline ownership did not request target");
			ok &= Check(
				snapshot.contributors == 0,
				"baseline-only target gained a feature contributor");
			ok &= Check(
				snapshot.requestReasons
					== ShaderInjectionRequestReason::
						kBaselineOwnership,
				"baseline-only target has the wrong request reason");
			ok &= Check(
				!snapshot.compileAttempted,
				"null-device freeze attempted compilation");
		}

		const auto summary = GetShaderInjectionSummary();
		ok &= Check(
			summary.requested == ownableTargets.size(),
			"baseline request count mismatch");
		ok &= Check(
			summary.requestedByBaselineOwnership
				== ownableTargets.size(),
			"baseline request attribution count mismatch");
		ok &= Check(
			summary.requestedByFeatureContributor == 0,
			"baseline-only freeze reported feature requests");
		ok &= Check(
			summary.requestedByDeveloperForceOn == 0,
			"baseline-only freeze reported developer requests");
		if (!ok)
			return 1;
		std::cout
			<< "PASS: baseline shader ownership requests targets without contributors\n";
		return 0;
	}

	bool IsLowerHexSha1(std::string_view a_value)
	{
		return a_value.size() == 40
			&& std::ranges::all_of(a_value, [](char a_character) {
				return (a_character >= '0' && a_character <= '9')
					|| (a_character >= 'a' && a_character <= 'f');
			});
	}

	bool RequiresStockHash(ShaderInjectionTarget a_target)
	{
		switch (a_target) {
		case ShaderInjectionTarget::kDeferredPrepass:
		case ShaderInjectionTarget::kBsdfLightDeferredPoint:
		case ShaderInjectionTarget::kAmbientIblPass:
		case ShaderInjectionTarget::kBsdfLightDeferredDirectional:
		case ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl:
		case ShaderInjectionTarget::kBsSky:
		case ShaderInjectionTarget::kBsWater:
		case ShaderInjectionTarget::kBsLighting:
			return true;
		default:
			return false;
		}
	}

}

int main(int a_argc, char* a_argv[])
{
	if (a_argc == 2
		&& std::string_view(a_argv[1])
			== "--baseline-ownership") {
		return TestBaselineOwnershipWithoutContributors();
	}
	if (a_argc != 1) {
		std::cerr << "FAIL: invalid arguments\n";
		return 1;
	}

	bool ok = TestStageScopedContributions();
	ok &= TestVertexCompileClassPartition();

	ShaderReplacementRegistration emptyStageMask;
	emptyStageMask.targetId =
		ShaderInjectionTarget::kDeferredPrepass;
	emptyStageMask.stages = 0;
	emptyStageMask.contributor = "empty-stage-mask";
	emptyStageMask.bind = [](ID3D11DeviceContext*) {};
	ok &= Check(
		!RegisterReplacement(std::move(emptyStageMask)),
		"empty contribution stage mask was accepted");
	ok &= Check(
		g_preDrawInstallRequests == 0,
		"empty contribution stage mask installed the pre-draw hook");

	ShaderReplacementRegistration invalidStageMask;
	invalidStageMask.targetId =
		ShaderInjectionTarget::kDeferredPrepass;
	invalidStageMask.stages =
		ShaderStageBit(ShaderStage::kCount);
	invalidStageMask.contributor = "invalid-stage-mask";
	ok &= Check(
		!RegisterReplacement(std::move(invalidStageMask)),
		"out-of-range contribution stage mask was accepted");

	ShaderReplacementRegistration disabledWetnessAmbient;
	disabledWetnessAmbient.targetId = ShaderInjectionTarget::kAmbientIblPass;
	disabledWetnessAmbient.contributor = "WetnessEffects";
	disabledWetnessAmbient.bind = [](ID3D11DeviceContext*) {};
	ok &= Check(
		RegisterReplacementIfEnabled(
			false,
			std::move(disabledWetnessAmbient)),
		"disabled WetnessEffects ambient registration failed");

	ShaderReplacementRegistration disabledSsgiAmbient;
	disabledSsgiAmbient.targetId = ShaderInjectionTarget::kAmbientIblPass;
	disabledSsgiAmbient.contributor = "ScreenSpaceGI";
	disabledSsgiAmbient.bind = [](ID3D11DeviceContext*) {};
	ShaderReplacementVariantRegistration mismatchedProfile;
	mismatchedProfile.targetId = ShaderInjectionTarget::kDeferredComposite;
	mismatchedProfile.name = "mismatched-profile";
	mismatchedProfile.stage = ShaderStage::kVertex;
	mismatchedProfile.compilation.sourcePath = L"registration-test.hlsl";
	mismatchedProfile.compilation.entryPoint = "main";
	mismatchedProfile.compilation.profile = "ps_5_0";
	ok &= Check(
		!RegisterReplacementVariant(std::move(mismatchedProfile)),
		"vertex registration accepted a pixel profile");

	auto vertexKeyRegistration = MakeRegistration(
		"vertex-key-without-resolver",
		1,
		"1111111111111111111111111111111111111111");
	vertexKeyRegistration.stage = ShaderStage::kVertex;
	vertexKeyRegistration.variantKeys.front().stage =
		ShaderStage::kVertex;
	vertexKeyRegistration.compilation.profile = "vs_5_0";
	ok &= Check(
		!RegisterReplacementVariant(std::move(vertexKeyRegistration)),
		"vertex registration accepted an inert variant key");

	const auto staticFamilies = GetDefaultShaderReplacementVariants();
	std::map<ShaderInjectionTarget, std::size_t, std::less<>> familyCounts;
	std::map<ShaderInjectionTarget, std::size_t, std::less<>>
		vertexFamilyCounts;
	std::set<std::string, std::less<>> stockHashes;
	for (const auto& registration : staticFamilies) {
		const auto expectedProfile =
			registration.stage == ShaderStage::kVertex
			? "vs_5_0"
			: "ps_5_0";
		ok &= Check(
			registration.compilation.profile == expectedProfile,
			"registration profile does not match its shader stage");
		ok &= Check(
			std::ranges::all_of(
				registration.variantKeys,
				[&registration](const ShaderVariantKey& a_key) {
					return a_key.stage == registration.stage;
				}),
			"registration key does not match its shader stage");
		if (RequiresStockHash(registration.targetId)) {
			ok &= Check(
				IsLowerHexSha1(registration.expectedStockSha1),
				"baseline-ownable registration lacks a lowercase 40-hex stock hash");
		} else if (!registration.expectedStockSha1.empty()) {
			ok &= Check(
				IsLowerHexSha1(registration.expectedStockSha1),
				"stock hash is not lowercase 40-hex");
		}
		if (!registration.expectedStockSha1.empty()) {
			ok &= Check(
				stockHashes.insert(registration.expectedStockSha1).second,
				"stock hash is claimed by more than one registration");
		}
		switch (registration.targetId) {
		case ShaderInjectionTarget::kBsSky:
		case ShaderInjectionTarget::kBsWater:
		case ShaderInjectionTarget::kBsLighting:
			if (registration.stage == ShaderStage::kPixel)
				++familyCounts[registration.targetId];
			else
				++vertexFamilyCounts[registration.targetId];
			break;
		default:
			break;
		}
	}
	ok &= Check(
		familyCounts[ShaderInjectionTarget::kBsSky] == 9,
		"BSSky registration count mismatch");
	ok &= Check(
		familyCounts[ShaderInjectionTarget::kBsWater] == 38,
		"BSWater registration count mismatch");
	ok &= Check(
		familyCounts[ShaderInjectionTarget::kBsLighting] == 12,
		"BSLighting registration count mismatch");
	ok &= Check(
		vertexFamilyCounts[ShaderInjectionTarget::kBsSky] == 7,
		"BSSky vertex representative count mismatch");
	ok &= Check(
		vertexFamilyCounts[ShaderInjectionTarget::kBsWater] == 16,
		"BSWater vertex representative count mismatch");
	ok &= Check(
		vertexFamilyCounts[ShaderInjectionTarget::kBsLighting] == 8,
		"BSLighting vertex representative count mismatch");
	ok &= Check(
		staticFamilies.size() == 98,
		"default shader replacement variant count mismatch");
	ok &= Check(
		stockHashes.size() == 96,
		"default shader replacement variant non-empty stock hash count mismatch");

	constexpr std::array<std::pair<ShaderInjectionTarget, std::wstring_view>, 3>
		kStaticFamilySources{ {
			{ ShaderInjectionTarget::kBsSky, L"BSSkyShader.hlsl" },
			{ ShaderInjectionTarget::kBsWater, L"BSWaterShader.hlsl" },
			{ ShaderInjectionTarget::kBsLighting, L"BSLightingShader.hlsl" }
		} };
	for (const auto& [target, sourcePath] : kStaticFamilySources) {
		const auto* metadata = GetShaderInjectionTarget(target);
		ok &= Check(
			metadata != nullptr,
			"static family target metadata is missing");
		if (metadata == nullptr)
			continue;
		ok &= Check(
			metadata->sourcePath == sourcePath,
			"static family source path mismatch");
		ok &= Check(
			metadata->entryPoint == "main",
			"static family entry point mismatch");
		ok &= Check(
			metadata->profile == "ps_5_0",
			"static family profile mismatch");
	}
	ok &= Check(
		RegisterReplacementIfEnabled(
			false,
			std::move(disabledSsgiAmbient)),
		"disabled ScreenSpaceGI ambient registration failed");
	ok &= Check(
		g_preDrawInstallRequests == 0,
		"disabled ambient registrations installed the pre-draw hook");

	ShaderReplacementRegistration noBindRegistration;
	noBindRegistration.targetId = ShaderInjectionTarget::kDeferredPrepass;
	noBindRegistration.contributor = "registration-no-bind";
	ok &= Check(
		RegisterReplacement(std::move(noBindRegistration)),
		"registration without a bind was rejected");
	ok &= Check(
		g_preDrawInstallRequests == 0,
		"registration without a bind installed the pre-draw hook");

	ShaderReplacementRegistration bindRegistration;
	bindRegistration.targetId = ShaderInjectionTarget::kAmbientIblPass;
	bindRegistration.contributor = "registration-with-bind";
	bindRegistration.bind = [](ID3D11DeviceContext*) {};
	ok &= Check(
		RegisterReplacement(std::move(bindRegistration)),
		"registration with a bind was rejected");
	ok &= Check(
		g_preDrawInstallRequests == 1,
		"registration with a bind did not install the pre-draw hook");

	constexpr auto baseSha =
		"1111111111111111111111111111111111111111";
	ok &= Check(
		RegisterReplacementVariant(
			MakeRegistration("registration-base", 0xABC001, baseSha)),
		"valid baseline registration was rejected");
	ok &= Check(
		!RegisterReplacementVariant(
			MakeRegistration(
				"duplicate-key",
				0xABC001,
				"2222222222222222222222222222222222222222")),
		"duplicate scoped key was accepted");
	ok &= Check(
		!RegisterReplacementVariant(
			MakeRegistration(
				"registration-base",
				0xABC002,
				"3333333333333333333333333333333333333333")),
		"duplicate target/name was accepted");
	ok &= Check(
		!RegisterReplacementVariant(
			MakeRegistration("duplicate-sha", 0xABC003, baseSha)),
		"duplicate expected stock SHA1 was accepted");
	if (!ok)
		return 1;
	std::cout << "PASS: shader injection registration guards\n";
	return 0;
}
