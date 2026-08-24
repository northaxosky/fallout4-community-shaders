#include "FrameGeneration.h"

#include <d3dcompiler.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>

#include "DX12SwapChain.h"
#include "Utils/CSUtil.h"
#include "DirectXMath.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Env.h"
#include "Feature.h"
#include "DX11Hooks.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Render/RenderHooks.h"
#include "Streamline.h"
#include "Render/StreamlineCore.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	using namespace framegeneration;
	using cs::engine::RenderTarget;
	using cs::engine::DepthStencilTarget;
	namespace { auto* L = cs::log::Get("cs.feature.framegeneration"); }

	namespace
	{
		std::string_view BackendName(const FrameGeneration& a_feature)
		{
			if (!a_feature.settings.frameGenerationMode || !a_feature.d3d12Interop)
				return "off";
			switch (a_feature.activeFrameGenType) {
			case FrameGeneration::FrameGenType::kFSR3:
				return "FSR3-FG";
			case FrameGeneration::FrameGenType::kDLSSG:
				return "DLSS-G";
			case FrameGeneration::FrameGenType::kXeSSFG:
				return "XeSS-FG";
			}
			return "off";
		}

		std::string_view RequestedFrameGenerationName(int a_type)
		{
			switch (a_type) {
			case 0:
				return "fsr3";
			case 1:
				return "dlss_g";
			case 2:
				return "xess_fg";
			}
			return "unknown";
		}

		std::string_view EffectiveFrameGenerationName(const FrameGeneration& a_feature)
		{
			if (!a_feature.settings.frameGenerationMode || !a_feature.d3d12Interop ||
				a_feature.GetFrameGenSkipReason() != FrameGeneration::FrameGenSkipReason::kActive)
				return "off";
			switch (a_feature.activeFrameGenType) {
			case FrameGeneration::FrameGenType::kFSR3:
				return "fsr3_fg";
			case FrameGeneration::FrameGenType::kDLSSG:
				return "dlss_g";
			case FrameGeneration::FrameGenType::kXeSSFG:
				return "xess_fg";
			}
			return "off";
		}

		std::string_view FrameGenSkipReasonName(FrameGeneration::FrameGenSkipReason a_reason)
		{
			switch (a_reason) {
			case FrameGeneration::FrameGenSkipReason::kNotDecided:
				return "not_decided";
			case FrameGeneration::FrameGenSkipReason::kActive:
				return "active";
			case FrameGeneration::FrameGenSkipReason::kUserDisabled:
				return "user_disabled";
			case FrameGeneration::FrameGenSkipReason::kExclusiveFullscreen:
				return "exclusive_fullscreen";
			case FrameGeneration::FrameGenSkipReason::kNoModule:
				return "no_module";
			case FrameGeneration::FrameGenSkipReason::kENBSwapChainOwner:
				return "enb_swapchain_owner";
			case FrameGeneration::FrameGenSkipReason::kDlssgUpscalerConflict:
				return "dlssg_conflicts_with_dlss_sr";
			case FrameGeneration::FrameGenSkipReason::kRenderDoc:
				return "renderdoc";
			}
			return "not_decided";
		}

		std::string SettingError(std::string_view a_key, std::string_view a_reason)
		{
			std::string error = "settings.";
			error.append(a_key);
			error.append(": ");
			error.append(a_reason);
			return error;
		}

		bool ReadBoolSetting(
			const toml::table& a_table,
			std::string_view a_key,
			bool& a_value,
			std::string& a_error)
		{
			switch (feature_config::ReadBool(a_table, a_key, a_value)) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = SettingError(a_key, "expected boolean");
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = SettingError(a_key, "invalid boolean value");
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = SettingError(a_key, "boolean value is out of range");
				break;
			}
			return false;
		}

		bool ReadIntegerSetting(
			const toml::table& a_table,
			std::string_view a_key,
			std::int64_t a_min,
			std::int64_t a_max,
			int& a_value,
			std::string& a_error)
		{
			auto value = static_cast<std::int64_t>(a_value);
			switch (feature_config::ReadSignedInteger(a_table, a_key, value, a_min, a_max)) {
			case feature_config::ScalarReadStatus::kMissing:
				return true;
			case feature_config::ScalarReadStatus::kValid:
				a_value = static_cast<int>(value);
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = SettingError(a_key, "expected integer");
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = SettingError(a_key, "invalid integer value");
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = SettingError(
					a_key,
					"value must be in range " + std::to_string(a_min) + ".." + std::to_string(a_max));
				break;
			}
			return false;
		}

		bool ParseSettingsTable(const toml::table& a_config, FrameGeneration::Settings& a_candidate, std::string& a_error)
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

			return ReadBoolSetting(*settingsTable, "frame_generation_mode", a_candidate.frameGenerationMode, a_error)
				&& ReadBoolSetting(*settingsTable, "frame_limit_mode", a_candidate.frameLimitMode, a_error)
				&& ReadBoolSetting(*settingsTable, "disable_in_menus", a_candidate.disableInMenus, a_error)
				&& ReadBoolSetting(*settingsTable, "enable_debug_logging", a_candidate.debugLogging, a_error)
				&& ReadIntegerSetting(*settingsTable, "frame_gen_type", 0, 2, a_candidate.frameGenType, a_error)
				&& ReadIntegerSetting(*settingsTable, "frame_gen_frames", 1, 3, a_candidate.frameGenFrames, a_error);
		}
	}

