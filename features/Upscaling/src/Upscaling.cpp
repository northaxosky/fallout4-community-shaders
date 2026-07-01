#include "Upscaling.h"

#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>

#include "Utils/CSUtil.h"
#include "DX11Hooks.h"
#include "Env.h"
#include "Feature.h"
#include "Render/Engine.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Render/RendererContext.h"
#include "Render/StreamlineCore.h"
namespace cs::features
{
	using namespace upscaling;
	namespace { auto* L = cs::log::Get("cs.feature.upscaling"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Upscaling.toml";

	namespace
	{
		bool IsAnisotropicFilter(D3D11_FILTER a_filter)
		{
			return a_filter == D3D11_FILTER_ANISOTROPIC
				|| a_filter == D3D11_FILTER_COMPARISON_ANISOTROPIC;
		}
	}


// Updates jitter, dynamic resolution, and resources.
struct BSGraphics_State_UpdateDynamicResolution
{
	static void thunk(RE::BSGraphics::RenderTargetManager* This,
		RE::NiPoint3* a2,
		RE::NiPoint3* a3,
		RE::NiPoint3* a4,
		RE::NiPoint3* a5)
	{
		func(This, a2, a3, a4, a5);
		Upscaling::GetSingleton()->UpdateUpscaling();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Disables TAA while an alternate upscaler is active.
struct ImageSpaceEffectTemporalAA_IsActive
{
	static bool thunk(struct ImageSpaceEffectTemporalAA* This)
	{
		return Upscaling::GetSingleton()->upscaleMethod == Upscaling::UpscaleMethod::kDisabled && func(This);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

float originalDynamicHeightRatio = 1.0f;
float originalDynamicWidthRatio = 1.0f;

// Fixes outline thickness in the VATS shader.
struct ImageSpaceEffectVatsTarget_UpdateParams_SetPixelConstant
{
	static void thunk(struct ImageSpaceShaderParam* This, int row, float x, float y, float z, float w)
	{
		func(This, row, x * originalDynamicHeightRatio, y * originalDynamicWidthRatio, z, w);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Fixes dynamic-resolution state and jitter in post-processing shaders.
struct DrawWorld_Imagespace_RenderEffectRange
{
	static void thunk(RE::BSGraphics::RenderTargetManager* This, uint a2, uint a3, uint a4, uint a5)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = cs::engine::GetRenderTargetManager();
		static auto gameViewport = cs::engine::GetGraphicsState();

		bool requiresOverride = cs::engine::dynres::GetHeightRatio(renderTargetManager) != 1.0 || cs::engine::dynres::GetWidthRatio(renderTargetManager) != 1.0;

		auto originalOffsetX = gameViewport->offsetX;
		auto originalOffsetY = gameViewport->offsetY;

		originalDynamicHeightRatio = cs::engine::dynres::GetHeightRatio(renderTargetManager);
		originalDynamicWidthRatio = cs::engine::dynres::GetWidthRatio(renderTargetManager);

		if (requiresOverride) {

			func(This, 0, 3, 1, 1);
			upscaling->OverrideRenderTargets({1, 4, 29, 16});
			upscaling->OverrideDepth(true);
			cs::engine::dynres::Set(renderTargetManager, 1.0f, 1.0f, false);

			func(This, 4, 13, 1, 1);
			upscaling->ResetDepth();
			upscaling->ResetRenderTargets({4});

			cs::engine::dynres::Set(renderTargetManager, originalDynamicWidthRatio, originalDynamicHeightRatio,
				originalDynamicWidthRatio != 1.0f || originalDynamicHeightRatio != 1.0f);
		} else {
			func(This, a2, a3, a4, a5);
		}

		gameViewport->offsetX = originalOffsetX;
		gameViewport->offsetY = originalOffsetY;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Inserts the alternate upscaler after dynamic-resolution viewport setup.
struct DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport
{
	static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_true)
	{
		func(This, a_true);

		auto upscaling = Upscaling::GetSingleton();
		upscaling->Upscale();

		static auto renderTargetManager = cs::engine::GetRenderTargetManager();

		originalDynamicHeightRatio = cs::engine::dynres::GetHeightRatio(renderTargetManager);
		originalDynamicWidthRatio = cs::engine::dynres::GetWidthRatio(renderTargetManager);

		cs::engine::dynres::Set(renderTargetManager, 1.0f, 1.0f, false);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Applies sampler LOD bias during the deferred pre-pass.
struct DrawWorld_Render_PreUI_DeferredPrePass
{
	static void thunk(struct DrawWorld* This)
	{
		auto upscaling = Upscaling::GetSingleton();
		upscaling->OverrideSamplerStates();
		func(This);
		upscaling->ResetSamplerStates();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Applies sampler LOD bias during forward rendering and builds the FSR reactive mask.
struct DrawWorld_Render_PreUI_Forward
{
	static void thunk(struct DrawWorld* This)
	{
		auto upscaling = Upscaling::GetSingleton();

		upscaling->OverrideSamplerStates();
		func(This);
		upscaling->ResetSamplerStates();

		auto fidelityFX = FidelityFX::GetSingleton();

		if (upscaling->upscaleMethod == Upscaling::UpscaleMethod::kFSR)
			fidelityFX->GenerateReactiveMask();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Fixes HBAO dynamic-resolution inputs.
struct DrawWorld_Render_PreUI_NVHBAO
{
	static void thunk(struct DrawWorld* This)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = cs::engine::GetRenderTargetManager();
		bool requiresOverride = cs::engine::dynres::GetHeightRatio(renderTargetManager) != 1.0 || cs::engine::dynres::GetWidthRatio(renderTargetManager) != 1.0;

		originalDynamicHeightRatio = cs::engine::dynres::GetHeightRatio(renderTargetManager);
		originalDynamicWidthRatio = cs::engine::dynres::GetWidthRatio(renderTargetManager);

		if (requiresOverride) {
			upscaling->OverrideDepth(true);
			upscaling->OverrideRenderTargets({20});
			cs::engine::dynres::Set(renderTargetManager, 1.0f, 1.0f, false);
		}

		func(This);

		if (requiresOverride) {
			upscaling->ResetDepth();
			upscaling->ResetRenderTargets({20});
			cs::engine::dynres::Set(renderTargetManager, originalDynamicWidthRatio, originalDynamicHeightRatio,
				originalDynamicWidthRatio != 1.0f || originalDynamicHeightRatio != 1.0f);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Runs BSDFComposite against scaled render targets and depth.
struct DrawWorld_DeferredComposite_RenderPassImmediately
{
	static void thunk(RE::BSRenderPass* This, uint a2, bool a3)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = cs::engine::GetRenderTargetManager();
		bool requiresOverride = cs::engine::dynres::GetHeightRatio(renderTargetManager) != 1.0 || cs::engine::dynres::GetWidthRatio(renderTargetManager) != 1.0;

		originalDynamicHeightRatio = cs::engine::dynres::GetHeightRatio(renderTargetManager);
		originalDynamicWidthRatio = cs::engine::dynres::GetWidthRatio(renderTargetManager);

		if (requiresOverride) {
			upscaling->OverrideRenderTargets({20, 25, 57, 24, 23, 58, 59, 3, 9, 60, 61, 28});
			upscaling->OverrideDepth(true);
			cs::engine::dynres::Set(renderTargetManager, 1.0f, 1.0f, false);
		}

		func(This, a2, a3);

		if (requiresOverride) {
			upscaling->ResetRenderTargets({4});
			upscaling->ResetDepth();
			cs::engine::dynres::Set(renderTargetManager, originalDynamicWidthRatio, originalDynamicHeightRatio,
				originalDynamicWidthRatio != 1.0f || originalDynamicHeightRatio != 1.0f);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Gives lens flare the scaled depth override.
struct BSImagespaceShaderLensFlare_RenderLensFlare
{
	static void thunk(RE::NiCamera* a_camera)
	{
		auto upscaling = Upscaling::GetSingleton();

		static auto renderTargetManager = cs::engine::GetRenderTargetManager();
		bool requiresOverride = cs::engine::dynres::GetHeightRatio(renderTargetManager) != 1.0 || cs::engine::dynres::GetWidthRatio(renderTargetManager) != 1.0;

		if (requiresOverride)
			upscaling->OverrideDepth(true);

		func(a_camera);

		if (requiresOverride)
			upscaling->ResetDepth();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Replaces SSLR raytracing shader for scaled render targets.
struct BSImagespaceShaderSSLRRaytracing_SetupTechnique_BeginTechnique
{
	static void thunk(RE::BSShader* This, uint a2, uint a3, uint a4, uint a5)
	{
		func(This, a2, a3, a4, a5);
		Upscaling::GetSingleton()->PatchSSRShader();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Captures opaque color before forward alpha for the FSR reactive mask.
struct ForwardAlphaImpl_FinishAccumulating_Standard_PostResolveDepth
{
	static void thunk(RE::BSShaderAccumulator* This)
	{
		func(This);
		auto upscaling = Upscaling::GetSingleton();
		auto fidelityFX = FidelityFX::GetSingleton();

		if (upscaling->upscaleMethod == Upscaling::UpscaleMethod::kFSR)
			fidelityFX->CopyOpaqueTexture();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Resets LoadingMenu jitter scale to native resolution.
struct LoadingMenu_Render_UpdateTemporalData
{
	static void thunk(RE::BSGraphics::State* This)
	{
		func(This);

		static auto renderTargetManager = cs::engine::GetRenderTargetManager();
		cs::engine::dynres::Set(renderTargetManager, 1.0f, 1.0f, false);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// Restores dynamic-resolution settings after imagespace.
struct DrawWorld_Imagespace
{
	static void thunk(struct DrawWorld* This)
	{
		func(This);

		static auto renderTargetManager = cs::engine::GetRenderTargetManager();

		cs::engine::dynres::Set(renderTargetManager, originalDynamicWidthRatio, originalDynamicHeightRatio,
			originalDynamicWidthRatio != 1.0f || originalDynamicHeightRatio != 1.0f);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::InstallHooks()
{
	L->info("Installing ImageSpaceEffectTemporalAA_IsActive vfunc hook");
	stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);

	auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
	L->info("Runtime index: {}", runtimeIdx);
	// All offsets[] arrays below are 3-wide (OG/NG/AE). A 4th runtime would index past the end and silently overwrite a random byte.
	assert(runtimeIdx < 3);

	L->info("Installing BSGraphics_State_UpdateDynamicResolution hook");
	{
		constexpr std::ptrdiff_t offsets[] = { 0x14B, 0x29F, 0x29F };
		stl::write_thunk_call<BSGraphics_State_UpdateDynamicResolution>(REL::ID({ 984743, 2318321, 2318321 }).address() + offsets[runtimeIdx]);
	}

	L->info("Installing DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport hook");
	{
		constexpr std::ptrdiff_t offsets[] = { 0xE1, 0xC5, 0xC5 };
		stl::write_thunk_call<DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID({ 587723, 2318322, 2318322 }).address() + offsets[runtimeIdx]);
	}

	L->info("Installing DrawWorld_Render_PreUI_DeferredPrePass hook");
	{
		constexpr std::ptrdiff_t offsets[] = { 0x17F, 0x2E3, 0x2E3 };
		stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(REL::ID({ 984743, 2318321, 2318321 }).address() + offsets[runtimeIdx]);
	}
	L->info("Installing DrawWorld_Render_PreUI_Forward hook");
	{
		constexpr std::ptrdiff_t offsets[] = { 0x1C9, 0x3A6, 0x3A6 };
		stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(REL::ID({ 984743, 2318321, 2318321 }).address() + offsets[runtimeIdx]);
	}

	L->info("Installing ForwardAlphaImpl_FinishAccumulating_Standard_PostResolveDepth hook");
	{
		constexpr std::ptrdiff_t offsets[] = { 0x1DC, 0x4C6, 0x4C6 };
		stl::write_thunk_call<ForwardAlphaImpl_FinishAccumulating_Standard_PostResolveDepth>(REL::ID({ 338205, 2318315, 2318315 }).address() + offsets[runtimeIdx]);
	}

	// Install even under ENB: render-target swaps propagate through ENB's wrapper.
	L->info("Installing 7 dynamic resolution hooks");

	{
		constexpr std::ptrdiff_t offsets[] = { 0x8DC, 0x915, 0x915 };
		stl::write_thunk_call<DrawWorld_DeferredComposite_RenderPassImmediately>(REL::ID({ 728427, 2318313, 2318313 }).address() + offsets[runtimeIdx]);
	}

	stl::detour_thunk<BSImagespaceShaderLensFlare_RenderLensFlare>(REL::ID({ 676108, 2317547, 2317547 }));

	stl::write_thunk_call<BSImagespaceShaderSSLRRaytracing_SetupTechnique_BeginTechnique>(REL::ID({ 779077, 2317302, 2317302 }).address() + 0x1C);

	{
		constexpr std::ptrdiff_t offsets[] = { 0x9F, 0x83, 0x83 };
		stl::write_thunk_call<DrawWorld_Imagespace_RenderEffectRange>(REL::ID({ 587723, 2318322, 2318322 }).address() + offsets[runtimeIdx]);
	}

	{
		constexpr std::ptrdiff_t offsets[] = { 0xBB, 0x110, 0x110 };
		stl::write_thunk_call<ImageSpaceEffectVatsTarget_UpdateParams_SetPixelConstant>(REL::ID({ 1042583, 2317983, 2317983 }).address() + offsets[runtimeIdx]);
	}

	{
		constexpr std::ptrdiff_t offsets[] = { 0x2BD, 0x275, 0x275 };
		stl::write_thunk_call<LoadingMenu_Render_UpdateTemporalData>(REL::ID({ 135719, 2249225, 2249225 }).address() + offsets[runtimeIdx]);
	}

	stl::detour_thunk<DrawWorld_Imagespace>(REL::ID({ 587723, 2318322, 2318322 }));

	L->info("All upscaling hooks installed");
}

struct SamplerStates
{
	ID3D11SamplerState* a[320];

	static SamplerStates* GetSingleton()
	{
		static auto samplerStates = reinterpret_cast<SamplerStates*>(REL::ID({ 44312, 2704455, 2704455 }).address());
		return samplerStates;
	}
};

void Upscaling::LoadSettings()
{
	toml::table table;
	try {
		table = toml::parse_file(kConfigPath);
	} catch (const toml::parse_error& e) {
		L->warn("Failed to parse {}: {}; keeping defaults", kConfigPath, e.description());
		return;
	}

	const auto rawMethod = table["settings"]["upscale_method_preference"].value_or<int64_t>(static_cast<int64_t>(settings.upscaleMethodPreference));
	const auto rawQuality = table["settings"]["quality_mode"].value_or<int64_t>(static_cast<int64_t>(settings.qualityMode));
	const auto rawPreset = table["settings"]["preset_dlss"].value_or<int64_t>(static_cast<int64_t>(settings.presetDLSS));
	const auto method = std::clamp(rawMethod, int64_t{ 0 }, int64_t{ 2 });
	const auto quality = std::clamp(rawQuality, int64_t{ 0 }, int64_t{ 4 });
	const auto preset = std::clamp(rawPreset, int64_t{ 0 }, int64_t{ 4 });
	if (method != rawMethod) {
		L->warn("Clamped invalid upscale_method_preference {} -> {}", rawMethod, method);
	}
	if (quality != rawQuality) {
		L->warn("Clamped invalid quality_mode {} -> {}", rawQuality, quality);
	}
	if (preset != rawPreset) {
		L->warn("Clamped invalid preset_dlss {} -> {}", rawPreset, preset);
	}
	settings.upscaleMethodPreference = static_cast<uint>(method);
	settings.qualityMode = static_cast<uint>(quality);
	settings.presetDLSS = static_cast<uint>(preset);
	settings.sharpnessFSR = std::clamp(static_cast<float>(table["settings"]["sharpness_fsr"].value_or<double>(static_cast<double>(settings.sharpnessFSR))), 0.0f, 1.0f);

	L->info("Loaded: upscaleMethod={}, qualityMode={}, presetDLSS={}, sharpnessFSR={:.2f}",
		settings.upscaleMethodPreference, settings.qualityMode, settings.presetDLSS, settings.sharpnessFSR);
}

void Upscaling::SaveSettings()
{
	toml::table table;
	try {
		table = toml::parse_file(kConfigPath);
	} catch (const toml::parse_error& e) {
		L->warn("Overwriting unparseable {}: {}", kConfigPath, e.description());
		table = toml::table{};
	}

	auto& settingsTable = table.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
	settingsTable.insert_or_assign("upscale_method_preference", static_cast<int64_t>(settings.upscaleMethodPreference));
	settingsTable.insert_or_assign("quality_mode", static_cast<int64_t>(settings.qualityMode));
	settingsTable.insert_or_assign("sharpness_fsr", static_cast<double>(settings.sharpnessFSR));
	settingsTable.insert_or_assign("preset_dlss", static_cast<int64_t>(settings.presetDLSS));

	std::ofstream out(kConfigPath);
	if (out) {
		out << table;
	}
}

void Upscaling::RestoreDefaultSettings()
{
	settings = Settings{};
	SaveSettings();
	cs::Menu::ShowToast("Upscaling reset to defaults", 2.5);
}

void Upscaling::DrawSettings()
{
	const auto activeMethod = GetUpscaleMethod(false);
	const char* activeStr = activeMethod == UpscaleMethod::kDLSS ? "DLSS"
		: activeMethod == UpscaleMethod::kFSR ? "FSR3"
		: "Disabled (native TAA)";
	ImGui::Text("Active: %s", activeStr);
	if (cs::env::IsENBLoaded())
		ImGui::TextDisabled("ENB detected: forcing Native AA quality (sub-native modes disabled).");

	ImGui::Separator();

	static const char* methodLabels[] = { "Disabled (native TAA)", "FSR3", "DLSS" };
	int method = static_cast<int>(settings.upscaleMethodPreference);
	if (ImGui::Combo("Method", &method, methodLabels, IM_ARRAYSIZE(methodLabels))) {
		settings.upscaleMethodPreference = static_cast<uint>(std::clamp(method, 0, 2));
		SaveSettings();
	}
	if (settings.upscaleMethodPreference == static_cast<uint>(UpscaleMethod::kDLSS) && activeMethod != UpscaleMethod::kDLSS)
		ImGui::TextDisabled("DLSS unavailable on this system; falling back to FSR3.");

	static const char* qualityLabels[] = { "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" };
	int qm = static_cast<int>(settings.qualityMode);
	if (ImGui::Combo("Quality", &qm, qualityLabels, IM_ARRAYSIZE(qualityLabels))) {
		settings.qualityMode = static_cast<uint>(std::clamp(qm, 0, 4));
		SaveSettings();
	}

	if (activeMethod == UpscaleMethod::kDLSS) {
		static const char* presetLabels[] = { "Default", "Preset J", "Preset K (transformer)", "Preset L", "Preset M" };
		int preset = static_cast<int>(settings.presetDLSS);
		if (ImGui::Combo("DLSS preset", &preset, presetLabels, IM_ARRAYSIZE(presetLabels))) {
			settings.presetDLSS = static_cast<uint>(std::clamp(preset, 0, 4));
			SaveSettings();
		}
		ImGui::TextDisabled("DLSS sharpening moved to Imagespace -> Lens -> Sharpen (CAS).");
	}

	if (activeMethod == UpscaleMethod::kFSR) {
		if (ImGui::SliderFloat("FSR sharpness", &settings.sharpnessFSR, 0.0f, 1.0f, "%.2f"))
			settings.sharpnessFSR = std::clamp(settings.sharpnessFSR, 0.0f, 1.0f);
		if (ImGui::IsItemDeactivatedAfterEdit())
			SaveSettings();
	}
}

void Upscaling::OnDataLoaded()
{
	L->info("OnDataLoaded: registering UI event sink, loading settings, updating game settings");
	RE::UI::GetSingleton()->RegisterSink<RE::MenuOpenCloseEvent>(this);
	LoadSettings();
	UpdateGameSettings();
	L->info("OnDataLoaded complete");
}

RE::BSEventNotifyControl Upscaling::ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event.menuName == "PauseMenu") {
		if (!a_event.opening) {
			GetSingleton()->LoadSettings();
			GetSingleton()->UpdateGameSettings();
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

void Upscaling::UpdateRenderTarget(int index, float a_currentWidthRatio, float a_currentHeightRatio)
{
	static auto rendererData = RE::BSGraphics::GetRendererData();
	originalRenderTargets[index] = rendererData->renderTargets[index];

	auto& originalRenderTarget = originalRenderTargets[index];
	auto& proxyRenderTarget = proxyRenderTargets[index];

	// Manually release proxy COM refs stored in engine structs.
	if (proxyRenderTarget.uaView)
		proxyRenderTarget.uaView->Release();
	proxyRenderTarget.uaView = nullptr;

	if (proxyRenderTarget.srView)
		proxyRenderTarget.srView->Release();
	proxyRenderTarget.srView = nullptr;

	if (proxyRenderTarget.rtView)
		proxyRenderTarget.rtView->Release();
	proxyRenderTarget.rtView = nullptr;

	if (proxyRenderTarget.texture)
		proxyRenderTarget.texture->Release();
	proxyRenderTarget.texture = nullptr;

	if (a_currentWidthRatio == 1.0f && a_currentHeightRatio == 1.0f)
		return;

	D3D11_TEXTURE2D_DESC textureDesc{};
	if (originalRenderTarget.texture)
		reinterpret_cast<ID3D11Texture2D*>(originalRenderTarget.texture)->GetDesc(&textureDesc);

	D3D11_RENDER_TARGET_VIEW_DESC rtViewDesc{};
	if (originalRenderTarget.rtView)
		reinterpret_cast<ID3D11RenderTargetView*>(originalRenderTarget.rtView)->GetDesc(&rtViewDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srViewDesc{};
	if (originalRenderTarget.srView)
		reinterpret_cast<ID3D11ShaderResourceView*>(originalRenderTarget.srView)->GetDesc(&srViewDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uaViewDesc;
	if (originalRenderTarget.uaView)
		reinterpret_cast<ID3D11UnorderedAccessView*>(originalRenderTarget.uaView)->GetDesc(&uaViewDesc);

	textureDesc.Width = static_cast<uint>(static_cast<float>(textureDesc.Width) * a_currentWidthRatio);
	textureDesc.Height = static_cast<uint>(static_cast<float>(textureDesc.Height) * a_currentHeightRatio);

	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	if (originalRenderTarget.texture)
		DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&proxyRenderTarget.texture)));

	if (auto texture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTarget.texture)) {
		if (originalRenderTarget.rtView)
			DX::ThrowIfFailed(device->CreateRenderTargetView(texture, &rtViewDesc, reinterpret_cast<ID3D11RenderTargetView**>(&proxyRenderTarget.rtView)));

		if (originalRenderTarget.srView)
			DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &srViewDesc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxyRenderTarget.srView)));

		if (originalRenderTarget.uaView)
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texture, &uaViewDesc, reinterpret_cast<ID3D11UnorderedAccessView**>(&proxyRenderTarget.uaView)));
	}

#ifndef NDEBUG
	if (auto texture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTarget.texture)) {
		auto name = std::format("RT PROXY {}", index);
		texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto rtView = reinterpret_cast<ID3D11RenderTargetView*>(proxyRenderTarget.rtView)) {
		auto name = std::format("RTV PROXY {}", index);
		rtView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto srView = reinterpret_cast<ID3D11ShaderResourceView*>(proxyRenderTarget.srView)) {
		auto name = std::format("SRV PROXY {}", index);
		srView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}

	if (auto uaView = reinterpret_cast<ID3D11UnorderedAccessView*>(proxyRenderTarget.uaView)) {
		auto name = std::format("UAV PROXY {}", index);
		uaView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
	}
#endif
}

void Upscaling::OverrideRenderTarget(int index, bool a_doCopy)
{
	if (!originalRenderTargets[index].texture || !proxyRenderTargets[index].texture)
		return;

	static auto rendererData = RE::BSGraphics::GetRendererData();

	rendererData->renderTargets[index] = proxyRenderTargets[index];

	if (a_doCopy) {
		D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
		reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture)->GetDesc(&srcDesc);
		reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture)->GetDesc(&dstDesc);

		D3D11_BOX srcBox;
		srcBox.left = 0;
		srcBox.top = 0;
		srcBox.front = 0;
		srcBox.right = dstDesc.Width;
		srcBox.bottom = dstDesc.Height;
		srcBox.back = 1;

		auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		context->CopySubresourceRegion(reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture), 0, 0, 0, 0, reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture), 0, &srcBox);
	}
}

void Upscaling::ResetRenderTarget(int index, bool a_doCopy)
{
	if (!originalRenderTargets[index].texture || !proxyRenderTargets[index].texture)
		return;

	static auto rendererData = RE::BSGraphics::GetRendererData();

	if (a_doCopy) {
		D3D11_TEXTURE2D_DESC srcDesc, dstDesc;
		reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture)->GetDesc(&srcDesc);
		reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture)->GetDesc(&dstDesc);

		D3D11_BOX srcBox;
		srcBox.left = 0;
		srcBox.top = 0;
		srcBox.front = 0;
		srcBox.right = srcDesc.Width;
		srcBox.bottom = srcDesc.Height;
		srcBox.back = 1;

		auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		context->CopySubresourceRegion(reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture), 0, 0, 0, 0, reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture), 0, &srcBox);
	}

	rendererData->renderTargets[index] = originalRenderTargets[index];
}

void Upscaling::UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio)
{
	static auto previousWidthRatio = 0.0f;
	static auto previousHeightRatio = 0.0f;

	if (previousWidthRatio == a_currentWidthRatio && previousHeightRatio == a_currentHeightRatio)
		return;

	L->info("Render targets resolution changed: ratio {:.4f}x{:.4f} -> {:.4f}x{:.4f}",
		previousWidthRatio, previousHeightRatio, a_currentWidthRatio, a_currentHeightRatio);

	previousWidthRatio = a_currentWidthRatio;
	previousHeightRatio = a_currentHeightRatio;

	L->info("Recreating {} render targets with new ratio", ARRAYSIZE(renderTargetsPatch));
	for (int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++)
		UpdateRenderTarget(renderTargetsPatch[i], a_currentWidthRatio, a_currentHeightRatio);

	// Force intermediate texture recreation with the new dimensions.
	upscalingTexture = nullptr;
	depthOverrideTexture = nullptr;

	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)cs::engine::RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource;
	frameBufferSRV->GetResource(&frameBufferResource);

	D3D11_TEXTURE2D_DESC texDesc{};
	static_cast<ID3D11Texture2D*>(frameBufferResource)->GetDesc(&texDesc);

	frameBufferResource->Release();

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = 1 }
	};

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = {.MipSlice = 0 }
	};

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	upscalingTexture = std::make_unique<Texture2D>(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);

	if (a_currentWidthRatio == 1.0f && a_currentHeightRatio == 1.0f)
		return;

	texDesc.Width = static_cast<uint>(static_cast<float>(texDesc.Width) * a_currentWidthRatio);
	texDesc.Height = static_cast<uint>(static_cast<float>(texDesc.Height) * a_currentHeightRatio);

	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	depthOverrideTexture = std::make_unique<Texture2D>(texDesc);
	depthOverrideTexture->CreateSRV(srvDesc);
	depthOverrideTexture->CreateUAV(uavDesc);
}

void Upscaling::OverrideRenderTargets(const std::vector<int>& a_indicesToCopy)
{
	static bool loggedOnce = false;
	if (!loggedOnce) {
		L->info("First OverrideRenderTargets call: {} targets to patch, {} indices to copy",
			ARRAYSIZE(renderTargetsPatch), a_indicesToCopy.size());
		loggedOnce = true;
	}

	for (int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++) {
		int targetIndex = renderTargetsPatch[i];
		bool shouldCopy = a_indicesToCopy.empty() || std::find(a_indicesToCopy.begin(), a_indicesToCopy.end(), targetIndex) != a_indicesToCopy.end();
		OverrideRenderTarget(targetIndex, shouldCopy);
	}

	static auto renderTargetManager = cs::engine::GetRenderTargetManager();

	// Keep engine RT metadata in scaled resolution for dimension queries.
	for (int i = 0; i < 100; i++) {
		originalRenderTargetData[i] = renderTargetManager->renderTargetData[i];
		renderTargetManager->renderTargetData[i].width = static_cast<uint>(static_cast<float>(renderTargetManager->renderTargetData[i].width) * cs::engine::dynres::GetWidthRatio(renderTargetManager));
		renderTargetManager->renderTargetData[i].height = static_cast<uint>(static_cast<float>(renderTargetManager->renderTargetData[i].height) * cs::engine::dynres::GetHeightRatio(renderTargetManager));
	}

	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// PSGetShaderResources AddRefs each non-null SRV; release below.
	ID3D11ShaderResourceView* boundSRVs[16] = {};
	context->PSGetShaderResources(0, 16, boundSRVs);

	for (int srvSlot = 0; srvSlot < 16; srvSlot++) {
		if (!boundSRVs[srvSlot])
			continue;

		for (int rtIndex = 0; rtIndex < ARRAYSIZE(renderTargetsPatch); rtIndex++) {
			int targetIndex = renderTargetsPatch[rtIndex];
			auto& originalRT = originalRenderTargets[targetIndex];
			auto& proxyRT = proxyRenderTargets[targetIndex];

			if (boundSRVs[srvSlot] == reinterpret_cast<ID3D11ShaderResourceView*>(originalRT.srView) && proxyRT.srView) {
				auto proxySRV = reinterpret_cast<ID3D11ShaderResourceView*>(proxyRT.srView);
				context->PSSetShaderResources(srvSlot, 1, &proxySRV);
				break;
			}
		}

		boundSRVs[srvSlot]->Release();
	}

	DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, false);
}

void Upscaling::ResetRenderTargets(const std::vector<int>& a_indicesToCopy)
{
	static bool loggedOnce = false;
	if (!loggedOnce) {
		L->info("First ResetRenderTargets call: {} targets to restore, {} indices to copy",
			ARRAYSIZE(renderTargetsPatch), a_indicesToCopy.size());
		loggedOnce = true;
	}

	for (int i = 0; i < ARRAYSIZE(renderTargetsPatch); i++) {
		int targetIndex = renderTargetsPatch[i];
		bool shouldCopy = a_indicesToCopy.empty() ||
			std::find(a_indicesToCopy.begin(), a_indicesToCopy.end(), targetIndex) != a_indicesToCopy.end();
		ResetRenderTarget(targetIndex, shouldCopy);
	}

	static auto renderTargetManager = cs::engine::GetRenderTargetManager();

	for (int i = 0; i < 100; i++) {
		renderTargetManager->renderTargetData[i] = originalRenderTargetData[i];
	}

	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// PSGetShaderResources AddRefs each non-null SRV; release below.
	ID3D11ShaderResourceView* boundSRVs[16] = {};
	context->PSGetShaderResources(0, 16, boundSRVs);

	for (int srvSlot = 0; srvSlot < 16; srvSlot++) {
		if (!boundSRVs[srvSlot])
			continue;

		for (int rtIndex = 0; rtIndex < ARRAYSIZE(renderTargetsPatch); rtIndex++) {
			int targetIndex = renderTargetsPatch[rtIndex];
			auto& originalRT = originalRenderTargets[targetIndex];
			auto& proxyRT = proxyRenderTargets[targetIndex];

			if (boundSRVs[srvSlot] == reinterpret_cast<ID3D11ShaderResourceView*>(proxyRT.srView) && originalRT.srView) {
				auto originalSRV = reinterpret_cast<ID3D11ShaderResourceView*>(originalRT.srView);
				context->PSSetShaderResources(srvSlot, 1, &originalSRV);
				break;
			}
		}

		boundSRVs[srvSlot]->Release();
	}

	DrawWorld_Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport::func(renderTargetManager, true);
}

void Upscaling::OverrideDepth(bool a_doCopy)
{
	static auto rendererData = RE::BSGraphics::GetRendererData();

	originalDepthView = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->depthStencilTargets[(uint)cs::engine::DepthStencilTarget::kMain].srViewDepth);

	if (a_doCopy) {
		static auto gameViewport = cs::engine::GetGraphicsState();

		// Copy depth once per frame at most.
		static decltype(gameViewport->frameCount) previousFrame = UINT_MAX;
		if (previousFrame != gameViewport->frameCount)
			CopyDepth();
		previousFrame = gameViewport->frameCount;
	}

	rendererData->depthStencilTargets[(uint)cs::engine::DepthStencilTarget::kMain].srViewDepth = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(depthOverrideTexture->srv.get());
}

void Upscaling::ResetDepth()
{
	static auto rendererData = RE::BSGraphics::GetRendererData();

	rendererData->depthStencilTargets[(uint)cs::engine::DepthStencilTarget::kMain].srViewDepth = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(originalDepthView);
}

void Upscaling::UpdateSamplerStates(float a_currentMipBias)
{
	static auto samplerStates = SamplerStates::GetSingleton();
	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	static float previousMipBias = 1.0f;
	static bool firstCall = true;

	if (!firstCall && previousMipBias == a_currentMipBias)
		return;
	firstCall = false;

	for (int a = 0; a < 320; a++)
		originalSamplerStates[a] = samplerStates->a[a];

	L->info("Mip bias changed: {:.4f} -> {:.4f}", previousMipBias, a_currentMipBias);
	previousMipBias = a_currentMipBias;

	const float clampedMipBias = std::clamp(a_currentMipBias, -15.99f, 15.99f);

	// Rebuild sampler states with negative LOD bias for sub-native rendering.
	for (int a = 0; a < 320; a++) {
		if (biasedSamplerStates[a]){
			biasedSamplerStates[a]->Release();
			biasedSamplerStates[a] = nullptr;
		}

		if (auto samplerState = originalSamplerStates[a]) {
			D3D11_SAMPLER_DESC samplerDesc;
			samplerState->GetDesc(&samplerDesc);

			if (clampedMipBias != 0.0f && IsAnisotropicFilter(samplerDesc.Filter)) {
				samplerDesc.MipLODBias = clampedMipBias;
			}

			DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &biasedSamplerStates[a]));
		}
	}
}

void Upscaling::OverrideSamplerStates()
{
	if (upscaleMethod == UpscaleMethod::kDisabled)
		return;

	static auto samplerStates = SamplerStates::GetSingleton();
	for (int a = 0; a < 320; a++)
		samplerStates->a[a] = biasedSamplerStates[a];
}

void Upscaling::ResetSamplerStates()
{
	if (upscaleMethod == UpscaleMethod::kDisabled)
		return;

	static auto samplerStates = SamplerStates::GetSingleton();
	for (int a = 0; a < 320; a++)
		samplerStates->a[a] = originalSamplerStates[a];
}

void Upscaling::CopyDepth()
{
	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Unbind + restore engine OM around the depth compute/copy; clears CS slots on exit.
	cs::engine::ComputeOMScope omcs(context);

	static auto gameViewport = cs::engine::GetGraphicsState();
	static auto renderTargetManager = cs::engine::GetRenderTargetManager();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * cs::engine::dynres::GetWidthRatio(renderTargetManager), screenSize.y * cs::engine::dynres::GetHeightRatio(renderTargetManager));

	static bool loggedOnce = false;
	if (!loggedOnce) {
		L->info("First CopyDepth: screen={}x{}, render={}x{}, widthRatio={:.4f}, heightRatio={:.4f}",
			(uint)screenSize.x, (uint)screenSize.y, (uint)renderSize.x, (uint)renderSize.y,
			cs::engine::dynres::GetWidthRatio(renderTargetManager), cs::engine::dynres::GetHeightRatio(renderTargetManager));
		loggedOnce = true;
	}

	auto depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->depthStencilTargets[(uint)cs::engine::DepthStencilTarget::kMain].srViewDepth);

