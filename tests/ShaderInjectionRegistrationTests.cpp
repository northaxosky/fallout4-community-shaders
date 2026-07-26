#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderVariantCompilation.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	std::uint32_t g_preDrawInstallRequests = 0;
	ID3D11PixelShader* g_patchedShader = nullptr;
	bool g_patchMatcherActive = false;
	std::uint32_t g_sssBinds = 0;
	std::uint32_t g_wetnessBinds = 0;
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

	bool RegisterPixelShaderSwapResolver(PixelShaderSwapResolver)
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

	bool MatchPatchedShader(
		ShaderInjectionTarget a_target,
		ID3D11PixelShader* a_shader) noexcept
	{
		return g_patchMatcherActive
			&& (a_target
					== ShaderInjectionTarget::
						kBsdfLightDeferredDirectional
				|| a_target
					== ShaderInjectionTarget::
						kBsdfLightDeferredDirectionalIbl)
			&& a_shader == g_patchedShader;
	}

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
}

int main()
{
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

	ShaderPatchedDispatchRegistration patchedRegistration;
	patchedRegistration.targetId =
		ShaderInjectionTarget::kBsdfLightDeferredDirectional;
	patchedRegistration.contributor =
		"ScreenSpaceShadows.DxbcPatch";
	patchedRegistration.matches = &MatchPatchedShader;
	patchedRegistration.bind = [](ID3D11DeviceContext*) {
		++g_sssBinds;
	};
	patchedRegistration.slotClaims.push_back({
		.stage = ShaderStage::kPixel,
		.resourceType = ShaderResourceType::kShaderResource,
		.slot = 6
	});
	ok &= Check(
		RegisterPatchedShaderDispatch(patchedRegistration),
		"separate patched dispatch registration was rejected");
	auto patchedIblRegistration = patchedRegistration;
	patchedIblRegistration.targetId =
		ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl;
	ok &= Check(
		RegisterPatchedShaderDispatch(
			std::move(patchedIblRegistration)),
		"second exclusive directional claim was rejected");
	ok &= Check(
		g_preDrawInstallRequests == 3,
		"both patched dispatch claims did not install the pre-draw hook");
	ok &= Check(
		!RegisterPatchedShaderDispatch(std::move(patchedRegistration)),
		"duplicate patched dispatch was accepted");
	const std::array patchTargets{
		ShaderInjectionTarget::kBsdfLightDeferredDirectional,
		ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl
	};
	ok &= Check(
		!ArePatchedShaderDispatchesPublished(patchTargets),
		"patched dispatch reported ready before freeze publication");
	ok &= Check(
		SetShaderInjectionEnabled(false)
			&& !PublishShaderInjectionFreezePreview()
			&& !ArePatchedShaderDispatchesPublished(patchTargets),
		"disabled shader injection published patched dispatch targets");
	ok &= Check(
		SetShaderInjectionEnabled(true),
		"shader injection could not be re-enabled after kill-switch test");

	ok &= Check(
		SetDeveloperShaderOverride(
			ShaderInjectionTarget::kBsdfLightDeferredDirectional,
			DeveloperShaderOverride::kForceOn),
		"directional developer force-on setup was rejected");
	const auto developerExclusivePreview = PreviewShaderInjectionFreeze(
		ShaderInjectionTarget::kBsdfLightDeferredDirectional);
	ok &= Check(
		developerExclusivePreview.published
			&& developerExclusivePreview.bytecodePatchExclusive
			&& !developerExclusivePreview.hlslRequested
			&& developerExclusivePreview.variants == 0
			&& developerExclusivePreview.suppressedContributors == 1
			&& developerExclusivePreview.suppressedContributorNames
				== std::vector<std::string>{
					"ShaderReplacement.DeveloperForceOn" },
		"exclusive DXBC patch did not retain the developer force-on suppression");
	std::byte shaderToken{};
	std::byte contextToken{};
	g_patchedShader =
		reinterpret_cast<ID3D11PixelShader*>(&shaderToken);
	ok &= Check(
		PublishShaderInjectionFreezePreview()
			&& ArePatchedShaderDispatchesPublished(patchTargets),
		"developer force-on exclusive preview was not published");
	ok &= Check(
		!IsInjectedPixelShader(
			ShaderInjectionTarget::kBsdfLightDeferredDirectional,
			g_patchedShader)
			&& !DispatchShaderInjectionForTesting(
				ShaderInjectionTarget::kBsdfLightDeferredDirectional,
				g_patchedShader,
				reinterpret_cast<ID3D11DeviceContext*>(&contextToken))
			&& g_sssBinds == 0
			&& g_wetnessBinds == 0,
		"suppressed developer force-on retained HLSL match or bind ownership");
	const auto developerSuppressedSnapshot =
		GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::kBsdfLightDeferredDirectional);
	const auto developerSuppressedSummary = GetShaderInjectionSummary();
	ok &= Check(
		developerSuppressedSnapshot.suppressedContributors == 1
			&& developerSuppressedSnapshot.suppressedContributorNames
				== std::vector<std::string>{
					"ShaderReplacement.DeveloperForceOn" }
			&& developerSuppressedSummary.suppressedContributors == 1,
		"developer force-on suppression was absent from status or summary");
	ok &= Check(
		SetDeveloperShaderOverride(
			ShaderInjectionTarget::kBsdfLightDeferredDirectional,
			DeveloperShaderOverride::kAuto),
		"directional developer override reset was rejected");

	ShaderReplacementRegistration conflictingHlsl;
	conflictingHlsl.targetId =
		ShaderInjectionTarget::kBsdfLightDeferredDirectional;
	conflictingHlsl.contributor = "WetnessEffects";
	conflictingHlsl.defines = { { "WETNESS_EFFECTS", "1" } };
	conflictingHlsl.bind = [](ID3D11DeviceContext*) {
		++g_wetnessBinds;
	};
	conflictingHlsl.slotClaims.push_back({
		.stage = ShaderStage::kPixel,
		.resourceType = ShaderResourceType::kShaderResource,
		.slot = 6
	});
	auto conflictingHlslIbl = conflictingHlsl;
	conflictingHlslIbl.targetId =
		ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl;
	ok &= Check(
		RegisterReplacement(std::move(conflictingHlsl)),
		"conflicting HLSL registration setup was rejected");
	ok &= Check(
		RegisterReplacement(std::move(conflictingHlslIbl)),
		"second conflicting HLSL registration setup was rejected");
	const auto exclusivePreview = PreviewShaderInjectionFreeze(
		ShaderInjectionTarget::kBsdfLightDeferredDirectional);
	const auto exclusiveIblPreview = PreviewShaderInjectionFreeze(
		ShaderInjectionTarget::kBsdfLightDeferredDirectionalIbl);
	ok &= Check(
		exclusivePreview.published
			&& exclusivePreview.bytecodePatchExclusive
			&& !exclusivePreview.hlslRequested
			&& !exclusivePreview.slotCollision
			&& exclusivePreview.variants == 0
			&& exclusivePreview.binds == 1
			&& exclusivePreview.patchedMatchers == 1
			&& exclusivePreview.suppressedContributors == 1
			&& exclusivePreview.suppressedContributorNames
				== std::vector<std::string>{ "WetnessEffects" },
		"exclusive DXBC patch did not suppress conflicting HLSL routing");
	ok &= Check(
		exclusiveIblPreview.published
			&& exclusiveIblPreview.bytecodePatchExclusive
			&& !exclusiveIblPreview.hlslRequested
			&& exclusiveIblPreview.variants == 0
			&& exclusiveIblPreview.binds == 1
			&& exclusiveIblPreview.patchedMatchers == 1
			&& exclusiveIblPreview.suppressedContributors == 1
			&& exclusiveIblPreview.suppressedContributorNames
				== std::vector<std::string>{ "WetnessEffects" },
		"second exclusive DXBC claim did not suppress Wetness");
	ok &= Check(
		PublishShaderInjectionFreezePreview(),
		"artifact-failure exclusive claims were not published");
	ok &= Check(
		!IsInjectedPixelShader(
			ShaderInjectionTarget::
				kBsdfLightDeferredDirectional,
			g_patchedShader)
			&& !IsPatchedDispatchPixelShader(
				ShaderInjectionTarget::
					kBsdfLightDeferredDirectional,
				g_patchedShader)
			&& !DispatchShaderInjectionForTesting(
				ShaderInjectionTarget::
					kBsdfLightDeferredDirectional,
				g_patchedShader,
				reinterpret_cast<ID3D11DeviceContext*>(
					&contextToken))
			&& g_sssBinds == 0
			&& g_wetnessBinds == 0,
		"missing artifact did not preserve exclusive stock routing");
	g_patchMatcherActive = true;
	ok &= Check(
		PublishShaderInjectionFreezePreview(),
		"active patched freeze preview was not published");
	ok &= Check(
		!IsInjectedPixelShader(
			ShaderInjectionTarget::
				kBsdfLightDeferredDirectional,
			g_patchedShader),
		"Wetness ownership matched the SSS-patched shader");
	ok &= Check(
		IsPatchedDispatchPixelShader(
			ShaderInjectionTarget::
				kBsdfLightDeferredDirectional,
			g_patchedShader),
		"central patched-dispatch predicate missed the SSS shader");
	ok &= Check(
		DispatchShaderInjectionForTesting(
			ShaderInjectionTarget::
				kBsdfLightDeferredDirectional,
			g_patchedShader,
			reinterpret_cast<ID3D11DeviceContext*>(
				&contextToken))
			&& g_sssBinds == 1
			&& g_wetnessBinds == 0,
		"exclusive dispatch did not run only the SSS bind once");
	const auto suppressedSnapshot =
		GetShaderInjectionTargetSnapshot(
			ShaderInjectionTarget::
				kBsdfLightDeferredDirectional);
	ok &= Check(
		suppressedSnapshot.suppressedContributors == 1
			&& suppressedSnapshot.suppressedContributorNames
				== std::vector<std::string>{ "WetnessEffects" },
		"suppressed Wetness contributor was not retained in telemetry");

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