bool FrameGeneration::Configure(const toml::table& a_config, std::string& a_error)
{
	auto candidate = settings;
	if (!ParseSettingsTable(a_config, candidate, a_error)) {
		return false;
	}

	settings = candidate;
	if (!settings.frameGenerationMode)
		SetFrameGenSkipReason(FrameGenSkipReason::kUserDisabled);
	L->info("frame_generation_mode: {}", settings.frameGenerationMode);
	L->info("frame_limit_mode: {}", settings.frameLimitMode);
	L->info("frame_gen_type: {} (0=FSR3, 1=DLSS-G, 2=XeSS-FG)", settings.frameGenType);
	L->info("frame_gen_frames: {} (1=2x, 2=3x MFG, 3=4x MFG)", settings.frameGenFrames);
	return true;
}

void FrameGeneration::SaveSettings()
{
	toml::table settingsTable;
	settingsTable.insert_or_assign("frame_generation_mode", settings.frameGenerationMode);
	settingsTable.insert_or_assign("frame_limit_mode", settings.frameLimitMode);
	settingsTable.insert_or_assign("disable_in_menus", settings.disableInMenus);
	settingsTable.insert_or_assign("enable_debug_logging", settings.debugLogging);
	settingsTable.insert_or_assign("frame_gen_type", static_cast<int64_t>(settings.frameGenType));
	settingsTable.insert_or_assign("frame_gen_frames", static_cast<int64_t>(settings.frameGenFrames));

	if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settingsTable); !result) {
		L->error("Failed to save settings: {}", result.error);
	}
}

void FrameGeneration::RestoreDefaultSettings()
{
	settings = Settings{};
	if (d3d12Interop)
		SetFrameGenSkipReason(FrameGenSkipReason::kActive);
	else if (WasExclusiveFullscreen())
		SetFrameGenSkipReason(FrameGenSkipReason::kExclusiveFullscreen);
	SaveSettings();
	cs::Menu::ShowToast("Frame Generation reset to defaults", 2.5);
}

