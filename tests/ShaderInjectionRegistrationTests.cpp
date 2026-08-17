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

	ShaderReplacementRegistration disabledWetnessAmbient;
	disabledWetnessAmbient.targetId = ShaderInjectionTarget::kAmbientIblPass;
	disabledWetnessAmbient.contributor = "WetnessEffects";
	disabledWetnessAmbient.bind = [](ID3D11DeviceContext*) {};
	bool ok = Check(
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
