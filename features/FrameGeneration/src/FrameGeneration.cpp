#include "FrameGeneration.h"

#include <d3dcompiler.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>

#include "DX12SwapChain.h"
#include "CSUtil.h"
#include "DirectXMath.h"
#include "Engine.h"
#include "Env.h"
#include "Feature.h"
#include "DX11Hooks.h"
#include "Log.h"
#include "RenderHooks.h"
#include "Streamline.h"
#include "StreamlineCore.h"

namespace cs::features
{
	using namespace framegeneration;
	using cs::engine::RenderTarget;
	using cs::engine::DepthStencilTarget;
	namespace { auto* L = cs::log::Get("cs.feature.framegen"); }

	constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\FrameGeneration.toml";

void FrameGeneration::LoadSettings()
{
	toml::table table;
	try {
		table = toml::parse_file(kConfigPath);
	} catch (const toml::parse_error&) {
		return;
	}

	settings.frameGenerationMode = table["settings"]["frame_generation_mode"].value_or(settings.frameGenerationMode);
	settings.frameLimitMode = table["settings"]["frame_limit_mode"].value_or(settings.frameLimitMode);
	settings.disableInMenus = table["settings"]["disable_in_menus"].value_or(settings.disableInMenus);
	settings.debugLogging = table["settings"]["enable_debug_logging"].value_or(settings.debugLogging);
	settings.frameGenType = std::clamp(static_cast<int>(table["settings"]["frame_gen_type"].value_or<int64_t>(settings.frameGenType)), 0, 2);
	settings.frameGenFrames = std::clamp(static_cast<int>(table["settings"]["frame_gen_frames"].value_or<int64_t>(settings.frameGenFrames)), 1, 3);

	static bool loggedOnce = false;
	if (!loggedOnce) {
		L->info("frame_generation_mode: {}", settings.frameGenerationMode);
		L->info("frame_limit_mode: {}", settings.frameLimitMode);
		L->info("frame_gen_type: {} (0=FSR3, 1=DLSS-G, 2=XeSS-FG)", settings.frameGenType);
		L->info("frame_gen_frames: {} (1=2x, 2=3x MFG, 3=4x MFG)", settings.frameGenFrames);
		loggedOnce = true;
	}
}

void FrameGeneration::ReloadSettingsIfNeeded()
{
	static int frameCounter = 0;
	if (++frameCounter % 60 != 0) return;
	LoadSettings();
}

void FrameGeneration::SaveSettings()
{
	toml::table table;
	try {
		table = toml::parse_file(kConfigPath);
	} catch (const toml::parse_error&) {
		table = toml::table{};
	}

	auto& settingsTable = table.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
	settingsTable.insert_or_assign("frame_generation_mode", settings.frameGenerationMode);
	settingsTable.insert_or_assign("frame_limit_mode", settings.frameLimitMode);
	settingsTable.insert_or_assign("disable_in_menus", settings.disableInMenus);
	settingsTable.insert_or_assign("enable_debug_logging", settings.debugLogging);
	settingsTable.insert_or_assign("frame_gen_type", static_cast<int64_t>(settings.frameGenType));
	settingsTable.insert_or_assign("frame_gen_frames", static_cast<int64_t>(settings.frameGenFrames));

	std::ofstream out(kConfigPath);
	if (out) {
		out << table;
	}
}

void FrameGeneration::DrawSettings()
{
	const char* activeStr = "Inactive";
	if (settings.frameGenerationMode) {
		switch (activeFrameGenType) {
			case FrameGenType::kFSR3:   activeStr = "FSR3"; break;
			case FrameGenType::kDLSSG:  activeStr = "DLSS-G"; break;
			case FrameGenType::kXeSSFG: activeStr = "XeSS-FG"; break;
		}
	}
	ImGui::Text("Active: %s", activeStr);

	if (cs::env::IsENBLoaded())
		ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "ENB detected: frame generation routes through ENB's swap-chain wrapper; FSR3 path is most compatible.");

	ImGui::Separator();

	if (ImGui::Checkbox("Enable frame generation", &settings.frameGenerationMode))
		SaveSettings();

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

		// Force RGBA8 for shared buffer - FSR3 requires it
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

		// Single-channel R8 alpha mask drives DLSS-G's kBufferTypeUIAlpha recomposition path.
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
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(HUDLessBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

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
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(depthBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

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
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(motionVectorBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

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
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(UIAlphaBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

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

	copyDepthToSharedBufferCS = (ID3D11ComputeShader*)cs::util::CompileShader(L"Data\\F4SE\\Plugins\\FrameGeneration\\CopyDepthToSharedBufferCS.hlsl", {}, "cs_5_0");
	generateSharedBuffersCS = (ID3D11ComputeShader*)cs::util::CompileShader(L"Data\\F4SE\\Plugins\\FrameGeneration\\GenerateSharedBuffersCS.hlsl", {}, "cs_5_0");
	uiAlphaMaskCS = (ID3D11ComputeShader*)cs::util::CompileShader(L"Data\\F4SE\\Plugins\\FrameGeneration\\UIAlphaMaskCS.hlsl", {}, "cs_5_0");

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

	context->CopyResource(reinterpret_cast<ID3D11Texture2D*>(colorMain.texture), reinterpret_cast<ID3D11Texture2D*>(colorPostAlpha.texture));
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

	context->OMSetRenderTargets(0, nullptr, nullptr);

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

			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* views[3] = { nullptr, nullptr, nullptr };
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
	
	context->OMSetRenderTargets(0, nullptr, nullptr);

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

			context->Dispatch(dispatchX, dispatchY, 1);
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

		// Stick within VRR bounds
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
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						if (wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
							// get the refresh rate
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
	context->CopyResource(HUDLessBufferShared[dx12SwapChain->frameIndex]->resource.get(), swapChainResource);

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

	// Match the menu-block check applied by DX12SwapChain::Present so we don't waste a dispatch
	// when frame generation will be skipped this frame anyway.
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

	// Proxy backbuffer may still be bound as RTV from game rendering; D3D11 returns zeros if read while bound.
	context->OMSetRenderTargets(0, nullptr, nullptr);

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
	context->Dispatch(dispatchX, dispatchY, 1);

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

	// Fix game initialising twice
	stl::detour_thunk<WindowSizeChanged>(REL::ID({ 212827, 2276824, 2276824 }));

	// Watch frame presentation via the shared post-upscale broker so Imagespace post-FX lands
	// before HUDLess capture (avoids the 60Hz judder when Imagespace was second-to-install).
	cs::engine::RegisterPostDynResViewport_FGCapture([](bool a_setting) {
		if (!a_setting) {
			FrameGeneration::GetSingleton()->PostDisplay();
		}
	});

	// Fix reticles on motion vectors and depth
	stl::detour_thunk<DrawWorld_Forward>(REL::ID({ 656535, 2318315, 2318315 }));

	constexpr std::ptrdiff_t reticleOffsets[] = { 0x253, 0x53D, 0x53D };
	stl::write_thunk_call<DrawWorld_Reticle>(
		REL::ID({ 338205, 2318315, 2318315 }).address() + reticleOffsets[runtimeIdx]);

	L->info("Installed hooks");
}

	void FrameGeneration::Load()
	{
		LoadSettings();

		// Gated: requesting DLSS-G triggers eUseFrameBasedResourceTagging on slInit, which breaks Upscaling's regular slSetTag path.
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