void FrameGeneration::DrawSettings()
{
	const char* activeStr = "Inactive";
	if (settings.frameGenerationMode && d3d12Interop &&
		GetFrameGenSkipReason() == FrameGenSkipReason::kActive) {
		switch (activeFrameGenType) {
			case FrameGenType::kFSR3:   activeStr = "FSR3"; break;
			case FrameGenType::kDLSSG:  activeStr = "DLSS-G"; break;
			case FrameGenType::kXeSSFG: activeStr = "XeSS-FG"; break;
		}
	}
	ImGui::Text("Active: %s", activeStr);

	if (settings.frameGenerationMode && WasExclusiveFullscreen())
		ImGui::TextWrapped("Frame generation requires borderless/windowed mode; currently exclusive fullscreen - frame generation is inactive.");

	if (cs::env::IsENBLoaded())
		ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "ENB detected: frame generation routes through ENB's swap-chain wrapper; FSR3 path is most compatible.");

	ImGui::Separator();

	if (ImGui::Checkbox("Enable frame generation", &settings.frameGenerationMode)) {
		if (!settings.frameGenerationMode)
			SetFrameGenSkipReason(FrameGenSkipReason::kUserDisabled);
		else if (d3d12Interop)
			SetFrameGenSkipReason(FrameGenSkipReason::kActive);
		else if (WasExclusiveFullscreen())
			SetFrameGenSkipReason(FrameGenSkipReason::kExclusiveFullscreen);
		SaveSettings();
	}

	static const char* fgTypeLabels[] = {
		"FSR3 (any GPU)",
		"DLSS-G (RTX 40+ NVIDIA)",
		"XeSS-FG (Intel Arc)"
	};
	int fgType = std::clamp(settings.frameGenType, 0, 2);
	if (ImGui::Combo("Mode", &fgType, fgTypeLabels, IM_ARRAYSIZE(fgTypeLabels))) {
		settings.frameGenType = fgType;
		SaveSettings();
	}

	if (settings.frameGenType == 1) {
		static const char* mfgLabels[] = { "2x", "3x (RTX 50+ only)", "4x (RTX 50+ only)" };
		int mfgIdx = std::clamp(settings.frameGenFrames - 1, 0, 2);
		if (ImGui::Combo("Multi-frame generation", &mfgIdx, mfgLabels, IM_ARRAYSIZE(mfgLabels))) {
			settings.frameGenFrames = mfgIdx + 1;
			SaveSettings();
		}
		ImGui::SetItemTooltip("3x/4x require an RTX 50-series GPU; older GPUs silently fall back to 2x.");
	}

	if (ImGui::Checkbox("Disable in menus", &settings.disableInMenus))
		SaveSettings();
	if (ImGui::Checkbox("VRR-aware frame limiter", &settings.frameLimitMode))
		SaveSettings();
	if (ImGui::Checkbox("Streamline debug logging", &settings.debugLogging))
		SaveSettings();
	ImGui::SetItemTooltip("Takes effect on next launch.");

	ImGui::TextDisabled("Mode and MFG changes take effect on next launch.");
}

void FrameGeneration::CollectTelemetry(cs::telemetry::Sink& a_sink) const
{
	const auto currentFrame = cs::telemetry::CurrentFrame();
	const bool captured = hasCapturedFrame
		&& currentFrame >= telemetryLastCapturedFrame
		&& currentFrame - telemetryLastCapturedFrame <= 1;
	const auto skipReason = settings.frameGenerationMode ?
		GetFrameGenSkipReason() : FrameGenSkipReason::kUserDisabled;
	a_sink
		.Field("backend", BackendName(*this))
		.Field("requested_fg", RequestedFrameGenerationName(settings.frameGenType))
		.Field("effective_fg", EffectiveFrameGenerationName(*this))
		.Field("reason", FrameGenSkipReasonName(skipReason))
		.Field("sl_device_api", cs::Streamline::GetSingleton()->GetRegisteredDeviceAPIName())
		.Field("interop", d3d12Interop)
		.Field("buffers_ready", setupBuffers)
		.Field("captured", captured)
		.Field("capture_index", static_cast<std::int64_t>(lastCapturedFrameIndex))
		.Field("multiplier", static_cast<std::int64_t>(cs::env::GetDisplayedFrameMultiplier()))
		.Field("displayed_total", static_cast<std::int64_t>(cs::env::GetDisplayedFrameTotal()));
}

void FrameGeneration::OnPostPostLoad()
{
	highFPSPhysicsFixLoaded = GetModuleHandleA("Data\\F4SE\\Plugins\\HighFPSPhysicsFix.dll") != nullptr;

	if (highFPSPhysicsFixLoaded)
		L->info("HighFPSPhysicsFix.dll is loaded");
	else
		L->info("HighFPSPhysicsFix.dll is not loaded");

	InstallHooks();
}