	auto depthUAV = depthOverrideTexture->uav.get();

	// Keep linearized depth in sync for other effects.
	auto linearDepthUAV = reinterpret_cast<ID3D11UnorderedAccessView*>(rendererData->renderTargets[(uint)cs::engine::RenderTarget::kMainDepthMips].uaView);

	{
		UpdateAndBindUpscalingCB(context, screenSize, renderSize);

		{
			ID3D11ShaderResourceView* views[] = { depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[] = { linearDepthUAV };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(GetOverrideLinearDepthCS(), nullptr, 0);

			uint dispatchX = (uint)std::ceil(screenSize.x / 8.0f);
			uint dispatchY = (uint)std::ceil(screenSize.y / 8.0f);
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		{
			ID3D11ShaderResourceView* views[] = { depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[] = { depthUAV };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(GetOverrideDepthCS(), nullptr, 0);

			uint dispatchX = (uint)std::ceil(renderSize.x / 8.0f);
			uint dispatchY = (uint)std::ceil(renderSize.y / 8.0f);
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		// Clear compute bindings to avoid SRV/UAV hazards.
		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod(bool a_checkMenu)
{
	static auto ui = RE::UI::GetSingleton();

	// Disable upscaling in menus that render at incompatible scales.
	if (a_checkMenu){
		if (ui->GetMenuOpen("ExamineMenu")
			|| ui->GetMenuOpen("PipboyMenu")
			|| ui->GetMenuOpen("LoadingMenu")
			|| ui->GetMenuOpen("TerminalMenu")
			|| ui->GetMenuOpen("ContainerMenu")
			|| ui->GetMenuOpen("BarterMenu"))
			return UpscaleMethod::kDisabled;
	}

	UpscaleMethod currentUpscaleMethod = (UpscaleMethod)settings.upscaleMethodPreference;

	auto* core = cs::Streamline::GetSingleton();
	if (!core->featureDLSS && currentUpscaleMethod == UpscaleMethod::kDLSS) {
		static bool loggedDLSSFallback = false;
		if (!loggedDLSSFallback) {
			L->info("DLSS preferred but not available, falling back to FSR");
			loggedDLSSFallback = true;
		}
		currentUpscaleMethod = UpscaleMethod::kFSR;
	}

	static bool loggedOnce = false;
	if (!loggedOnce && !a_checkMenu) {
		L->info("GetUpscaleMethod resolved: method={} (0=Disabled, 1=FSR, 2=DLSS), preference={}, enb={}, dlssAvailable={}",
			static_cast<uint>(currentUpscaleMethod), settings.upscaleMethodPreference, cs::env::IsENBLoaded(), core->featureDLSS);
		loggedOnce = true;
	}

	return currentUpscaleMethod;
}

uint Upscaling::GetEffectiveQualityMode()
{
	// ENB clamp avoids viewport compounding until proxy/UI isolation work lands.
	if (cs::env::IsENBLoaded() && settings.qualityMode != 0) {
		return 0;
	}
	return settings.qualityMode;
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMethodNoMenu = UpscaleMethod::kDisabled;

	auto streamline = Streamline::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	if (previousUpscaleMethodNoMenu != upscaleMethodNoMenu) {
		L->info("Method transition: {} -> {} (0=Disabled, 1=FSR, 2=DLSS)",
			static_cast<uint>(previousUpscaleMethodNoMenu), static_cast<uint>(upscaleMethodNoMenu));
		if (previousUpscaleMethodNoMenu == UpscaleMethod::kDisabled)
			CreateUpscalingResources();
		else if (previousUpscaleMethodNoMenu == UpscaleMethod::kFSR)
			fidelityFX->DestroyFSRResources();
		else if (previousUpscaleMethodNoMenu == UpscaleMethod::kDLSS)
			streamline->DestroyDLSSResources();

		if (upscaleMethodNoMenu == UpscaleMethod::kDisabled)
			DestroyUpscalingResources();
		else if (upscaleMethodNoMenu == UpscaleMethod::kFSR)
			fidelityFX->CreateFSRResources();

		previousUpscaleMethodNoMenu = upscaleMethodNoMenu;
	}
}

ID3D11ComputeShader* Upscaling::GetDilateMotionVectorCS()
{
	if (!dilateMotionVectorCS) {
		L->debug("Compiling DilateMotionVectorCS.hlsl");
		dilateMotionVectorCS.attach((ID3D11ComputeShader*)cs::util::CompileShader(L"Data/F4SE/Plugins/Upscaling/DilateMotionVectorCS.hlsl", {}, "cs_5_0"));
	}
	return dilateMotionVectorCS.get();
}

ID3D11ComputeShader* Upscaling::GetOverrideLinearDepthCS()
{
	if (!overrideLinearDepthCS) {
		L->debug("Compiling OverrideLinearDepthCS.hlsl");
		overrideLinearDepthCS.attach((ID3D11ComputeShader*)cs::util::CompileShader(L"Data/F4SE/Plugins/Upscaling/OverrideLinearDepthCS.hlsl", {}, "cs_5_0"));
	}
	return overrideLinearDepthCS.get();
}

ID3D11ComputeShader* Upscaling::GetOverrideDepthCS()
{
	if (!overrideDepthCS) {
		L->debug("Compiling OverrideDepthCS.hlsl");
		overrideDepthCS.attach((ID3D11ComputeShader*)cs::util::CompileShader(L"Data/F4SE/Plugins/Upscaling/OverrideDepthCS.hlsl", {}, "cs_5_0"));
	}
	return overrideDepthCS.get();
}

ID3D11PixelShader* Upscaling::GetBSImagespaceShaderSSLRRaytracing()
{
	if (!BSImagespaceShaderSSLRRaytracing) {
		L->debug("Compiling BSImagespaceShaderSSLRRaytracing.hlsl");
		BSImagespaceShaderSSLRRaytracing.attach((ID3D11PixelShader*)cs::util::CompileShader(L"Data/F4SE/Plugins/Upscaling/BSImagespaceShaderSSLRRaytracing.hlsl", {}, "ps_5_0"));
	}
	return BSImagespaceShaderSSLRRaytracing.get();
}

ConstantBuffer* Upscaling::GetUpscalingCB()
{
	static std::unique_ptr<ConstantBuffer> upscalingCB = nullptr;

	if (!upscalingCB) {
		L->debug("Creating UpscalingCB");
		upscalingCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<UpscalingCB>(false));
	}
	return upscalingCB.get();
}

void Upscaling::UpdateAndBindUpscalingCB(ID3D11DeviceContext* a_context, float2 a_screenSize, float2 a_renderSize)
{
	float4 cameraData{};
	cameraData.x = cs::engine::GetCameraFar();
	cameraData.y = cs::engine::GetCameraNear();
	cameraData.z = cameraData.x - cameraData.y;
	cameraData.w = cameraData.x * cameraData.y;

	UpscalingCB upscalingData;
	upscalingData.ScreenSize[0] = static_cast<uint>(a_screenSize.x);
	upscalingData.ScreenSize[1] = static_cast<uint>(a_screenSize.y);
	upscalingData.RenderSize[0] = static_cast<uint>(a_renderSize.x);
	upscalingData.RenderSize[1] = static_cast<uint>(a_renderSize.y);
	upscalingData.CameraData = cameraData;

	auto upscalingCB = GetUpscalingCB();
	upscalingCB->Update(upscalingData);

	auto upscalingBuffer = upscalingCB->CB();
	a_context->CSSetConstantBuffers(0, 1, &upscalingBuffer);
}

void Upscaling::UpdateGameSettings()
{
	static auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();

	imageSpaceManager->effectList[(uint)RE::ImageSpaceManager::ImageSpaceEffectEnum::EFFECT_SHADER_FXAA]->isActive = false;

	static auto enableTAA = (bool*)REL::ID({ 460417, 2704658, 2704658 }).address();
	*enableTAA = true;
}

void Upscaling::UpdateUpscaling()
{
	static bool firstCall = true;
	if (firstCall) {
		L->info("UpdateUpscaling first call");
		firstCall = false;
	}

	static auto gameViewport = cs::engine::GetGraphicsState();
	static auto renderTargetManager = cs::engine::GetRenderTargetManager();

	upscaleMethodNoMenu = GetUpscaleMethod(false);
	upscaleMethod = GetUpscaleMethod(true);

	// Convert quality mode to render scale, e.g. Quality ~1.5x -> 0.67.
	auto effectiveQuality = GetEffectiveQualityMode();
	float resolutionScale = upscaleMethodNoMenu == UpscaleMethod::kDisabled ? 1.0f : 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)effectiveQuality);

	{
		static float previousResolutionScale = -1.0f;
		if (previousResolutionScale != resolutionScale) {
			L->info("Resolution scale changed: {:.4f} -> {:.4f} (qualityMode={}, enb={}, method={})",
				previousResolutionScale, resolutionScale, settings.qualityMode, cs::env::IsENBLoaded(), static_cast<uint>(upscaleMethodNoMenu));
			previousResolutionScale = resolutionScale;
		}
	}

	// Negative mip bias compensates for sub-native render scale.
	float currentMipBias = 0.0f;
	if ((upscaleMethodNoMenu == UpscaleMethod::kDLSS || upscaleMethodNoMenu == UpscaleMethod::kFSR) && resolutionScale < 1.0f)
		currentMipBias = std::log2f(resolutionScale) - 1.0f;

	UpdateSamplerStates(currentMipBias);
	UpdateRenderTargets(resolutionScale, resolutionScale);

	if (upscaleMethod == UpscaleMethod::kDisabled) {
		resolutionScale = 1.0f;
	}

	// Apply sub-pixel TAA jitter for temporal upscalers.
	if (upscaleMethod != UpscaleMethod::kDisabled) {
		auto screenWidth = gameViewport->screenWidth;
		auto screenHeight = gameViewport->screenHeight;

		auto renderWidth = static_cast<uint>(static_cast<float>(screenWidth) * resolutionScale);
		auto phaseCount = ffxFsr3GetJitterPhaseCount(renderWidth, screenWidth);
		ffxFsr3GetJitterOffset(&jitter.x, &jitter.y, gameViewport->frameCount, phaseCount);

		static bool loggedFirstJitter = false;
		if (!loggedFirstJitter) {
			L->info("First jitter: screen={}x{}, renderWidth={}, phaseCount={}, jitter=({}, {})",
				screenWidth, screenHeight, renderWidth, phaseCount, jitter.x, jitter.y);
			loggedFirstJitter = true;
		}

		// Convert to NDC; DirectX needs X negated.
		gameViewport->offsetX = 2.0f * -jitter.x / static_cast<float>(screenWidth);
		gameViewport->offsetY = 2.0f * jitter.y / static_cast<float>(screenHeight);
	}

	originalDynamicHeightRatio = resolutionScale;
	originalDynamicWidthRatio = resolutionScale;

	cs::engine::dynres::Set(renderTargetManager, originalDynamicWidthRatio, originalDynamicHeightRatio,
		originalDynamicWidthRatio != 1.0f || originalDynamicHeightRatio != 1.0f);

	CheckResources();
}

void Upscaling::Upscale()
{
	if (upscaleMethod == UpscaleMethod::kDisabled)
		return;

	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Unbind + restore engine OM around sampling/copying the frame buffer; clears CS slots on exit.
	cs::engine::ComputeOMScope omcs(context);

	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)cs::engine::RenderTarget::kFrameBuffer].srView);

