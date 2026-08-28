#include "Upscaling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>
#include <REX/TScopeExit.h>
#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/ComputeScope.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/SwapChainHook.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "UpscalingAnchors.h"
#include "UpscalingPublication.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling");

		constexpr const wchar_t* kEncodeTexturesPath = L"Data\\Shaders\\Upscaling\\EncodeTexturesCS.hlsl";
		constexpr const wchar_t* kDepthRefractionUpscalePath = L"Data\\Shaders\\Upscaling\\DepthRefractionUpscalePS.hlsl";
		constexpr const wchar_t* kUpscaleVSPath = L"Data\\Shaders\\Upscaling\\UpscaleVS.hlsl";
		constexpr const wchar_t* kSSLRRaytracingPath = L"Data\\Shaders\\Upscaling\\BSImagespaceShaderSSLRRaytracing.hlsl";
		constexpr const wchar_t* kCopyDepthForFrameGenerationPath =
			L"Data\\Shaders\\Upscaling\\CopyDepthForFrameGenerationCS.hlsl";

		// RT4 supplies post-alpha color for first-person alpha conditioning.
		constexpr auto kSceneColorTarget = cs::engine::RenderTarget::kMainTemp;
		constexpr auto kPreAlphaColorTarget = cs::engine::RenderTarget::kMain;
		// RT29 holds full-resolution R16G16_FLOAT motion.
		constexpr auto kMotionVectorTarget = cs::engine::RenderTarget::kMotionVectors;
		constexpr auto kNormalsTarget = cs::engine::RenderTarget::kGbufferNormal;
		constexpr auto kRefractionNormalTarget = cs::engine::RenderTarget::kRefractionNormal;

		std::int32_t GetJitterPhaseCount(std::int32_t a_renderWidth, std::int32_t a_displayWidth)
		{
			return static_cast<std::int32_t>(
				8.0f * std::pow(static_cast<float>(a_displayWidth) / a_renderWidth, 2.0f));
		}

		float Halton(std::int32_t a_index, std::int32_t a_base)
		{
			float fraction = 1.0f;
			float result = 0.0f;
			for (auto index = a_index; index > 0; index /= a_base) {
				fraction /= static_cast<float>(a_base);
				result += fraction * static_cast<float>(index % a_base);
			}
			return result;
		}

		void GetJitterOffset(
			float& a_outX,
			float& a_outY,
			std::int32_t a_index,
			std::int32_t a_phaseCount)
		{
			const auto sequenceIndex = (a_index % a_phaseCount) + 1;
			a_outX = Halton(sequenceIndex, 2) - 0.5f;
			a_outY = Halton(sequenceIndex, 3) - 0.5f;
		}

		float CalculateMipBias(float a_renderWidth, float a_screenWidth, bool a_isDLSS)
		{
			if (a_screenWidth <= 0.0f || a_renderWidth <= 0.0f) {
				return 0.0f;
			}
			return std::log2(a_renderWidth / a_screenWidth) - (a_isDLSS ? 1.0f : 0.0f);
		}

		std::string SettingError(std::string_view a_key, std::string_view a_reason)
		{
			return "settings." + std::string(a_key) + ": " + std::string(a_reason);
		}

		bool AcceptSetting(
			feature_config::ScalarReadStatus a_status,
			std::string_view a_key,
			std::string_view a_expected,
			std::string& a_error)
		{
			switch (a_status) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = SettingError(a_key, "expected " + std::string(a_expected));
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = SettingError(a_key, "invalid value");
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = SettingError(a_key, "value is out of range");
				break;
			}
			return false;
		}

		bool ReadEnum(
			const toml::table& a_table,
			std::string_view a_key,
			std::uint32_t& a_value,
			std::uint64_t a_max,
			std::string& a_error)
		{
			auto raw = static_cast<std::uint64_t>(a_value);
			const auto status = feature_config::ReadUnsignedInteger(a_table, a_key, raw, 0, a_max);
			if (!AcceptSetting(status, a_key, "integer", a_error)) {
				return false;
			}
			a_value = static_cast<std::uint32_t>(raw);
			return true;
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			Upscaling::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode) {
				return true;
			}

			const auto* settingsTable = settingsNode->as_table();
			if (!settingsTable) {
				a_error = "settings: expected table";
				return false;
			}

			if (!AcceptSetting(feature_config::ReadBool(*settingsTable, "enabled", a_candidate.enabled),
					"enabled", "boolean", a_error)
				|| !ReadEnum(*settingsTable, "upscale_method", a_candidate.upscaleMethod, 3, a_error)
				|| !ReadEnum(*settingsTable, "upscale_method_no_dlss", a_candidate.upscaleMethodNoDLSS, 3, a_error)
				|| !ReadEnum(*settingsTable, "quality_mode", a_candidate.qualityMode, 4, a_error)
				|| !ReadEnum(*settingsTable, "frame_generation_mode", a_candidate.frameGenerationMode, 1, a_error)
				|| !ReadEnum(*settingsTable, "frame_generation_force_enable", a_candidate.frameGenerationForceEnable, 1, a_error)
				|| !AcceptSetting(feature_config::ReadBool(
						*settingsTable,
						"frame_generation_allow_in_menus",
						a_candidate.frameGenerationAllowInMenus),
					"frame_generation_allow_in_menus", "boolean", a_error)
				|| !ReadEnum(*settingsTable, "streamline_log_level", a_candidate.streamlineLogLevel, 2, a_error)
				|| !ReadEnum(*settingsTable, "preset_dlss", a_candidate.presetDLSS, 4, a_error)
				|| !AcceptSetting(feature_config::ReadFloat(*settingsTable, "sharpness_fsr", a_candidate.sharpnessFSR, 0.0f, 1.0f),
					"sharpness_fsr", "number", a_error)
				|| !AcceptSetting(feature_config::ReadBool(*settingsTable, "sharpness_enabled_dlss", a_candidate.sharpnessEnabledDLSS),
					"sharpness_enabled_dlss", "boolean", a_error)
				|| !AcceptSetting(feature_config::ReadFloat(*settingsTable, "sharpness_dlss", a_candidate.sharpnessDLSS, 0.0f, 1.0f),
					"sharpness_dlss", "number", a_error)) {
				return false;
			}

			return true;
		}

		void ForceViewportToRenderTargetDimensions()
		{
			using func_t = void (*)();
			static REL::Relocation<func_t> func{ REL::ID({ upscaling_anchors::kUnprovenOnOG,
				upscaling_anchors::kForceViewportToRenderTargetDimensions,
				upscaling_anchors::kForceViewportToRenderTargetDimensions }) };
			func();
		}

		// Fallout 4 controls TAA through this global and the graphics state.
		void SetTemporalEnabled(bool a_enabled)
		{
			if (auto* global = cs::engine::GetTemporalAAEnableGlobal()) {
				*global = a_enabled ? 1u : 0u;
			}
			if (auto* state = cs::engine::GetGraphicsState()) {
				state->taaState = a_enabled
					? RE::BSGraphics::TAA_STATE::kEnabled
					: RE::BSGraphics::TAA_STATE::kDisabled;
			}
		}

		// Engine thunks quarantine faults instead of unwinding through engine frames.
		template <class Fn>
		void GuardedThunkBody(const char* a_where, Fn&& a_fn) noexcept
		{
			try {
				a_fn();
				return;
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "{} failed: {}", a_where, e.what());
			} catch (...) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "{} failed", a_where);
			}
			Upscaling::GetSingleton()->QuarantineAfterException(a_where);
		}

		// Validate all anchors first to avoid partial hook installation.
		bool IsCallSiteTargeting(std::uintptr_t a_site, std::uintptr_t a_expectedTarget)
		{
			if (!a_site || !a_expectedTarget) {
				return false;
			}
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_site);
			if (bytes[0] != 0xE8) {
				return false;
			}
			std::int32_t displacement = 0;
			std::memcpy(&displacement, bytes + 1, sizeof(displacement));
			const auto target = a_site + 5 + static_cast<std::intptr_t>(displacement);
			return target == a_expectedTarget;
		}

		bool ViewReferencesTexture(
			ID3D11ShaderResourceView* a_view,
			ID3D11Texture2D* a_texture) noexcept
		{
			if (!a_view || !a_texture) {
				return false;
			}
			winrt::com_ptr<ID3D11Resource> resource;
			a_view->GetResource(resource.put());
			return resource.get() == a_texture;
		}

		bool HaveMatchingCopyContract(
			ID3D11Texture2D* a_left,
			ID3D11Texture2D* a_right) noexcept
		{
			if (!a_left || !a_right || a_left == a_right) {
				return false;
			}
			D3D11_TEXTURE2D_DESC left{};
			D3D11_TEXTURE2D_DESC right{};
			a_left->GetDesc(&left);
			a_right->GetDesc(&right);
			return left.Width == right.Width &&
				left.Height == right.Height &&
				left.MipLevels == right.MipLevels &&
				left.ArraySize == right.ArraySize &&
				left.Format == right.Format &&
				left.SampleDesc.Count == right.SampleDesc.Count &&
				left.SampleDesc.Quality == right.SampleDesc.Quality;
		}

		bool TryGetFrameBufferTexture(
			winrt::com_ptr<ID3D11Texture2D>& a_texture,
			D3D11_TEXTURE2D_DESC& a_desc) noexcept
		{
			auto* rtv = cs::engine::GetRenderTargetRTV(cs::engine::RenderTarget::kFrameBuffer);
			if (!rtv) {
				return false;
			}

			winrt::com_ptr<ID3D11Resource> resource;
			rtv->GetResource(resource.put());
			if (!resource || FAILED(resource->QueryInterface(IID_PPV_ARGS(a_texture.put())))) {
				return false;
			}

			a_texture->GetDesc(&a_desc);
			return a_desc.Width > 0 &&
				a_desc.Height > 0 &&
				a_desc.MipLevels == 1 &&
				a_desc.ArraySize == 1 &&
				a_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
				a_desc.SampleDesc.Count == 1 &&
				a_desc.SampleDesc.Quality == 0;
		}

		bool MatchesTextureContract(
			const cs::buffer::Texture2D* a_texture,
			const D3D11_TEXTURE2D_DESC& a_frameBufferDesc,
			DXGI_FORMAT a_format) noexcept
		{
			if (!a_texture || !a_texture->resource || !a_texture->srv || !a_texture->uav) {
				return false;
			}

			const auto& desc = a_texture->desc;
			return desc.Width == a_frameBufferDesc.Width &&
				desc.Height == a_frameBufferDesc.Height &&
				desc.MipLevels == 1 &&
				desc.ArraySize == 1 &&
				desc.Format == a_format &&
				desc.SampleDesc.Count == 1 &&
				desc.SampleDesc.Quality == 0;
		}

		std::uint64_t GetEngineFrame() noexcept
		{
			auto* state = cs::engine::GetGraphicsState();
			return state ? state->frameCount : 0;
		}

		double GetRefreshRate(HWND a_window)
		{
			const HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
			MONITORINFOEXW monitorInfo{};
			monitorInfo.cbSize = sizeof(monitorInfo);
			if (!GetMonitorInfoW(monitor, &monitorInfo)) {
				return 0.0;
			}
			DEVMODEW mode{};
			mode.dmSize = sizeof(mode);
			if (!EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
				return 0.0;
			}
			return mode.dmDisplayFrequency > 1 ? static_cast<double>(mode.dmDisplayFrequency) : 0.0;
		}
	}

	Upscaling* Upscaling::GetSingleton()
	{
		static Upscaling instance;
		return &instance;
	}

	bool Upscaling::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}

		settings = candidate;
		_bootSettings = candidate;
		return true;
	}

	void Upscaling::SaveSettings()
	{
		toml::table table;
		table.insert_or_assign("enabled", settings.enabled);
		table.insert_or_assign("upscale_method", static_cast<std::int64_t>(settings.upscaleMethod));
		table.insert_or_assign("upscale_method_no_dlss", static_cast<std::int64_t>(settings.upscaleMethodNoDLSS));
		table.insert_or_assign("quality_mode", static_cast<std::int64_t>(settings.qualityMode));
		table.insert_or_assign("frame_generation_mode", static_cast<std::int64_t>(settings.frameGenerationMode));
		table.insert_or_assign(
			"frame_generation_force_enable",
			static_cast<std::int64_t>(settings.frameGenerationForceEnable));
		table.insert_or_assign(
			"frame_generation_allow_in_menus",
			settings.frameGenerationAllowInMenus);
		table.insert_or_assign("streamline_log_level", static_cast<std::int64_t>(settings.streamlineLogLevel));
		table.insert_or_assign("preset_dlss", static_cast<std::int64_t>(settings.presetDLSS));
		table.insert_or_assign("sharpness_fsr", settings.sharpnessFSR);
		table.insert_or_assign("sharpness_enabled_dlss", settings.sharpnessEnabledDLSS);
		table.insert_or_assign("sharpness_dlss", settings.sharpnessDLSS);

		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), table); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void Upscaling::RestoreDefaultSettings()
	{
		settings = Settings{};
		SaveSettings();
	}

	void Upscaling::LoadUpscalingSDKs()
	{
		if (!streamline.triedInitialization) {
			streamline.LoadInterposer();
		}
		fidelityFX.LoadFrameGeneration();
	}

	void Upscaling::Load()
	{
		if (REX::FModule::IsRuntimeOG()) {
			FailLoad("Upscaling engine anchors are proven for NG/AE only; the OG runtime is unsupported");
			return;
		}

		// The shared swap-chain broker remains the sole IAT owner.
		cs::render::RegisterPreCreateDeviceAndSwapChain(
			[](DXGI_SWAP_CHAIN_DESC* a_desc, std::vector<D3D_FEATURE_LEVEL>& a_featureLevels) {
				GetSingleton()->OnPreCreateDeviceAndSwapChain(a_desc, a_featureLevels);
			});
		cs::render::RegisterPostCreateDeviceAndSwapChain(
			[](IDXGIAdapter* a_adapter, ID3D11Device** a_device, IDXGISwapChain** a_swapChain) {
				GetSingleton()->OnPostCreateDeviceAndSwapChain(a_adapter, a_device, a_swapChain);
			});
		if (!cs::render::RegisterReplacementCreateDeviceAndSwapChain(
				[](cs::render::CreateDeviceAndSwapChainContext& a_context) {
					return GetSingleton()->OnReplacementCreateDeviceAndSwapChain(a_context);
				})) {
			FailLoad("Failed to register the sole Upscaling swap-chain replacement route");
			return;
		}

		L->info("Registered swap-chain broker callbacks");
	}

	void Upscaling::OnPreCreateDeviceAndSwapChain(
		DXGI_SWAP_CHAIN_DESC*,
		std::vector<D3D_FEATURE_LEVEL>& a_featureLevels)
	{
		LoadUpscalingSDKs();

		// FO4 requests 11.0, but FSR3's DX11 shaders require the 11.1 UAV slot count.
		if (std::ranges::find(a_featureLevels, D3D_FEATURE_LEVEL_11_1) == a_featureLevels.end()) {
			a_featureLevels.insert(a_featureLevels.begin(), D3D_FEATURE_LEVEL_11_1);
		}
	}

	std::optional<HRESULT> Upscaling::OnReplacementCreateDeviceAndSwapChain(
		cs::render::CreateDeviceAndSwapChainContext& a_context)
	{
		if (!a_context.realCreate || !a_context.swapChainDesc || !a_context.swapChain ||
			!a_context.device || !a_context.immediateContext || !settings.frameGenerationMode ||
			!a_context.swapChainDesc->Windowed || !fidelityFX.IsFrameGenerationModuleReady()) {
			return std::nullopt;
		}

		const double refreshRate = GetRefreshRate(a_context.swapChainDesc->OutputWindow);
		if (refreshRate < 120.0 && !settings.frameGenerationForceEnable) {
			L->info(
				"Frame generation startup route skipped at {:.2f} Hz; force enable is off",
				refreshRate);
			return std::nullopt;
		}

		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* immediateContext = nullptr;
		D3D_FEATURE_LEVEL featureLevel{};
		const HRESULT deviceResult = a_context.realCreate(
			a_context.adapter,
			a_context.driverType,
			a_context.software,
			a_context.flags,
			a_context.featureLevels,
			a_context.featureLevelCount,
			a_context.sdkVersion,
			nullptr,
			nullptr,
			&device,
			&featureLevel,
			&immediateContext);
		if (FAILED(deviceResult) || !device || !immediateContext) {
			if (immediateContext) {
				immediateContext->Release();
			}
			if (device) {
				device->Release();
			}
			L->warn(
				"Frame-generation device creation failed ({:#010x}); using native D3D11 creation",
				static_cast<std::uint32_t>(deviceResult));
			return std::nullopt;
		}

		HRESULT proxyResult = E_FAIL;
		try {
			proxyResult = dx12SwapChain.Initialize(
				a_context.adapter,
				device,
				immediateContext,
				*a_context.swapChainDesc,
				fidelityFX);
		} catch (const std::exception& e) {
			dx12SwapChain.Rollback();
			L->warn("Frame-generation proxy setup threw: {}; using native D3D11 creation", e.what());
		} catch (...) {
			dx12SwapChain.Rollback();
			L->warn("Frame-generation proxy setup threw; using native D3D11 creation");
		}
		if (FAILED(proxyResult) || !dx12SwapChain.GetProxy()) {
			immediateContext->Release();
			device->Release();
			L->warn(
				"Frame-generation proxy setup failed ({:#010x}); using native D3D11 creation",
				static_cast<std::uint32_t>(proxyResult));
			return std::nullopt;
		}

		*a_context.device = device;
		*a_context.immediateContext = immediateContext;
		*a_context.swapChain = dx12SwapChain.GetProxy();
		if (a_context.featureLevel) {
			*a_context.featureLevel = featureLevel;
		}
		L->info(
			"Frame-generation startup route published a two-buffer flip-discard proxy at {:.2f} Hz",
			refreshRate);
		return S_OK;
	}

	void Upscaling::OnPostCreateDeviceAndSwapChain(
		IDXGIAdapter* a_adapter,
		ID3D11Device** a_device,
		IDXGISwapChain** a_swapChain)
	{
		const bool proxyPath = a_swapChain && dx12SwapChain.Owns(*a_swapChain);
		streamline.UpgradeInterfaces(a_device, proxyPath ? nullptr : a_swapChain);
		if (proxyPath && a_device) {
			dx12SwapChain.SetOutwardD3D11Device(*a_device);
		}
		if (!streamline.SetDevice(a_device ? *a_device : nullptr)) {
			streamline.featureDLSS = false;
			L->error("Streamline device registration failed; DLSS is unavailable this session");
			return;
		}
		streamline.CheckFeatures(a_adapter);
		streamline.PostDevice();
	}

	void Upscaling::OnPostPostLoad()
	{
		using namespace upscaling_anchors;

		const auto anchor = [](std::uint64_t a_id) {
			return REL::ID({ kUnprovenOnOG, a_id, a_id });
		};
		const auto tupleTarget = [](const std::uint64_t (&a_ids)[3]) {
			return REL::ID({ a_ids[0], a_ids[1], a_ids[2] }).address();
		};

		const auto viewportSite =
			anchor(kDrawWorldBegin).address() + kDrawWorldBeginSetDynamicViewportCall;
		const auto viewportTarget = anchor(kSetDynamicViewportAsDefault).address();
		const auto imagespaceCallSite =
			anchor(kMainDrawWorldAndUI).address() + kMainDrawWorldAndUIImagespaceCall;
		const auto imagespaceTarget = anchor(kDrawWorldImagespace).address();
		const auto imagespaceUpscaleSite =
			imagespaceTarget + kDrawWorldImagespaceUpscaleCall;
		const auto dynamicResolutionSite =
			anchor(kRenderPreUI).address() + kRenderPreUIUpdateDynamicResolutionCall;
		const auto dynamicResolutionTarget = anchor(kUpdateDynamicResolution).address();
		const auto samplerStateTable = anchor(kSamplerStateTable).address();
		if (!samplerBias.Initialize(samplerStateTable)) {
			FailLoad("The sampler-state table address did not resolve");
			return;
		}
		const auto runtimeIndex =
			static_cast<std::size_t>(REX::FModule::GetRuntimeIndex());
		const auto firstPersonAlphaSite =
			REL::ID({
				kDrawWorldForward[0],
				kDrawWorldForward[1],
				kDrawWorldForward[2] })
				.address() +
			kFirstPersonAlphaCall[runtimeIndex];
		const auto firstPersonAlphaTarget = kRenderAlphaGeometry[runtimeIndex]
			? REL::ID({
				  kRenderAlphaGeometry[0],
				  kRenderAlphaGeometry[1],
				  kRenderAlphaGeometry[2] })
				  .address()
			: 0;
		const auto renderEffectRangeSite =
			imagespaceTarget + kDrawWorldImagespaceRenderEffectRangeCall;
		const auto deferredCompositeSite =
			anchor(kDeferredComposite).address() + kDeferredCompositeRenderPassCall;
		const auto sslrSite =
			anchor(kSSLRRaytracingSetupTechnique).address() + kSSLRRaytracingBeginTechniqueCall;
		const auto vatsSite =
			anchor(kVatsUpdateParams).address() + kVatsSetPixelConstantCall;
		const auto loadingMenuSite =
			anchor(kLoadingMenuUpdateTemporalData).address() + kLoadingMenuUpdateTemporalDataCall;
		const auto deferredPrePassSite =
			anchor(kRenderPreUI).address() + kRenderPreUIDeferredPrePassCall;
		const auto forwardSite =
			anchor(kRenderPreUI).address() + kRenderPreUIForwardCall;
		if (!IsCallSiteTargeting(viewportSite, viewportTarget)) {
			FailLoad("World viewport selection site did not contain the expected call");
			return;
		}
		if (!IsCallSiteTargeting(imagespaceCallSite, imagespaceTarget)) {
			FailLoad("DrawWorld::Imagespace call site did not contain the expected call");
			return;
		}
		if (!IsCallSiteTargeting(imagespaceUpscaleSite, viewportTarget)) {
			FailLoad("Imagespace viewport handoff did not contain the expected call");
			return;
		}
		if (!IsCallSiteTargeting(dynamicResolutionSite, dynamicResolutionTarget)) {
			FailLoad("Dynamic-resolution site did not contain the expected call");
			return;
		}
		if (!firstPersonAlphaTarget ||
			!IsCallSiteTargeting(firstPersonAlphaSite, firstPersonAlphaTarget)) {
			FailLoad("First-person alpha site did not contain the expected RenderAlphaGeometry call");
			return;
		}
		if (!IsCallSiteTargeting(
				renderEffectRangeSite,
				tupleTarget(kImageSpaceManagerRenderEffectRange))) {
			FailLoad("Imagespace effect-range site did not contain the expected RenderEffectRange call");
			return;
		}
		if (!IsCallSiteTargeting(
				deferredCompositeSite,
				tupleTarget(kBSBatchRendererRenderPassImmediately))) {
			FailLoad("Deferred composite site did not contain the expected RenderPassImmediately call");
			return;
		}
		if (!IsCallSiteTargeting(sslrSite, tupleTarget(kBSShaderBeginTechnique))) {
			FailLoad("SSLR raytracing site did not contain the expected BeginTechnique call");
			return;
		}
		if (!IsCallSiteTargeting(vatsSite, tupleTarget(kImageSpaceShaderParamSetPixelConstant))) {
			FailLoad("VATS parameter site did not contain the expected SetPixelConstant call");
			return;
		}
		if (!IsCallSiteTargeting(
				loadingMenuSite,
				tupleTarget(kBSGraphicsStateUpdateTemporalData))) {
			FailLoad("Loading-menu site did not contain the expected UpdateTemporalData call");
			return;
		}
		if (!IsCallSiteTargeting(
				deferredPrePassSite,
				tupleTarget(kDrawWorldDeferredPrePass))) {
			FailLoad("Render_PreUI site did not contain the expected DeferredPrePass call");
			return;
		}
		if (!IsCallSiteTargeting(forwardSite, tupleTarget(kRenderPreUIForwardTarget))) {
			FailLoad("Render_PreUI site did not contain the expected Forward call");
			return;
		}

		stl::write_thunk_call<DrawWorldBegin_SetDynamicViewport>(viewportSite);
		stl::write_thunk_call<Main_UpdateJitter>(
			anchor(kDrawWorldBegin).address() + kDrawWorldBeginUpdateTemporalDataCall);
		stl::write_thunk_call<Main_PostProcessing>(imagespaceCallSite);
		stl::write_thunk_call<DrawWorld_FirstPersonAlpha>(firstPersonAlphaSite);
		stl::write_thunk_call<DrawWorldImagespace_Upscale>(imagespaceUpscaleSite);
		// Split the imagespace effect chain so HDR effects stay at render resolution.
		stl::write_thunk_call<DrawWorldImagespace_RenderEffectRange>(renderEffectRangeSite);
		// Keep dynamic-resolution G-buffers valid through the deferred lighting composite.
		stl::write_thunk_call<DeferredComposite_RenderPass>(deferredCompositeSite);
		// Reconstructed screen-space reflection shader resolves scaled targets.
		stl::write_thunk_call<SSLRRaytracing_BeginTechnique>(sslrSite);
		// Scale the VATS outline thickness constant by the dynamic ratio.
		stl::write_thunk_call<Vats_SetPixelConstant>(vatsSite);
		// LoadingMenu renders full-extent, so neutralize its jitter and ratios.
		stl::write_thunk_call<LoadingMenu_UpdateTemporalData>(loadingMenuSite);
		// Bias the global sampler table around the material passes to match the render resolution.
		stl::write_thunk_call<RenderPreUI_DeferredPrePass>(deferredPrePassSite);
		stl::write_thunk_call<RenderPreUI_Forward>(forwardSite);

		stl::detour_thunk<LensFlare_RenderLensFlare>(anchor(kLensFlareRenderLensFlare));
		stl::detour_thunk<BSImageSpace_Init_FXAA>(anchor(kImageSpaceInitEffects));
		// ResetWindow can run without the render-target create callback.
		stl::detour_thunk<Renderer_ResetWindow>(anchor(kRendererResetWindow));
		stl::detour_thunk<BSShaderRenderTargets_Create>(anchor(kBSShaderRenderTargetsCreate));
		// Restore the render-scale ratios once the imagespace function returns.
		stl::detour_thunk<DrawWorldImagespace_RestoreRatios>(anchor(kDrawWorldImagespace));
		// Publish the scale and refresh proxies after the native dynamic-resolution update.
		stl::write_thunk_call<Main_UpdateDynamicResolution>(dynamicResolutionSite);
		stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(
			RE::VTABLE::ImageSpaceEffectTemporalAA[0]);
		_hooksInstalled.store(true, std::memory_order_release);
		L->info("Installed hooks");
	}

	void Upscaling::OnDataLoaded()
	{
		SetTemporalEnabled(false);
		if (!MenuOpenCloseEventHandler::Register()) {
			L->warn("Loading-menu reset listener was not registered");
		}
	}

	Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod() const
	{
		if (!settings.enabled) {
			return UpscaleMethod::kNONE;
		}
		if (streamline.featureDLSS)
			return (UpscaleMethod)settings.upscaleMethod;
		return (UpscaleMethod)settings.upscaleMethodNoDLSS;
	}

	bool Upscaling::IsUpscalingActive() const
	{
		auto method = GetUpscaleMethod();

		if (!(method == UpscaleMethod::kFSR || method == UpscaleMethod::kDLSS)) {
			return false;
		}

		return resolutionScale.x < .99f;
	}

	bool Upscaling::IsFrameGenerationDx12PathActive() const noexcept
	{
		return dx12SwapChain.IsReady();
	}

	bool Upscaling::IsFrameGenerationActive() const noexcept
	{
		return IsFrameGenerationDx12PathActive() && settings.frameGenerationMode &&
			fidelityFX.IsFrameGenerationActive();
	}

	bool Upscaling::ShouldUseFrameGenerationThisFrame() const noexcept
	{
		if (!IsFrameGenerationDx12PathActive() || !settings.frameGenerationMode ||
			!dx12SwapChain.IsFrameGenerationReady()) {
			return false;
		}
		if (settings.frameGenerationAllowInMenus) {
			return true;
		}

		auto* main = RE::Main::GetSingleton();
		auto* ui = RE::UI::GetSingleton();
		if (!main || !ui || main->inMenuMode) {
			return false;
		}
		return !ui->GetMenuOpen<RE::MainMenu>() &&
			!ui->GetMenuOpen<RE::PauseMenu>() &&
			!ui->GetMenuOpen<RE::LoadingMenu>() &&
			!ui->GetMenuOpen<RE::PipboyMenu>();
	}

	std::pair<std::uint32_t, std::uint32_t> Upscaling::GetRenderSize() const noexcept
	{
		const auto* state = cs::engine::GetGraphicsState();
		if (!state) {
			return { 0, 0 };
		}
		return {
			static_cast<std::uint32_t>(state->screenWidth * resolutionScale.x),
			static_cast<std::uint32_t>(state->screenHeight * resolutionScale.y)
		};
	}

	float Upscaling::GetMipBias() const
	{
		return _mipBias.load(std::memory_order_relaxed);
	}

	bool Upscaling::CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod)
	{
		L->debug("Creating texture resources for method {} ({})",
			static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

		if (a_upscalemethod != UpscaleMethod::kDLSS &&
			a_upscalemethod != UpscaleMethod::kFSR) {
			return true;
		}

		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		D3D11_TEXTURE2D_DESC frameBufferDesc{};
		if (!TryGetFrameBufferTexture(frameBuffer, frameBufferDesc)) {
			L->error("RT0 is unavailable or incompatible with the SDR upscaling contract");
			return false;
		}

		D3D11_TEXTURE2D_DESC colorDesc = frameBufferDesc;
		colorDesc.Usage = D3D11_USAGE_DEFAULT;
		colorDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		colorDesc.CPUAccessFlags = 0;
		colorDesc.MiscFlags = 0;

		const auto createTexture = [](const D3D11_TEXTURE2D_DESC& a_desc) {
			auto texture = std::make_unique<cs::buffer::Texture2D>(a_desc);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = a_desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			texture->CreateSRV(srvDesc);

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = a_desc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			texture->CreateUAV(uavDesc);
			return texture.release();
		};

		if (!upscalingTexture) {
			upscalingTexture = createTexture(colorDesc);
		}

		auto maskDesc = colorDesc;
		maskDesc.Format = DXGI_FORMAT_R8_UNORM;
		if (!reactiveMaskTexture) {
			reactiveMaskTexture = createTexture(maskDesc);
		}
		if (!transparencyCompositionMaskTexture) {
			transparencyCompositionMaskTexture = createTexture(maskDesc);
		}

		if (a_upscalemethod == UpscaleMethod::kDLSS) {
			if (!motionVectorCopyTexture) {
				auto* motionVector = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
				if (motionVector) {
					D3D11_TEXTURE2D_DESC motionTexDesc{};
					motionVector->GetDesc(&motionTexDesc);
					motionTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

					D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
					srvDesc.Format = motionTexDesc.Format;
					srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
					srvDesc.Texture2D.MostDetailedMip = 0;
					srvDesc.Texture2D.MipLevels = 1;

					D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
					uavDesc.Format = motionTexDesc.Format;
					uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
					uavDesc.Texture2D.MipSlice = 0;

					motionVectorCopyTexture = new cs::buffer::Texture2D(motionTexDesc);
					motionVectorCopyTexture->CreateSRV(srvDesc);
					motionVectorCopyTexture->CreateUAV(uavDesc);
				}
			}

			if (!sharpenerTexture) {
				sharpenerTexture = createTexture(colorDesc);
			}
		}

		return HasRequiredResources(a_upscalemethod);
	}

	bool Upscaling::HasRequiredResources(UpscaleMethod a_upscalemethod) const noexcept
	{
		if (a_upscalemethod != UpscaleMethod::kFSR &&
			a_upscalemethod != UpscaleMethod::kDLSS) {
			return true;
		}

		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		D3D11_TEXTURE2D_DESC frameBufferDesc{};
		if (!TryGetFrameBufferTexture(frameBuffer, frameBufferDesc) ||
			!MatchesTextureContract(
				upscalingTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8G8B8A8_UNORM) ||
			!MatchesTextureContract(
				reactiveMaskTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8_UNORM) ||
			!MatchesTextureContract(
				transparencyCompositionMaskTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8_UNORM)) {
			return false;
		}

		if (a_upscalemethod == UpscaleMethod::kFSR) {
			return fidelityFX.IsReady();
		}
		return motionVectorCopyTexture &&
			sharpenerTexture &&
			sharpenerTexture->resource.get() != upscalingTexture->resource.get() &&
			MatchesTextureContract(
				sharpenerTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8G8B8A8_UNORM);
	}

	void Upscaling::DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod)
	{
		L->debug("Destroying texture resources for method {} ({})",
			static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod));

		const auto destroy = [](cs::buffer::Texture2D*& a_texture) {
			if (!a_texture) {
				return;
			}
			a_texture->Reset();
			delete a_texture;
			a_texture = nullptr;
		};

		destroy(reactiveMaskTexture);
		destroy(transparencyCompositionMaskTexture);
		destroy(motionVectorCopyTexture);
		destroy(upscalingTexture);
		destroy(sharpenerTexture);
	}

	bool Upscaling::CheckResources(UpscaleMethod a_upscalemethod)
	{
		static auto previousUpscaleMode = UpscaleMethod::kTAA;
		static auto previousQualityMode = std::numeric_limits<std::uint32_t>::max();

		// Provider contexts are quality-sized.
		if (previousUpscaleMode == a_upscalemethod &&
			previousQualityMode == settings.qualityMode &&
			HasRequiredResources(a_upscalemethod)) {
			return true;
		}

		L->debug("Resource change detected - Upscale: {} ({}) -> {} ({}), quality: {} -> {}",
			static_cast<int>(previousUpscaleMode), magic_enum::enum_name(previousUpscaleMode),
			static_cast<int>(a_upscalemethod), magic_enum::enum_name(a_upscalemethod),
			previousQualityMode, settings.qualityMode);

		if (previousUpscaleMode == UpscaleMethod::kDLSS) {
			streamline.DestroyDLSSResources();
		} else if (previousUpscaleMode == UpscaleMethod::kFSR) {
			fidelityFX.DestroyFSRResources();
		}

		DestroyUpscalingTextureResources(a_upscalemethod);

		bool ok = true;
		if (a_upscalemethod == UpscaleMethod::kFSR) {
			ok = fidelityFX.CreateFSRResources();
		}

		ok = CreateUpscalingTextureResources(a_upscalemethod) && ok;

		previousUpscaleMode = a_upscalemethod;
		previousQualityMode = settings.qualityMode;
		fidelityFX.RequestFrameGenerationReset();
		return ok;
	}

	ID3D11ComputeShader* Upscaling::GetEncodeTexturesCS()
	{
		auto upscaleMethod = GetUpscaleMethod();
		uint methodIndex = (uint)upscaleMethod;

		if (!encodeTexturesCS[methodIndex]) {
			L->debug("Compiling EncodeTexturesCS.hlsl for upscale method {}", methodIndex);

			std::vector<std::pair<const char*, const char*>> defines = { { "FO4CS_SUBSTRATE", "1" } };

			switch (upscaleMethod) {
			case UpscaleMethod::kDLSS:
				defines.push_back({ "DLSS", "" });
				break;
			case UpscaleMethod::kFSR:
				defines.push_back({ "FSR", "" });
				break;
			default:
				break;
			}

			encodeTexturesCS[methodIndex].attach(
				(ID3D11ComputeShader*)cs::util::CompileShader(kEncodeTexturesPath, defines, "cs_5_0"));
		}
		return encodeTexturesCS[methodIndex].get();
	}

	ID3D11PixelShader* Upscaling::GetDepthRefractionUpscalePS()
	{
		if (!depthRefractionUpscalePS) {
			L->debug("Compiling DepthRefractionUpscalePS.hlsl");
			std::vector<std::pair<const char*, const char*>> defines = {
				{ "PSHADER", "" },
				{ "FO4CS_SUBSTRATE", "1" }
			};
			depthRefractionUpscalePS.attach(
				(ID3D11PixelShader*)cs::util::CompileShader(kDepthRefractionUpscalePath, defines, "ps_5_0"));
		}

		return depthRefractionUpscalePS.get();
	}

	ID3D11VertexShader* Upscaling::GetUpscaleVS()
	{
		if (!upscaleVS) {
			L->debug("Compiling UpscaleVS.hlsl");
			upscaleVS.attach(
				(ID3D11VertexShader*)cs::util::CompileShader(kUpscaleVSPath, { { "VSHADER", "" } }, "vs_5_0"));
		}

		return upscaleVS.get();
	}

	ID3D11PixelShader* Upscaling::GetSSLRRaytracingPS()
	{
		if (!sslrRaytracingPS && !_sslrCompileFailed) {
			L->debug("Compiling BSImagespaceShaderSSLRRaytracing.hlsl");
			sslrRaytracingPS.attach((ID3D11PixelShader*)cs::util::CompileShader(
				kSSLRRaytracingPath, {}, "ps_5_0"));
			if (!sslrRaytracingPS) {
				_sslrCompileFailed = true;
				L->error("BSImagespaceShaderSSLRRaytracing.hlsl failed to compile; SSR runs unpatched");
			}
		}
		return sslrRaytracingPS.get();
	}

	void Upscaling::PatchSSRShader()
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* shader = GetSSLRRaytracingPS();
		if (context && shader) {
			context->PSSetShader(shader, nullptr, 0);
		}
	}

	void Upscaling::ConfigureTAA()
	{
		auto upscaleMethod = GetUpscaleMethod();

		SetTemporalEnabled(upscaleMethod != UpscaleMethod::kNONE);
	}

	void Upscaling::UpdateResolutionScale(RE::BSGraphics::State* a_state, UpscaleMethod a_method)
	{
		if (!a_state) {
			resolutionScale = { 1.0f, 1.0f };
			return;
		}

		resolutionScale = { 1.0f, 1.0f };

		if (a_method != UpscaleMethod::kNONE && a_method != UpscaleMethod::kTAA &&
			a_state->screenWidth > 0 && a_state->screenHeight > 0) {
			const float scale =
				1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)settings.qualityMode);
			resolutionScale = { scale, scale };
		}
	}

	void Upscaling::ConfigureUpscaling()
	{
		auto* state = cs::engine::GetGraphicsState();
		if (!state) {
			return;
		}

		auto upscaleMethod = GetUpscaleMethod();
		UpdateResolutionScale(state, upscaleMethod);
		const auto [renderWidth, renderHeight] = GetRenderSize();

		// Fail closed before publishing any jitter, ratios, or mip bias.
		if (!CheckResources(upscaleMethod)) {
			_resourcesReady.store(false, std::memory_order_release);
			RestoreNativeFrameState();
			return;
		}

		float2 screenSize{ (float)state->screenWidth, (float)state->screenHeight };
		const bool upscalerActive =
			upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA;

		if (upscalerActive) {
			auto phaseCount = GetJitterPhaseCount(
				static_cast<std::int32_t>(renderWidth),
				static_cast<std::int32_t>(state->screenWidth));

			GetJitterOffset(
				jitter.x,
				jitter.y,
				static_cast<std::int32_t>(state->frameCount),
				phaseCount);
		} else {
			resolutionScale = { 1.0f, 1.0f };

			jitter.x = -state->offsetX * screenSize.x / 2.0f;
			jitter.y = state->offsetY * screenSize.y / 2.0f;
		}

		// Ratios, jitter, and proxies are published after the native dynamic-resolution update.
		const float mipBias = upscalerActive
			? CalculateMipBias(
				  static_cast<float>(renderWidth),
				  screenSize.x,
				  upscaleMethod == UpscaleMethod::kDLSS)
			: 0.0f;
		_mipBias.store(mipBias, std::memory_order_relaxed);
		if (!samplerBias.Update(mipBias)) {
			// Retry until the engine has populated every sampler slot.
			return;
		}
	}

	void Upscaling::PublishDynamicResolution()
	{
		auto* state = cs::engine::GetGraphicsState();
		if (!state) {
			return;
		}

		const auto method = GetUpscaleMethod();
		// Jitter is computed for any vendor method (including Native AA at scale 1.0); proxies only below native.
		const bool vendorMethod = method == UpscaleMethod::kFSR || method == UpscaleMethod::kDLSS;
		const bool scaleActive = vendorMethod && resolutionScale.x < 0.99f;
		const float scale = scaleActive ? resolutionScale.x : 1.0f;

		if (vendorMethod) {
			const auto [renderWidth, renderHeight] = GetRenderSize();
			if (renderWidth > 0 && renderHeight > 0) {
				state->offsetX = -2.0f * jitter.x / static_cast<float>(renderWidth);
				state->offsetY = 2.0f * jitter.y / static_cast<float>(renderHeight);
			}
		}

		// Build render-resolution proxies before the world and HDR imagespace chain use them.
		dynamicResolution.UpdateRenderTargets(scale, scale);

		_savedDynamicWidthRatio = scale;
		_savedDynamicHeightRatio = scale;
		cs::engine::SetDynamicResolution(scale, scale, scale != 1.0f);
		_resolutionScalePublished = true;
	}

	void Upscaling::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		// Resource setup waits until the engine has created its render targets.
		if (!a_device) {
			FailLoad("Upscaling requires a D3D11 device");
		}
	}

	void Upscaling::SetupResources()
	{
		auto* device = cs::engine::GetDevice();
		if (!device) {
			L->error("Renderer device is not ready; resources were not created");
			return;
		}

		D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		depthStencilDesc.StencilEnable = false;

		DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, upscaleDepthStencilState.put()));

		if (!jitterCB) {
			jitterCB = new cs::buffer::ConstantBuffer(cs::buffer::ConstantBufferDesc<JitterCB>());
		}

		if (!upscalingDataCB) {
			upscalingDataCB =
				new cs::buffer::ConstantBuffer(cs::buffer::ConstantBufferDesc<UpscalingDataCB>());
		}

		if (!_frameGenerationCopyCB) {
			_frameGenerationCopyCB =
				new cs::buffer::ConstantBuffer(cs::buffer::ConstantBufferDesc<FrameGenerationCopyCB>());
		}

		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;
		blendDesc.RenderTarget[0].BlendEnable = false;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, upscaleBlendState.put()));

		D3D11_RASTERIZER_DESC rasterizerDesc = {};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.FrontCounterClockwise = false;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0.0f;
		rasterizerDesc.SlopeScaledDepthBias = 0.0f;
		rasterizerDesc.DepthClipEnable = false;
		rasterizerDesc.ScissorEnable = false;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = false;
		DX::ThrowIfFailed(device->CreateRasterizerState(&rasterizerDesc, upscaleRasterizerState.put()));

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearSampler.put()));

		const auto method = GetUpscaleMethod();
		UpdateResolutionScale(cs::engine::GetGraphicsState(), method);
		const bool ok = CheckResources(method);

		rcas.Initialize();

		if (IsFrameGenerationDx12PathActive() && !_copyDepthForFrameGenerationCS) {
			_copyDepthForFrameGenerationCS.attach(
				static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(
					kCopyDepthForFrameGenerationPath,
					{},
					"cs_5_0")));
			if (!_copyDepthForFrameGenerationCS) {
				dx12SwapChain.DisableFrameGeneration("depth capture shader compilation failed");
			}
		}

		// Failed provider setup leaves native dynamic resolution in control.
		_resourcesReady.store(ok, std::memory_order_release);
		if (ok) {
			L->info("Created upscaling resources after render-target creation");
		} else {
			L->error(
				"Upscaling resources were not created after render-target creation; "
				"the game renders natively");
		}
	}

	void Upscaling::InvalidateEngineDerivedResources()
	{
		InvalidateFirstPersonAlphaState();
		dynamicResolution.Release();
		samplerBias.Release();
		_imagespaceRatiosNeutralized = false;
		// Restore native ratios/offsets and clear the latch so a later non-driving frame can't strand a sub-rect.
		RestoreNativeFrameState();
		_resourcesReady.store(false, std::memory_order_release);

		const auto destroy = [](cs::buffer::Texture2D*& a_texture) {
			if (!a_texture) {
				return;
			}
			a_texture->Reset();
			delete a_texture;
			a_texture = nullptr;
		};

		const auto method = GetUpscaleMethod();
		if (method == UpscaleMethod::kDLSS) {
			streamline.DestroyDLSSResources();
		} else if (method == UpscaleMethod::kFSR) {
			fidelityFX.DestroyFSRResources();
		}

		destroy(reactiveMaskTexture);
		destroy(transparencyCompositionMaskTexture);
		destroy(motionVectorCopyTexture);
		destroy(upscalingTexture);
		destroy(sharpenerTexture);
		_upscaledThisFrame = false;
		_srPublishedToFramebuffer.store(false, std::memory_order_release);

		bool recreated = true;
		if (method == UpscaleMethod::kFSR) {
			recreated = fidelityFX.CreateFSRResources();
		}
		recreated = CreateUpscalingTextureResources(method) && recreated;
		_resourcesReady.store(recreated, std::memory_order_release);
		fidelityFX.RequestFrameGenerationReset();
	}

	void Upscaling::PrepareFirstPersonAlphaInputs()
	{
		InvalidateFirstPersonAlphaState();
		if (!_resourcesReady.load(std::memory_order_acquire) ||
			!ShouldUseFrameGenerationThisFrame() ||
			!_copyDepthForFrameGenerationCS ||
			!_frameGenerationCopyCB) {
			return;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* preAlphaColor = cs::engine::GetRenderTargetTexture(kPreAlphaColorTarget);
		auto* preAlphaSRV = cs::engine::GetRenderTargetSRV(kPreAlphaColorTarget);
		auto* postAlphaColor = cs::engine::GetRenderTargetTexture(kSceneColorTarget);
		auto* postAlphaSRV = cs::engine::GetRenderTargetSRV(kSceneColorTarget);
		auto* nativeMotion = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* nativeMotionSRV = cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
		auto* nativeDepth =
			cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		auto* nativeDepthSRV =
			cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMain);
		auto* sharedMotion = dx12SwapChain.GetMotionTexture();
		auto* sharedDepth = dx12SwapChain.GetDepthTexture();
		if (!context ||
			!HaveMatchingCopyContract(preAlphaColor, postAlphaColor) ||
			!ViewReferencesTexture(preAlphaSRV, preAlphaColor) ||
			!ViewReferencesTexture(postAlphaSRV, postAlphaColor) ||
			!ViewReferencesTexture(nativeMotionSRV, nativeMotion) ||
			!ViewReferencesTexture(nativeDepthSRV, nativeDepth) ||
			!sharedMotion ||
			!sharedMotion->texture11 ||
			!sharedMotion->uav11 ||
			!sharedDepth ||
			!sharedDepth->texture11 ||
			!sharedDepth->uav11) {
			return;
		}

		const auto [renderWidth, renderHeight] = GetRenderSize();
		D3D11_TEXTURE2D_DESC colorDesc{};
		D3D11_TEXTURE2D_DESC motionDesc{};
		D3D11_TEXTURE2D_DESC depthDesc{};
		D3D11_TEXTURE2D_DESC sharedMotionDesc{};
		D3D11_TEXTURE2D_DESC sharedDepthDesc{};
		preAlphaColor->GetDesc(&colorDesc);
		nativeMotion->GetDesc(&motionDesc);
		nativeDepth->GetDesc(&depthDesc);
		sharedMotion->texture11->GetDesc(&sharedMotionDesc);
		sharedDepth->texture11->GetDesc(&sharedDepthDesc);
		if (!renderWidth ||
			!renderHeight ||
			renderWidth > colorDesc.Width ||
			renderHeight > colorDesc.Height ||
			renderWidth > motionDesc.Width ||
			renderHeight > motionDesc.Height ||
			renderWidth > depthDesc.Width ||
			renderHeight > depthDesc.Height ||
			sharedMotionDesc.Width != dx12SwapChain.GetWidth() ||
			sharedMotionDesc.Height != dx12SwapChain.GetHeight() ||
			sharedDepthDesc.Width != sharedMotionDesc.Width ||
			sharedDepthDesc.Height != sharedMotionDesc.Height ||
			motionDesc.Format != DXGI_FORMAT_R16G16_FLOAT ||
			sharedMotionDesc.Format != DXGI_FORMAT_R16G16_FLOAT ||
			sharedDepthDesc.Format != DXGI_FORMAT_R32_FLOAT) {
			return;
		}

		cs::engine::CopyResourcePreservingOM(context, preAlphaColor, postAlphaColor);
		_firstPersonAlphaStamp = {
			.stage = FirstPersonAlphaStage::kPrepared,
			.engineFrame = GetEngineFrame(),
			.preAlphaColor = preAlphaColor,
			.postAlphaColor = postAlphaColor,
			.nativeMotion = nativeMotion,
			.nativeDepth = nativeDepth,
			.sharedMotion = sharedMotion->texture11.get(),
			.sharedDepth = sharedDepth->texture11.get()
		};
	}

	void Upscaling::FinishFirstPersonAlphaInputs()
	{
		const auto stamp = _firstPersonAlphaStamp;
		InvalidateFirstPersonAlphaState();
		if (stamp.stage != FirstPersonAlphaStage::kPrepared ||
			stamp.engineFrame != GetEngineFrame() ||
			!ShouldUseFrameGenerationThisFrame()) {
			return;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* preAlphaColor = cs::engine::GetRenderTargetTexture(kPreAlphaColorTarget);
		auto* preAlphaSRV = cs::engine::GetRenderTargetSRV(kPreAlphaColorTarget);
		auto* postAlphaColor = cs::engine::GetRenderTargetTexture(kSceneColorTarget);
		auto* postAlphaSRV = cs::engine::GetRenderTargetSRV(kSceneColorTarget);
		auto* nativeMotion = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* nativeMotionSRV = cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
		auto* nativeDepth =
			cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		auto* nativeDepthSRV =
			cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMain);
		auto* sharedMotion = dx12SwapChain.GetMotionTexture();
		auto* sharedDepth = dx12SwapChain.GetDepthTexture();
		if (!context ||
			preAlphaColor != stamp.preAlphaColor ||
			postAlphaColor != stamp.postAlphaColor ||
			nativeMotion != stamp.nativeMotion ||
			nativeDepth != stamp.nativeDepth ||
			!sharedMotion ||
			sharedMotion->texture11.get() != stamp.sharedMotion ||
			!sharedMotion->uav11 ||
			!sharedDepth ||
			sharedDepth->texture11.get() != stamp.sharedDepth ||
			!sharedDepth->uav11 ||
			!ViewReferencesTexture(preAlphaSRV, preAlphaColor) ||
			!ViewReferencesTexture(postAlphaSRV, postAlphaColor) ||
			!ViewReferencesTexture(nativeMotionSRV, nativeMotion) ||
			!ViewReferencesTexture(nativeDepthSRV, nativeDepth)) {
			return;
		}

		const auto [renderWidth, renderHeight] = GetRenderSize();
		const FrameGenerationCopyCB dimensions{
			renderWidth,
			renderHeight,
			dx12SwapChain.GetWidth(),
			dx12SwapChain.GetHeight(),
			1,
			0,
			0,
			0
		};
		if (!dimensions.renderWidth || !dimensions.renderHeight ||
			!dimensions.outputWidth || !dimensions.outputHeight) {
			return;
		}

		winrt::com_ptr<ID3DUserDefinedAnnotation> annotation;
		context->QueryInterface(IID_PPV_ARGS(annotation.put()));
		if (annotation) {
			annotation->SetMarker(L"FG_CaptureDepthMotion");
		}
		cs::engine::ComputeOMScope scope(context, 4, 0, 2, 1);
		_frameGenerationCopyCB->Update(dimensions);
		ID3D11Buffer* constantBuffer = _frameGenerationCopyCB->CB();
		context->CSSetConstantBuffers(0, 1, &constantBuffer);
		ID3D11ShaderResourceView* srvs[] = {
			nativeDepthSRV,
			nativeMotionSRV,
			preAlphaSRV,
			postAlphaSRV
		};
		context->CSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
		ID3D11UnorderedAccessView* uavs[] = {
			sharedDepth->uav11.get(),
			sharedMotion->uav11.get()
		};
		context->CSSetUnorderedAccessViews(
			0,
			static_cast<UINT>(std::size(uavs)),
			uavs,
			nullptr);
		context->CSSetShader(_copyDepthForFrameGenerationCS.get(), nullptr, 0);
		context->Dispatch(
			(dimensions.outputWidth + 7) / 8,
			(dimensions.outputHeight + 7) / 8,
			1);

		_firstPersonAlphaStamp = stamp;
		_firstPersonAlphaStamp.stage = FirstPersonAlphaStage::kConditioned;
	}

	void Upscaling::BeginFrameGenerationCaptureState() noexcept
	{
		_frameGenerationInputsCaptured = false;
		_hudlessCapturePending = false;
		_frameGenerationAlphaConditioned.store(false, std::memory_order_relaxed);
		dx12SwapChain.SetFrameGenerationInputsReady(false);
	}

	void Upscaling::InvalidateFirstPersonAlphaState() noexcept
	{
		_firstPersonAlphaStamp = {};
	}

	bool Upscaling::ConsumeFirstPersonAlphaInputs(
		std::uint64_t a_engineFrame,
		ID3D11Texture2D* a_nativeMotion,
		ID3D11Texture2D* a_nativeDepth,
		ID3D11Texture2D* a_sharedMotion,
		ID3D11Texture2D* a_sharedDepth) noexcept
	{
		const auto stamp = _firstPersonAlphaStamp;
		InvalidateFirstPersonAlphaState();
		return stamp.stage == FirstPersonAlphaStage::kConditioned &&
			stamp.engineFrame == a_engineFrame &&
			stamp.nativeMotion == a_nativeMotion &&
			stamp.nativeDepth == a_nativeDepth &&
			stamp.sharedMotion == a_sharedMotion &&
			stamp.sharedDepth == a_sharedDepth;
	}

	void Upscaling::CaptureFrameGenerationInputs()
	{
		BeginFrameGenerationCaptureState();
		if (_frameGenerationResetPending.exchange(false, std::memory_order_acq_rel)) {
			InvalidateFirstPersonAlphaState();
			fidelityFX.RequestFrameGenerationReset();
		}
		if (!dx12SwapChain.IsFrameGenerationReady() || !_copyDepthForFrameGenerationCS ||
			!_frameGenerationCopyCB) {
			return;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* motion = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* motionSRV = cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
		auto* depth =
			cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		auto* depthSRV =
			cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMain);
		auto* sharedMotion = dx12SwapChain.GetMotionTexture();
		auto* sharedDepth = dx12SwapChain.GetDepthTexture();
		if (!context || !motion || !motionSRV || !depth || !depthSRV || !sharedMotion || !sharedDepth ||
			!sharedMotion->texture11 || !sharedMotion->uav11 || !sharedDepth->texture11 ||
			!sharedDepth->uav11) {
			return;
		}
		if (!fidelityFX.CacheFrameGenerationCameraData()) {
			static bool loggedCamera = false;
			if (!loggedCamera) {
				loggedCamera = true;
				L->error("Frame-generation world camera data is unavailable");
			}
			return;
		}

		const auto [renderWidth, renderHeight] = GetRenderSize();
		D3D11_TEXTURE2D_DESC sourceMotionDesc{};
		D3D11_TEXTURE2D_DESC targetMotionDesc{};
		motion->GetDesc(&sourceMotionDesc);
		sharedMotion->texture11->GetDesc(&targetMotionDesc);
		if (sourceMotionDesc.Width != targetMotionDesc.Width ||
			sourceMotionDesc.Height != targetMotionDesc.Height ||
			sourceMotionDesc.Format != DXGI_FORMAT_R16G16_FLOAT ||
			targetMotionDesc.Format != DXGI_FORMAT_R16G16_FLOAT) {
			dx12SwapChain.DisableFrameGeneration("RT29 motion-vector contract is incompatible");
			return;
		}

		const bool alphaConditioned = ConsumeFirstPersonAlphaInputs(
			GetEngineFrame(),
			motion,
			depth,
			sharedMotion->texture11.get(),
			sharedDepth->texture11.get());
		if (!alphaConditioned) {
			winrt::com_ptr<ID3DUserDefinedAnnotation> annotation;
			context->QueryInterface(IID_PPV_ARGS(annotation.put()));
			if (annotation) {
				annotation->SetMarker(L"FG_CaptureDepthMotion");
			}
			cs::engine::ComputeOMScope scope(context, 4, 0, 2, 1);
			const FrameGenerationCopyCB dimensions{
				renderWidth,
				renderHeight,
				dx12SwapChain.GetWidth(),
				dx12SwapChain.GetHeight(),
				0,
				0,
				0,
				0
			};
			_frameGenerationCopyCB->Update(dimensions);
			ID3D11Buffer* constantBuffer = _frameGenerationCopyCB->CB();
			context->CSSetConstantBuffers(0, 1, &constantBuffer);
			ID3D11ShaderResourceView* srvs[] = { depthSRV, motionSRV, nullptr, nullptr };
			context->CSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
			ID3D11UnorderedAccessView* uavs[] = {
				sharedDepth->uav11.get(),
				sharedMotion->uav11.get()
			};
			context->CSSetUnorderedAccessViews(
				0,
				static_cast<UINT>(std::size(uavs)),
				uavs,
				nullptr);
			context->CSSetShader(_copyDepthForFrameGenerationCS.get(), nullptr, 0);
			context->Dispatch(
				(dx12SwapChain.GetWidth() + 7) / 8,
				(dx12SwapChain.GetHeight() + 7) / 8,
				1);
			_frameGenerationRawCaptures.fetch_add(1, std::memory_order_relaxed);
		} else {
			_frameGenerationAlphaConditionedCaptures.fetch_add(1, std::memory_order_relaxed);
		}
		_frameGenerationAlphaConditioned.store(alphaConditioned, std::memory_order_relaxed);

		_frameGenerationInputsCaptured = true;
		_hudlessCapturePending = true;
	}

	void Upscaling::CaptureHUDLessColor()
	{
		if (!_hudlessCapturePending || !_frameGenerationInputsCaptured ||
			!dx12SwapChain.IsFrameGenerationReady()) {
			return;
		}
		_hudlessCapturePending = false;

		auto* context = cs::engine::GetImmediateContext();
		auto* frameBufferRTV =
			cs::engine::GetRenderTargetRTV(cs::engine::RenderTarget::kFrameBuffer);
		auto* hudless = dx12SwapChain.GetHudlessTexture();
		if (!context || !frameBufferRTV || !hudless || !hudless->texture11) {
			dx12SwapChain.SetFrameGenerationInputsReady(false);
			return;
		}

		winrt::com_ptr<ID3D11Resource> frameBufferResource;
		frameBufferRTV->GetResource(frameBufferResource.put());
		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		if (!frameBufferResource ||
			FAILED(frameBufferResource->QueryInterface(IID_PPV_ARGS(frameBuffer.put())))) {
			dx12SwapChain.SetFrameGenerationInputsReady(false);
			return;
		}

		D3D11_TEXTURE2D_DESC sourceDesc{};
		D3D11_TEXTURE2D_DESC targetDesc{};
		frameBuffer->GetDesc(&sourceDesc);
		hudless->texture11->GetDesc(&targetDesc);
		if (sourceDesc.Width != targetDesc.Width || sourceDesc.Height != targetDesc.Height ||
			sourceDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
			targetDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
			dx12SwapChain.DisableFrameGeneration("pre-UI RT0 is incompatible with SDR HUDLessColor");
			return;
		}

		winrt::com_ptr<ID3DUserDefinedAnnotation> annotation;
		context->QueryInterface(IID_PPV_ARGS(annotation.put()));
		if (annotation) {
			annotation->SetMarker(L"FG_CaptureHUDLess_RT0_PostImagespace");
		}
		cs::engine::CopyResourcePreservingOM(
			context,
			hudless->texture11.get(),
			frameBuffer.get());
		dx12SwapChain.SetFrameGenerationInputsReady(true);
		_frameGenerationDispatches.fetch_add(1, std::memory_order_relaxed);
	}

	void Upscaling::ClearFrameGenerationCaptureState() noexcept
	{
		_frameGenerationInputsCaptured = false;
		_hudlessCapturePending = false;
		dx12SwapChain.SetFrameGenerationInputsReady(false);
		InvalidateFirstPersonAlphaState();
	}

	void Upscaling::RecordFrameGenerationFailure() noexcept
	{
		_frameGenerationFailures.fetch_add(1, std::memory_order_relaxed);
	}

	bool Upscaling::IsDrivingFrameState() const noexcept
	{
		return _hooksInstalled.load(std::memory_order_acquire)
			&& _resourcesReady.load(std::memory_order_acquire)
			&& !_quarantined.load(std::memory_order_acquire)
			&& IsHealthy();
	}

	void Upscaling::RestoreNativeFrameState()
	{
		if (auto* state = cs::engine::GetGraphicsState()) {
			state->offsetX = 0.0f;
			state->offsetY = 0.0f;
		}
		cs::engine::SetDynamicResolutionRatios(1.0f, 1.0f);
		resolutionScale = { 1.0f, 1.0f };
		_resolutionScalePublished = false;
		_mipBias.store(0.0f, std::memory_order_relaxed);
		ForceViewportToRenderTargetDimensions();
	}

	void Upscaling::RestoreNativeFrameStateOnce()
	{
		// Restore only once before returning ratio ownership to the engine.
		if (!_resolutionScalePublished) {
			return;
		}
		RestoreNativeFrameState();
		L->warn("Upscaling stopped driving frame state; native resolution and viewport restored");
	}

	void Upscaling::QuarantineAfterException(const char* a_where) noexcept
	{
		const bool alreadyQuarantined =
			_quarantined.exchange(true, std::memory_order_acq_rel);
		_resourcesReady.store(false, std::memory_order_release);
		_upscaledThisFrame = false;
		ClearFrameGenerationCaptureState();

		try {
			RestoreNativeFrameState();
		} catch (...) {
		}

		// Restore engine render-target and depth pointers before abandoning the proxies.
		try {
			dynamicResolution.Release();
		} catch (...) {
		}
		try {
			samplerBias.Release();
		} catch (...) {
		}
		_imagespaceRatiosNeutralized = false;
		if (!alreadyQuarantined) {
			try {
				L->critical(
					"Upscaling quarantined after an exception in {}; native resolution restored "
					"and the engine has its dynamic-resolution driver back for this session",
					a_where);
			} catch (...) {
			}
		}
	}

	void Upscaling::BSShaderRenderTargets_Create::thunk(void* a_this)
	{
		func(a_this);

		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling resource setup", [&] {
			if (upscaling->_resourcesReady.load(std::memory_order_acquire)) {
				upscaling->InvalidateEngineDerivedResources();
				return;
			}
			upscaling->SetupResources();
		});
	}

	bool Upscaling::ImageSpaceEffectTemporalAA_IsActive::thunk(
		RE::ImageSpaceEffectTemporalAA* a_this)
	{
		auto* upscaling = GetSingleton();
		const auto method = upscaling->GetUpscaleMethod();
		if (upscaling->IsDrivingFrameState() &&
			upscaling->_srPublishedToFramebuffer.load(std::memory_order_acquire) &&
			(method == UpscaleMethod::kFSR || method == UpscaleMethod::kDLSS)) {
			return false;
		}
		return func(a_this);
	}

	void Upscaling::DrawWorldBegin_SetDynamicViewport::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		bool a_enabled)
	{
		auto* upscaling = GetSingleton();
		const auto method = upscaling->GetUpscaleMethod();
		const bool vendorActive =
			upscaling->IsDrivingFrameState() &&
			method != UpscaleMethod::kNONE &&
			method != UpscaleMethod::kTAA;
		func(a_this, vendorActive ? true : a_enabled);
	}

	void Upscaling::Main_UpdateDynamicResolution::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		RE::NiPoint3* a_2,
		RE::NiPoint3* a_3,
		RE::NiPoint3* a_4,
		RE::NiPoint3* a_5)
	{
		auto* upscaling = GetSingleton();

		func(a_this, a_2, a_3, a_4, a_5);

		GuardedThunkBody("Upscaling dynamic-resolution publish", [&] {
			if (!upscaling->IsDrivingFrameState()) {
				return;
			}
			upscaling->PublishDynamicResolution();
		});
	}

	bool Upscaling::Upscale()
	{
		auto upscaleMethod = GetUpscaleMethod();

		auto* context = cs::engine::GetImmediateContext();
		auto* state = cs::engine::GetGraphicsState();
		if (!context || !state || !upscalingTexture || !reactiveMaskTexture ||
			!transparencyCompositionMaskTexture) {
			return false;
		}

		auto* motionVectorTexture = cs::engine::GetRenderTargetTexture(kMotionVectorTarget);
		auto* motionVectorSRV = cs::engine::GetRenderTargetSRV(kMotionVectorTarget);
		if (!motionVectorTexture || !motionVectorSRV ||
			(upscaleMethod == UpscaleMethod::kDLSS && !motionVectorCopyTexture)) {
			return false;
		}

		bool upscaled = false;
		const auto [renderWidth, renderHeight] = GetRenderSize();
		if (renderWidth == 0 || renderHeight == 0) {
			return false;
		}

		{
			// Keep OM unbound until provider reads complete.
			cs::engine::OMScope omScope(context);

			{
				cs::ComputeScope computeScope(context);

				// Null t0 and RT20's unused channel produce the accepted zero masks.
				auto* normalsSRV = cs::engine::GetRenderTargetSRV(kNormalsTarget);
				auto* depthSRV = cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMain);
				auto* encodeShader = GetEncodeTexturesCS();
				if (!depthSRV || !encodeShader) {
					return false;
				}

				ID3D11ShaderResourceView* views[4] = { nullptr, normalsSRV, motionVectorSRV, depthSRV };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);
				context->CSSetShader(encodeShader, nullptr, 0);

				UpscalingDataCB upscalingData;
				upscalingData.trueSamplingDim = float2((float)renderWidth, (float)renderHeight);
				upscalingData.pad0 = { 0.0f, 0.0f };
				upscalingDataCB->Update(upscalingData);
				auto upscalingBuffer = upscalingDataCB->CB();
				context->CSSetConstantBuffers(0, 1, &upscalingBuffer);

				ID3D11UnorderedAccessView* uavs[4] = {
					reactiveMaskTexture->uav.get(),
					transparencyCompositionMaskTexture->uav.get(),
					(upscaleMethod == UpscaleMethod::kDLSS) ? motionVectorCopyTexture->uav.get() : nullptr,
					nullptr
				};
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

				context->Dispatch((renderWidth + 7) / 8, (renderHeight + 7) / 8, 1);

				ID3D11UnorderedAccessView* nullUAVs[4] = { nullptr, nullptr, nullptr, nullptr };
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

				ID3D11Buffer* nullBuffer = nullptr;
				context->CSSetConstantBuffers(0, 1, &nullBuffer);
			}

			if (upscaleMethod == UpscaleMethod::kDLSS) {
				upscaled = streamline.Upscale(
					upscalingTexture->resource.get(),
					reactiveMaskTexture->resource.get(),
					transparencyCompositionMaskTexture->resource.get(),
					motionVectorCopyTexture->resource.get());
			} else if (upscaleMethod == UpscaleMethod::kFSR) {
				upscaled = fidelityFX.Upscale(
					upscalingTexture->resource.get(),
					reactiveMaskTexture->resource.get(),
					transparencyCompositionMaskTexture->resource.get(),
					motionVectorTexture,
					settings.sharpnessFSR);
			}
		}

		if (upscaled) {
			_upscaleDispatches.fetch_add(1, std::memory_order_relaxed);
		} else {
			_providerFailures.fetch_add(1, std::memory_order_relaxed);
		}
		_upscaledThisFrame = upscaled;
		return upscaled;
	}

	bool Upscaling::PerformUpscaling()
	{
		_upscaledThisFrame = false;
		// Keep the last completed resolve result stable across vfunc queries within the frame.
		const auto finish = [this](bool a_published) {
			_upscaledThisFrame = a_published;
			_srPublishedToFramebuffer.store(a_published, std::memory_order_release);
			return a_published;
		};

		auto* context = cs::engine::GetImmediateContext();
		winrt::com_ptr<ID3D11Texture2D> frameBuffer;
		D3D11_TEXTURE2D_DESC frameBufferDesc{};
		if (!context || !upscalingTexture ||
			!TryGetFrameBufferTexture(frameBuffer, frameBufferDesc) ||
			!MatchesTextureContract(
				upscalingTexture,
				frameBufferDesc,
				DXGI_FORMAT_R8G8B8A8_UNORM)) {
			return finish(false);
		}

		cs::engine::CopyResourcePreservingOM(
			context,
			upscalingTexture->resource.get(),
			frameBuffer.get());

		if (!Upscale()) {
			return finish(false);
		}

		UpscaleDepth();

		const auto method = GetUpscaleMethod();
		bool published = false;
		if (method == UpscaleMethod::kFSR) {
			published = PublishUpscalingOutput(
				context,
				frameBuffer.get(),
				upscalingTexture->resource.get(),
				_upscaledThisFrame);
		} else if (method == UpscaleMethod::kDLSS) {
			published = ApplySharpening(frameBuffer.get());
		}

		return finish(published);
	}

	void Upscaling::UpscaleDepth()
	{
		if (!_upscaledThisFrame || !IsUpscalingActive()) {
			return;
		}

		auto* context = cs::engine::GetImmediateContext();
		auto* state = cs::engine::GetGraphicsState();
		if (!context || !state || !linearSampler || !jitterCB || !upscaleRasterizerState ||
			!upscaleBlendState || !upscaleDepthStencilState) {
			return;
		}

		float2 screenSize{ (float)state->screenWidth, (float)state->screenHeight };
		if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
			return;
		}

		auto* depthTexture = cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMain);
		auto* depthDSV = cs::engine::GetDepthStencilDSV(cs::engine::DepthStencilTarget::kMain);
		auto* depthCopyTexture = cs::engine::GetDepthStencilTexture(cs::engine::DepthStencilTarget::kMainCopy);
		auto* depthCopySRV = cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMainCopy);
		auto* depthCopyStencilSRV = cs::engine::GetDepthStencilStencilSRV(cs::engine::DepthStencilTarget::kMainCopy);
		auto* refractionNormalsTexture = cs::engine::GetRenderTargetTexture(kRefractionNormalTarget);
		auto* refractionNormalsCopy = cs::engine::GetRenderTargetCopyTexture(kRefractionNormalTarget);
		auto* refractionNormalsCopySRV = cs::engine::GetRenderTargetCopySRV(kRefractionNormalTarget);
		auto* refractionNormalsRTV = cs::engine::GetRenderTargetRTV(kRefractionNormalTarget);

		if (!depthTexture || !depthDSV || !depthCopyTexture || !depthCopySRV ||
			!refractionNormalsTexture || !refractionNormalsCopy || !refractionNormalsCopySRV || !refractionNormalsRTV) {
			return;
		}

		auto* fullscreenVS = GetUpscaleVS();
		auto* depthUpscalePS = GetDepthRefractionUpscalePS();
		if (!fullscreenVS || !depthUpscalePS) {
			return;
		}

		// Restore the engine's exact OM bindings after the depth pass.
		cs::engine::OMScope omScope(context);
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		context->VSSetShader(fullscreenVS, nullptr, 0);

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = screenSize.x;
		viewport.Height = screenSize.y;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		context->RSSetState(upscaleRasterizerState.get());
		context->OMSetBlendState(upscaleBlendState.get(), nullptr, 0xffffffff);

		ID3D11SamplerState* samplers[] = { linearSampler.get() };
		context->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);

		JitterCB jitterData{};
		jitterData.jitter = jitter;

		jitterCB->Update(jitterData);
		auto bufferArray = jitterCB->CB();
		context->PSSetConstantBuffers(0, 1, &bufferArray);

		const auto copyIfNonAliased = [&](ID3D11Resource* dst, ID3D11Resource* src) {
			if (dst && src && dst != src) {
				context->CopyResource(dst, src);
			}
		};

		{
			copyIfNonAliased(depthCopyTexture, depthTexture);

			context->OMSetDepthStencilState(upscaleDepthStencilState.get(), 0x00);

			copyIfNonAliased(refractionNormalsCopy, refractionNormalsTexture);

			ID3D11ShaderResourceView* srvs[] = { refractionNormalsCopySRV, depthCopySRV, depthCopyStencilSRV };
			context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

			// Fallout 4's SAO derives camera-Z without a second output.
			ID3D11RenderTargetView* rtvs[] = { refractionNormalsRTV };
			context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, depthDSV);

			context->PSSetShader(depthUpscalePS, nullptr, 0);
			context->Draw(3, 0);
		}

		ID3D11ShaderResourceView* nullPSResources[3] = { nullptr, nullptr, nullptr };
		context->PSSetShaderResources(0, ARRAYSIZE(nullPSResources), nullPSResources);

		context->PSSetShader(nullptr, nullptr, 0);
		context->VSSetShader(nullptr, nullptr, 0);
	}

	bool Upscaling::ApplySharpening(ID3D11Texture2D* a_frameBuffer)
	{
		if (!a_frameBuffer || !upscalingTexture || !sharpenerTexture || !_upscaledThisFrame) {
			return false;
		}

		auto* context = cs::engine::GetImmediateContext();
		if (!context) {
			return false;
		}

		cs::engine::ComputeOMScope scope(context, 1, 0, 1, 1);
		if (settings.sharpnessEnabledDLSS && settings.sharpnessDLSS > 0.0f) {
			// Match FSR3's slider-to-RCAS attenuation.
			float currentSharpness = (-2.0f * settings.sharpnessDLSS) + 2.0f;
			currentSharpness = exp2(-currentSharpness);

			if (!rcas.ApplySharpen(
					sharpenerTexture->srv.get(),
					upscalingTexture->uav.get(),
					currentSharpness)) {
				return false;
			}
			return PublishUpscalingOutput(
				context,
				a_frameBuffer,
				upscalingTexture->resource.get(),
				true);
		}

		return PublishUpscalingOutput(
			context,
			a_frameBuffer,
			sharpenerTexture->resource.get(),
			true);
	}

	void Upscaling::Main_UpdateJitter::thunk(RE::BSGraphics::State* a_state)
	{
		auto* upscaling = GetSingleton();

		GuardedThunkBody("Upscaling frame-start", [&] {
			if (!upscaling->IsDrivingFrameState()) {
				upscaling->RestoreNativeFrameStateOnce();
				return;
			}
			upscaling->ConfigureTAA();
		});

		func(a_state);

		GuardedThunkBody("Upscaling ConfigureUpscaling", [&] {
			if (!upscaling->IsDrivingFrameState()) {
				return;
			}
			upscaling->ConfigureUpscaling();
		});
	}

	void Upscaling::Main_PostProcessing::thunk()
	{
		auto* upscaling = GetSingleton();
		upscaling->_imagespaceScope = true;
		const REX::TScopeExit clearScope{ [upscaling]() noexcept {
			upscaling->_imagespaceScope = false;
		} };

		// Scope resolve to the world-and-UI caller, excluding pause-only UI.
		func();

		GuardedThunkBody("Upscaling post-resolve temporal reset", [&] {
			SetTemporalEnabled(false);
		});
	}

	void Upscaling::DrawWorld_FirstPersonAlpha::thunk(RE::BSShaderAccumulator* a_accumulator)
	{
		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling first-person alpha pre-stage", [upscaling] {
			upscaling->PrepareFirstPersonAlphaInputs();
		});

		func(a_accumulator);

		GuardedThunkBody("Upscaling first-person alpha post-stage", [upscaling] {
			upscaling->FinishFirstPersonAlphaInputs();
		});
	}

	void Upscaling::DrawWorldImagespace_Upscale::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		bool a_enabled)
	{
		func(a_this, a_enabled);

		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling resolve", [&] {
			if (!upscaling->_imagespaceScope) {
				return;
			}
			if (upscaling->IsFrameGenerationDx12PathActive()) {
				upscaling->CaptureFrameGenerationInputs();
			}
			if (!upscaling->IsDrivingFrameState()) {
				return;
			}

			const auto upscaleMethod = upscaling->GetUpscaleMethod();
			bool upscaled = true;
			if (upscaleMethod != UpscaleMethod::kNONE && upscaleMethod != UpscaleMethod::kTAA)
				upscaled = upscaling->PerformUpscaling();

			// Jitter must not leak into UI regardless of whether the resolve succeeded.
			if (auto* state = cs::engine::GetGraphicsState()) {
				state->offsetX = 0.0f;
				state->offsetY = 0.0f;
			}

			if (upscaled) {
				// Save the render scale, then neutralize ratios so UI and post-processing run full-extent.
				if (auto* renderTargetManager = cs::engine::GetRenderTargetManager()) {
					upscaling->_savedDynamicWidthRatio = renderTargetManager->GetDynamicWidthRatio();
					upscaling->_savedDynamicHeightRatio = renderTargetManager->GetDynamicHeightRatio();
				}
				cs::engine::SetDynamicResolution(1.0f, 1.0f, false);
				upscaling->_imagespaceRatiosNeutralized = true;
			}
			// On failure, leave the real ratio and activated flag so native DR resolves the sub-rect as in vanilla.

			upscaling->CaptureHUDLessColor();

			SetTemporalEnabled(upscaleMethod == UpscaleMethod::kTAA);
		});
	}

	void Upscaling::DrawWorldImagespace_RenderEffectRange::thunk(
		RE::BSGraphics::RenderTargetManager* a_this,
		std::uint32_t a_first,
		std::uint32_t a_last,
		std::uint32_t a_4,
		std::uint32_t a_5)
	{
		auto* upscaling = GetSingleton();

		float widthRatio = 1.0f;
		float heightRatio = 1.0f;
		bool doSplit = false;
		if (upscaling->IsDrivingFrameState() && upscaling->dynamicResolution.HasProxies()) {
			if (auto* renderTargetManager = cs::engine::GetRenderTargetManager()) {
				widthRatio = renderTargetManager->GetDynamicWidthRatio();
				heightRatio = renderTargetManager->GetDynamicHeightRatio();
				doSplit = widthRatio != 1.0f || heightRatio != 1.0f;
			}
		}

		if (!doSplit) {
			func(a_this, a_first, a_last, a_4, a_5);
			return;
		}

		auto* state = cs::engine::GetGraphicsState();
		const float savedOffsetX = state ? state->offsetX : 0.0f;
		const float savedOffsetY = state ? state->offsetY : 0.0f;

		try {
			// HDR effects render against the render-resolution proxies.
			func(a_this, 0, 3, 1, 1);
			upscaling->dynamicResolution.OverrideRenderTargets({ 1, 4, 29, 16 });
			upscaling->dynamicResolution.OverrideDepth(true);
			cs::engine::SetDynamicResolution(1.0f, 1.0f, false);

			// LDR effects render full-extent.
			func(a_this, 4, 13, 1, 1);
			upscaling->dynamicResolution.ResetDepth();
			upscaling->dynamicResolution.ResetRenderTargets({ 4 });

			cs::engine::SetDynamicResolution(widthRatio, heightRatio, true);
		} catch (...) {
			upscaling->dynamicResolution.ResetDepth();
			upscaling->dynamicResolution.ResetRenderTargets({ 4 });
			cs::engine::SetDynamicResolution(widthRatio, heightRatio, true);
			upscaling->QuarantineAfterException("Upscaling imagespace effect split");
		}

		if (state) {
			state->offsetX = savedOffsetX;
			state->offsetY = savedOffsetY;
		}
	}

	void Upscaling::DrawWorldImagespace_RestoreRatios::thunk(void* a_this)
	{
		func(a_this);

		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling imagespace ratio restore", [&] {
			if (!upscaling->_imagespaceRatiosNeutralized) {
				return;
			}
			upscaling->_imagespaceRatiosNeutralized = false;
			const bool activated = upscaling->_savedDynamicWidthRatio != 1.0f ||
				upscaling->_savedDynamicHeightRatio != 1.0f;
			cs::engine::SetDynamicResolution(
				upscaling->_savedDynamicWidthRatio,
				upscaling->_savedDynamicHeightRatio,
				activated);
		});
	}

	void Upscaling::DeferredComposite_RenderPass::thunk(void* a_pass, std::uint32_t a_2, bool a_3)
	{
		auto* upscaling = GetSingleton();

		float widthRatio = 1.0f;
		float heightRatio = 1.0f;
		bool overrideActive = false;
		if (upscaling->IsDrivingFrameState() && upscaling->dynamicResolution.HasProxies()) {
			if (auto* renderTargetManager = cs::engine::GetRenderTargetManager()) {
				widthRatio = renderTargetManager->GetDynamicWidthRatio();
				heightRatio = renderTargetManager->GetDynamicHeightRatio();
				overrideActive = widthRatio != 1.0f || heightRatio != 1.0f;
			}
		}

		if (overrideActive) {
			GuardedThunkBody("Upscaling composite override", [&] {
				upscaling->dynamicResolution.OverrideRenderTargets(
					{ 20, 25, 57, 24, 23, 58, 59, 3, 9, 60, 61, 28 });
				upscaling->dynamicResolution.OverrideDepth(true);
				cs::engine::SetDynamicResolution(1.0f, 1.0f, false);
			});
		}

		func(a_pass, a_2, a_3);

		if (overrideActive) {
			GuardedThunkBody("Upscaling composite reset", [&] {
				upscaling->dynamicResolution.ResetRenderTargets({ 4 });
				upscaling->dynamicResolution.ResetDepth();
				if (upscaling->dynamicResolution.HasProxies()) {
					cs::engine::SetDynamicResolution(widthRatio, heightRatio, true);
				}
			});
		}
	}

	void Upscaling::LensFlare_RenderLensFlare::thunk(RE::NiCamera* a_camera)
	{
		auto* upscaling = GetSingleton();

		bool overrideActive = false;
		if (upscaling->IsDrivingFrameState() && upscaling->dynamicResolution.HasProxies()) {
			if (auto* renderTargetManager = cs::engine::GetRenderTargetManager()) {
				overrideActive = renderTargetManager->GetDynamicWidthRatio() != 1.0f ||
					renderTargetManager->GetDynamicHeightRatio() != 1.0f;
			}
		}

		if (overrideActive) {
			GuardedThunkBody("Upscaling lens-flare depth override", [&] {
				upscaling->dynamicResolution.OverrideDepth(true);
			});
		}

		func(a_camera);

		if (overrideActive) {
			GuardedThunkBody("Upscaling lens-flare depth reset", [&] {
				upscaling->dynamicResolution.ResetDepth();
			});
		}
	}

	void Upscaling::SSLRRaytracing_BeginTechnique::thunk(
		void* a_shader,
		std::uint32_t a_2,
		std::uint32_t a_3,
		std::uint32_t a_4,
		std::uint32_t a_5)
	{
		func(a_shader, a_2, a_3, a_4, a_5);

		auto* upscaling = GetSingleton();
		GuardedThunkBody("Upscaling SSLR shader patch", [&] {
			if (upscaling->IsDrivingFrameState() && upscaling->IsUpscalingActive()) {
				upscaling->PatchSSRShader();
			}
		});
	}

	void Upscaling::Vats_SetPixelConstant::thunk(
		void* a_param,
		int a_row,
		float a_x,
		float a_y,
		float a_z,
		float a_w)
	{
		auto* upscaling = GetSingleton();
		if (upscaling->IsDrivingFrameState() && upscaling->IsUpscalingActive()) {
			func(
				a_param,
				a_row,
				a_x * upscaling->_savedDynamicHeightRatio,
				a_y * upscaling->_savedDynamicWidthRatio,
				a_z,
				a_w);
			return;
		}
		func(a_param, a_row, a_x, a_y, a_z, a_w);
	}

	void Upscaling::LoadingMenu_UpdateTemporalData::thunk(RE::BSGraphics::State* a_state)
	{
		func(a_state);

		GuardedThunkBody("Upscaling loading-menu reset", [] {
			cs::engine::SetDynamicResolution(1.0f, 1.0f, false);
		});
	}

	void Upscaling::RenderPreUI_DeferredPrePass::thunk(void* a_this)
	{
		auto* upscaling = GetSingleton();
		if (!upscaling->IsDrivingFrameState()) {
			func(a_this);
			return;
		}
		try {
			upscaling->samplerBias.Override();
			func(a_this);
			upscaling->samplerBias.Reset();
		} catch (...) {
			upscaling->samplerBias.Reset();
			upscaling->QuarantineAfterException("Upscaling sampler override (deferred prepass)");
		}
	}

	void Upscaling::RenderPreUI_Forward::thunk(void* a_this)
	{
		auto* upscaling = GetSingleton();
		if (!upscaling->IsDrivingFrameState()) {
			func(a_this);
			return;
		}
		try {
			upscaling->samplerBias.Override();
			func(a_this);
			upscaling->samplerBias.Reset();
		} catch (...) {
			upscaling->samplerBias.Reset();
			upscaling->QuarantineAfterException("Upscaling sampler override (forward)");
		}
	}

	void Upscaling::BSImageSpace_Init_FXAA::thunk(RE::ImageSpaceManager* a_this)
	{
		func(a_this);

		GuardedThunkBody("Upscaling FXAA disable", [] {
			auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
			if (!imageSpaceManager) {
				return;
			}
			constexpr auto fxaa = static_cast<std::uint32_t>(
				RE::ImageSpaceManager::ImageSpaceEffectEnum::EFFECT_SHADER_FXAA);
			if (fxaa < imageSpaceManager->effectList.size()) {
				if (auto* effect = imageSpaceManager->effectList[fxaa]) {
					effect->isActive = false;
				}
			}
		});
	}

	void Upscaling::Renderer_ResetWindow::thunk(RE::BSGraphics::Renderer* a_this, std::uint32_t a_arg)
	{
		func(a_this, a_arg);

		GuardedThunkBody("Upscaling reset-window invalidation", [] {
			GetSingleton()->InvalidateEngineDerivedResources();
		});
	}

	RE::BSEventNotifyControl Upscaling::MenuOpenCloseEventHandler::ProcessEvent(
		const RE::MenuOpenCloseEvent& a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (a_event.menuName == RE::LoadingMenu::MENU_NAME && !a_event.opening) {
			auto* upscaling = GetSingleton();
			upscaling->_frameGenerationResetPending.store(true, std::memory_order_release);
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	bool Upscaling::MenuOpenCloseEventHandler::Register()
	{
		static MenuOpenCloseEventHandler handler;
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		ui->RegisterSink<RE::MenuOpenCloseEvent>(&handler);
		return true;
	}

	cs::settings::RestartSettingsView Upscaling::GetRestartSettings() const noexcept
	{
		static constexpr std::array fields{
			CS_RESTART_ENABLE_FIELD(
				Settings,
				frameGenerationMode,
				"FSR 3 frame generation"),
			CS_RESTART_FIELD(
				Settings,
				frameGenerationForceEnable,
				"Force frame generation below 120 Hz"),
			CS_RESTART_FIELD(
				Settings,
				streamlineLogLevel,
				"Streamline log level")
		};
		return cs::settings::MakeRestartSettingsView(fields, _bootSettings, settings);
	}

	void Upscaling::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("enabled", settings.enabled)
			.Field("resources_ready", _resourcesReady.load(std::memory_order_acquire))
			.Field("hooks_installed", _hooksInstalled.load(std::memory_order_acquire))
			.Field("method", static_cast<std::int64_t>(static_cast<int>(GetUpscaleMethod())))
			.Field("dlss_available", streamline.featureDLSS)
			.Field("upscaling_active", IsUpscalingActive())
			.Field("fg_proxy_active", IsFrameGenerationDx12PathActive())
			.Field("fg_ready", dx12SwapChain.IsFrameGenerationReady())
			.Field("fg_active", IsFrameGenerationActive())
			.Field("fg_inputs_captured", _frameGenerationInputsCaptured)
			.Field("fg_hudless_pending", _hudlessCapturePending)
			.Field("fg_last_alpha_conditioned", _frameGenerationAlphaConditioned.load(std::memory_order_relaxed))
			.Field("fg_alpha_conditioned_captures", static_cast<std::int64_t>(_frameGenerationAlphaConditionedCaptures.load(std::memory_order_relaxed)))
			.Field("fg_raw_captures", static_cast<std::int64_t>(_frameGenerationRawCaptures.load(std::memory_order_relaxed)))
			.Field("fg_dispatches", static_cast<std::int64_t>(_frameGenerationDispatches.load(std::memory_order_relaxed)))
			.Field("fg_failures", static_cast<std::int64_t>(_frameGenerationFailures.load(std::memory_order_relaxed)))
			.Field("scale_published", _resolutionScalePublished)
			.Field("provider_failures", static_cast<std::int64_t>(_providerFailures.load(std::memory_order_relaxed)))
			.Field("sr_published_to_framebuffer", _srPublishedToFramebuffer.load(std::memory_order_acquire))
			.Field("resolution_scale_x", static_cast<double>(resolutionScale.x))
			.Field("resolution_scale_y", static_cast<double>(resolutionScale.y))
			.Field("mip_bias", static_cast<double>(_mipBias.load(std::memory_order_relaxed)))
			.Field("dispatches", static_cast<std::int64_t>(_upscaleDispatches.load(std::memory_order_relaxed)));
	}

	void Upscaling::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &settings.enabled);

		static const char* methods[] = { "None", "TAA", "FSR 3", "DLSS" };
		int method = static_cast<int>(streamline.featureDLSS ? settings.upscaleMethod : settings.upscaleMethodNoDLSS);
		if (ImGui::Combo("Upscaler", &method, methods, IM_ARRAYSIZE(methods))) {
			if (streamline.featureDLSS) {
				settings.upscaleMethod = static_cast<std::uint32_t>(method);
			} else {
				settings.upscaleMethodNoDLSS = static_cast<std::uint32_t>(std::min(method, 2));
			}
			changed = true;
		}
		if (!streamline.featureDLSS) {
			ImGui::TextDisabled("DLSS is unavailable on this adapter; the no-DLSS selection is used.");
		}

		static const char* qualityModes[] = { "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" };
		int qualityMode = static_cast<int>(settings.qualityMode);
		if (ImGui::Combo("Quality mode", &qualityMode, qualityModes, IM_ARRAYSIZE(qualityModes))) {
			settings.qualityMode = static_cast<std::uint32_t>(qualityMode);
			changed = true;
		}

		changed |= ImGui::SliderFloat("FSR sharpness", &settings.sharpnessFSR, 0.0f, 1.0f);
		changed |= ImGui::Checkbox("DLSS sharpening", &settings.sharpnessEnabledDLSS);
		changed |= ImGui::SliderFloat("DLSS sharpness", &settings.sharpnessDLSS, 0.0f, 1.0f);

		bool frameGenerationEnabled = settings.frameGenerationMode != 0;
		if (ImGui::Checkbox("FSR 3 frame generation", &frameGenerationEnabled)) {
			settings.frameGenerationMode = frameGenerationEnabled ? 1u : 0u;
			changed = true;
		}
		bool forceFrameGeneration = settings.frameGenerationForceEnable != 0;
		if (ImGui::Checkbox("Force frame generation below 120 Hz", &forceFrameGeneration)) {
			settings.frameGenerationForceEnable = forceFrameGeneration ? 1u : 0u;
			changed = true;
		}
		changed |= ImGui::Checkbox(
			"Allow frame generation in menus",
			&settings.frameGenerationAllowInMenus);
		ImGui::TextDisabled("Frame generation requires windowed or borderless SDR output.");

		static const char* presets[] = { "Default", "J", "K", "L", "M" };
		int preset = static_cast<int>(settings.presetDLSS);
		if (ImGui::Combo("DLSS preset", &preset, presets, IM_ARRAYSIZE(presets))) {
			settings.presetDLSS = static_cast<std::uint32_t>(preset);
			changed = true;
		}

		static const char* logLevels[] = { "Off", "Default", "Verbose" };
		int logLevel = static_cast<int>(settings.streamlineLogLevel);
		if (ImGui::Combo("Streamline log level", &logLevel, logLevels, IM_ARRAYSIZE(logLevels))) {
			settings.streamlineLogLevel = static_cast<std::uint32_t>(logLevel);
			changed = true;
		}
		if (changed) {
			SaveSettings();
		}

		ImGui::TextDisabled(
			"Render scale: %.0f%% | resources: %s",
			static_cast<double>(resolutionScale.x) * 100.0,
			_resourcesReady.load(std::memory_order_acquire) ? "ready" : "not ready");
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(Upscaling::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