void FrameGeneration::CreateFrameGenerationResources()
{
	L->info("Creating resources");
	
	setupBuffers = true;

	auto rendererData = RE::BSGraphics::GetRendererData();
	auto& main = rendererData->renderTargets[(uint)RenderTarget::kMain];

	for (int index = 0; index < 2; index++) {
		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		reinterpret_cast<ID3D11Texture2D*>(main.texture)->GetDesc(&texDesc);
		reinterpret_cast<ID3D11ShaderResourceView*>(main.srView)->GetDesc(&srvDesc);
		reinterpret_cast<ID3D11RenderTargetView*>(main.rtView)->GetDesc(&rtvDesc);

		texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;

		texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

		// FSR3 shared buffers require RGBA8.
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		HUDLessBufferShared[index] = new Texture2D(texDesc);
		HUDLessBufferShared[index]->CreateSRV(srvDesc);
		HUDLessBufferShared[index]->CreateRTV(rtvDesc);
		HUDLessBufferShared[index]->CreateUAV(uavDesc);

		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		depthBufferShared[index] = new Texture2D(texDesc);
		depthBufferShared[index]->CreateSRV(srvDesc);
		depthBufferShared[index]->CreateRTV(rtvDesc);
		depthBufferShared[index]->CreateUAV(uavDesc);

		auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
		D3D11_TEXTURE2D_DESC texDescMotionVector{};
		reinterpret_cast<ID3D11Texture2D*>(motionVector.texture)->GetDesc(&texDescMotionVector);

		texDesc.Format = texDescMotionVector.Format;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		motionVectorBufferShared[index] = new Texture2D(texDesc);
		motionVectorBufferShared[index]->CreateSRV(srvDesc);
		motionVectorBufferShared[index]->CreateRTV(rtvDesc);
		motionVectorBufferShared[index]->CreateUAV(uavDesc);

		// DLSS-G recomposition uses a single-channel alpha mask.
		texDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		UIAlphaBufferShared[index] = new Texture2D(texDesc);
		UIAlphaBufferShared[index]->CreateSRV(srvDesc);
		UIAlphaBufferShared[index]->CreateRTV(rtvDesc);
		UIAlphaBufferShared[index]->CreateUAV(uavDesc);

		auto dx12SwapChain = DX12SwapChain::GetSingleton();

		{
			winrt::com_ptr<IDXGIResource1> dxgiResource;
			DX::ThrowIfFailed(HUDLessBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&HUDLessBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			winrt::com_ptr<IDXGIResource1> dxgiResource;
			DX::ThrowIfFailed(depthBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&depthBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			winrt::com_ptr<IDXGIResource1> dxgiResource;
			DX::ThrowIfFailed(motionVectorBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&motionVectorBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			winrt::com_ptr<IDXGIResource1> dxgiResource;
			DX::ThrowIfFailed(UIAlphaBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&UIAlphaBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}
	}

	copyDepthToSharedBufferCS = (ID3D11ComputeShader*)cs::util::CompileShader(L"Data\\Shaders\\FrameGeneration\\CopyDepthToSharedBufferCS.hlsl", {}, "cs_5_0");
	generateSharedBuffersCS = (ID3D11ComputeShader*)cs::util::CompileShader(L"Data\\Shaders\\FrameGeneration\\GenerateSharedBuffersCS.hlsl", {}, "cs_5_0");
	uiAlphaMaskCS = (ID3D11ComputeShader*)cs::util::CompileShader(L"Data\\Shaders\\FrameGeneration\\UIAlphaMaskCS.hlsl", {}, "cs_5_0");

	L->info("Frame generation resources created (HUDLess + Depth + MVec + UIAlpha)");
}

void FrameGeneration::PreAlpha()
{
	if (!d3d12Interop)
		return;

	auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	
	auto& colorMain = rendererData->renderTargets[(uint)RenderTarget::kMain];
	auto& colorPostAlpha = rendererData->renderTargets[(uint)RenderTarget::kMainTemp];

	{
		TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "PreAlpha");
		context->CopyResource(reinterpret_cast<ID3D11Texture2D*>(colorMain.texture), reinterpret_cast<ID3D11Texture2D*>(colorPostAlpha.texture));
	}
}

void FrameGeneration::PostAlpha()
{
	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();

	auto rendererData = RE::BSGraphics::GetRendererData();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	cs::engine::ComputeOMScope omcs(context);

	{
		auto& colorPreAlpha = rendererData->renderTargets[(uint)RenderTarget::kMain];
		auto& colorPostAlpha = rendererData->renderTargets[(uint)RenderTarget::kMainTemp];

		auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
		auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];

		{
			uint32_t dispatchX = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Width) / 8.0f);
			uint32_t dispatchY = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Height) / 8.0f);

			ID3D11ShaderResourceView* views[4] = { 
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPreAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPostAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(motionVector.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth)
			};

			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[3] = {
				motionVectorBufferShared[dx12SwapChain->frameIndex]->uav.get(),
				depthBufferShared[dx12SwapChain->frameIndex]->uav.get(),
				UIAlphaBufferShared[dx12SwapChain->frameIndex]->uav.get()
			};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(generateSharedBuffersCS, nullptr, 0);

			{
				TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "Capture");
				context->Dispatch(dispatchX, dispatchY, 1);
			}
		}

		// Clear bound SRVs to prevent OM/CS hazards.
		ID3D11ShaderResourceView* views[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[3] = { nullptr, nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}
}