	winrt::com_ptr<ID3D11Resource> frameBufferResource;
	frameBufferSRV->GetResource(frameBufferResource.put());

	static auto gameViewport = cs::engine::GetGraphicsState();
	static auto renderTargetManager = cs::engine::GetRenderTargetManager();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(screenSize.x * cs::engine::dynres::GetWidthRatio(renderTargetManager), screenSize.y * cs::engine::dynres::GetHeightRatio(renderTargetManager));

	// Copy frame buffer into the DLSS/FSR input texture.
	context->CopyResource(upscalingTexture->resource.get(), frameBufferResource.get());

	static bool loggedOnce = false;
	if (!loggedOnce) {
		D3D11_TEXTURE2D_DESC fbDesc{}, utDesc{};
		static_cast<ID3D11Texture2D*>(frameBufferResource.get())->GetDesc(&fbDesc);
		upscalingTexture->resource->GetDesc(&utDesc);
		L->info("First Upscale dispatch: method={} (1=FSR, 2=DLSS), screen={}x{}, render={}x{}, jitter=({}, {}), qualityMode={}",
			static_cast<uint>(upscaleMethod),
			(uint)screenSize.x, (uint)screenSize.y, (uint)renderSize.x, (uint)renderSize.y,
			jitter.x, jitter.y, settings.qualityMode);
		L->info("FrameBuffer texture: {}x{} format={}", fbDesc.Width, fbDesc.Height, (uint)fbDesc.Format);
		L->info("Upscaling texture: {}x{} format={}", utDesc.Width, utDesc.Height, (uint)utDesc.Format);
		L->info("dynamicWidthRatio={}, dynamicHeightRatio={}", cs::engine::dynres::GetWidthRatio(renderTargetManager), cs::engine::dynres::GetHeightRatio(renderTargetManager));
		loggedOnce = true;
	}