void FrameGeneration::CopyBuffersToSharedResources()
{
	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();

	auto rendererData = RE::BSGraphics::GetRendererData();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	
	cs::engine::ComputeOMScope omcs(context);

	auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
	context->CopyResource(motionVectorBufferShared[dx12SwapChain->frameIndex]->resource.get(), reinterpret_cast<ID3D11Texture2D*>(motionVector.texture));
		
	{
		auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];

		{
			uint32_t dispatchX = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Width) / 8.0f);
			uint32_t dispatchY = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Height) / 8.0f);


			ID3D11ShaderResourceView* views[1] = { reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth) };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { depthBufferShared[dx12SwapChain->frameIndex]->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(copyDepthToSharedBufferCS, nullptr, 0);

			{
				TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "Capture");
				context->Dispatch(dispatchX, dispatchY, 1);
			}
		}

		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}	
}

void FrameGeneration::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void FrameGeneration::FrameLimiter(bool a_useFrameGeneration)
{
	static LARGE_INTEGER lastFrame = {};

	if (d3d12Interop && settings.frameLimitMode) {

		// Stay inside VRR bounds.
		double bestRefreshRate = refreshRate - (refreshRate * refreshRate) / 3600.0;

		LARGE_INTEGER qpf;
		QueryPerformanceFrequency(&qpf);

		int64_t targetFrameTicks = int64_t(double(qpf.QuadPart) / (bestRefreshRate * (a_useFrameGeneration ? 0.5 : 1.0)));

		LARGE_INTEGER timeNow;
		QueryPerformanceCounter(&timeNow);
		int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
		if (delta < targetFrameTicks) {
			TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
		}
	}

	QueryPerformanceCounter(&lastFrame);
}

void FrameGeneration::GameFrameLimiter()
{
	double bestRefreshRate = 60.0f;

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);

	int64_t targetFrameTicks = int64_t(double(qpf.QuadPart) / bestRefreshRate);

	static LARGE_INTEGER lastFrame = {};
	LARGE_INTEGER timeNow;
	QueryPerformanceCounter(&timeNow);
	int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
	if (delta < targetFrameTicks) {
		TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
	}
	QueryPerformanceCounter(&lastFrame);	
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double FrameGeneration::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		// CCD identifies the window's active display path.
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
						// Mirrored displays can share one source.
						if (wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
							UINT numerator = p.targetInfo.refreshRate.Numerator;
							UINT denominator = p.targetInfo.refreshRate.Denominator;
							return (double)numerator / (double)denominator;
						}
					}
				}
			}
		}
	}
	L->error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

void FrameGeneration::PostDisplay()
{
	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();

	auto rendererData = RE::BSGraphics::GetRendererData();

	auto& swapChain = rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer];
	ID3D11Resource* swapChainResource;
	reinterpret_cast<ID3D11RenderTargetView*>(swapChain.rtView)->GetResource(&swapChainResource);

	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	{
		TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "Capture");
		context->CopyResource(HUDLessBufferShared[dx12SwapChain->frameIndex]->resource.get(), swapChainResource);
	}
	lastCapturedFrameIndex = dx12SwapChain->frameIndex;
	hasCapturedFrame = true;
	telemetryLastCapturedFrame = cs::telemetry::CurrentFrame();

	static bool loggedOnce = false;
	if (!loggedOnce) {
		L->info("PostDisplay captured HUDLess (frameIdx={})", dx12SwapChain->frameIndex);
		loggedOnce = true;
	}
}

void FrameGeneration::GenerateUIAlphaMask()
{
	if (!d3d12Interop || !setupBuffers || !uiAlphaMaskCS)
		return;

	// Skip UI-alpha work when menus disable frame generation.
	if (settings.disableInMenus) {
		if (auto* main = RE::Main::GetSingleton(); main && main->inMenuMode)
			return;
	}

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	ID3D11ShaderResourceView* backbufferSRV = dx12SwapChain->swapChainBufferProxy ? dx12SwapChain->swapChainBufferProxy->srv : nullptr;
	if (!backbufferSRV)
		return;

	auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Bound proxy RTVs return zero when sampled.
	cs::engine::ComputeOMScope omcs(context);

	uint32_t dispatchX = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Width) / 8.0f);
	uint32_t dispatchY = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Height) / 8.0f);

	ID3D11ShaderResourceView* srvs[2] = {
		HUDLessBufferShared[dx12SwapChain->frameIndex]->srv.get(),
		backbufferSRV
	};
	context->CSSetShaderResources(0, 2, srvs);

	ID3D11UnorderedAccessView* uavs[1] = {
		UIAlphaBufferShared[dx12SwapChain->frameIndex]->uav.get()
	};
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

	context->CSSetShader(uiAlphaMaskCS, nullptr, 0);
	{
		TracyD3D11Zone(cs::Menu::Get().GetTracyD3D11Ctx(), "CaptureUI");
		context->Dispatch(dispatchX, dispatchY, 1);
	}

	ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
	context->CSSetShaderResources(0, 2, nullSRVs);
	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
	context->CSSetShader(nullptr, nullptr, 0);
}

void FrameGeneration::Reset()
{
	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();

	auto rendererData = RE::BSGraphics::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->ClearRenderTargetView(HUDLessBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	context->ClearRenderTargetView(depthBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	context->ClearRenderTargetView(motionVectorBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	context->ClearRenderTargetView(UIAlphaBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
}

struct WindowSizeChanged
{
	// Ignore the engine's duplicate initialization resize.
	static void thunk(RE::BSGraphics::Renderer*, unsigned int)
	{
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

bool reticleFix = false;

struct DrawWorld_Forward
{
	static void thunk(void* a1)
	{		
		func(a1);

		if (!reticleFix)
			FrameGeneration::GetSingleton()->CopyBuffersToSharedResources();

		reticleFix = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct DrawWorld_Reticle
{
	static void thunk(void* a1)
	{
		auto frameGen = FrameGeneration::GetSingleton();
		frameGen->PreAlpha();
		func(a1);
		reticleFix = true;
		frameGen->PostAlpha();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void FrameGeneration::InstallHooks()
{
	auto runtimeIdx = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());

	// Swallow FO4's duplicate init resize event.
	stl::detour_thunk<WindowSizeChanged>(REL::ID({ 212827, 2276824, 2276824 }));

	// Capture after native and optional upscale work.
	cs::engine::RegisterPostDynResViewport_FGCapture([](bool a_setting) {
		if (!a_setting) {
			FrameGeneration::GetSingleton()->PostDisplay();
		}
	});

	stl::detour_thunk<DrawWorld_Forward>(REL::ID({ 656535, 2318315, 2318315 }));

	constexpr std::ptrdiff_t reticleOffsets[] = { 0x253, 0x53D, 0x53D };
	stl::write_thunk_call<DrawWorld_Reticle>(
		REL::ID({ 338205, 2318315, 2318315 }).address() + reticleOffsets[runtimeIdx]);

	L->info("Installed hooks");
}

	void FrameGeneration::Load()
	{
		// DLSS-G tagging breaks Upscaling's slSetTag path.
		if (settings.frameGenType == 1) {
			auto* core = cs::Streamline::GetSingleton();
			core->RequestFeature(sl::kFeatureDLSS_G);
			core->RequestFeature(sl::kFeatureReflex);
			core->RequestFeature(sl::kFeaturePCL);
		}

		DX11Hooks::Install();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(FrameGeneration::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}

}