	// Dilate DLSS motion vectors for temporal stability.
	if (upscaleMethod == UpscaleMethod::kDLSS){
		{
			UpdateAndBindUpscalingCB(context, screenSize, renderSize);

			auto motionVectorSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)cs::engine::RenderTarget::kMotionVectors].srView);
			auto depthTextureSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->depthStencilTargets[(uint)cs::engine::DepthStencilTarget::kMain].srViewDepth);

			ID3D11ShaderResourceView* views[2] = { motionVectorSRV, depthTextureSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { dilatedMotionVectorTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			auto dilateCS = GetDilateMotionVectorCS();
			if (!dilateCS) {
				L->error("Failed to compile DilateMotionVector compute shader");
				return;
			}
			context->CSSetShader(dilateCS, nullptr, 0);

			uint dispatchX = (uint)std::ceil(renderSize.x / 8.0f);
			uint dispatchY = (uint)std::ceil(renderSize.y / 8.0f);
			context->Dispatch(dispatchX, dispatchY, 1);
		}

		// Clear DLSS compute bindings to avoid SRV/UAV hazards.
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		ID3D11ShaderResourceView* views[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	auto effectiveQuality = GetEffectiveQualityMode();
	{
		TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "Eval");
		if (upscaleMethod == UpscaleMethod::kDLSS) {
			Streamline::GetSingleton()->Upscale(upscalingTexture.get(), dilatedMotionVectorTexture.get(), jitter, renderSize, effectiveQuality);
		} else if (upscaleMethod == UpscaleMethod::kFSR) {
			FidelityFX::GetSingleton()->Upscale(upscalingTexture.get(), jitter, renderSize);
		}
	}

	// Copy upscaled output back into the frame buffer.
	cs::engine::CopyResourcePreservingOM(context, frameBufferResource.get(), upscalingTexture->resource.get());

	static bool copyLogged = false;
	if (!copyLogged) {
		L->info("CopyResource back to frame buffer executed");
		copyLogged = true;
	}
}

void Upscaling::CreateUpscalingResources()
{
	if (cs::Streamline::GetSingleton()->featureDLSS) {
		auto renderer = RE::BSGraphics::GetRendererData();
		auto& main = renderer->renderTargets[(uint)cs::engine::RenderTarget::kMain];

		D3D11_TEXTURE2D_DESC texDesc{};
		reinterpret_cast<ID3D11Texture2D*>(main.texture)->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = {.MipSlice = 0 }
		};

		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		uavDesc.Format = texDesc.Format;

		dilatedMotionVectorTexture = std::make_unique<Texture2D>(texDesc);
		dilatedMotionVectorTexture->CreateUAV(uavDesc);
	}
}

void Upscaling::DestroyUpscalingResources()
{
	if (cs::Streamline::GetSingleton()->featureDLSS) {
		dilatedMotionVectorTexture = nullptr;
	}
}

void Upscaling::OnD3D11Ready(IDXGIAdapter* /*a_adapter*/, ID3D11Device* /*a_device*/)
{
	Streamline::GetSingleton()->CacheDLSSFunctions();
}

void Upscaling::PatchSSRShader()
{
	static auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Replace SSR pixel shader with the scaled-render-target variant.
	context->PSSetShader(GetBSImagespaceShaderSSLRRaytracing(), nullptr, 0);
}

	void Upscaling::Load()
	{
		L->info("ENB state: {}", cs::env::IsENBLoaded() ? "loaded (native AA only)" : "not loaded (full pipeline active)");
		cs::Streamline::GetSingleton()->RequestFeature(sl::kFeatureDLSS);

		L->info("Installing DX11 hooks...");
		DX11Hooks::Install();
		L->info("DX11 hooks installed");

		L->info("Installing upscaling hooks...");
		Upscaling::InstallHooks();
		L->info("Upscaling hooks installed");
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(Upscaling::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}

}
