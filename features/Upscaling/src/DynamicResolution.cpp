#include "DynamicResolution.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/ComputeScope.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.dynres");

		// Engine render-target slots that hold the world and HDR imagespace chain.
		constexpr int renderTargetsPatch[] = {
			20, 57, 24, 25, 23, 58, 59, 28, 3, 9, 60, 61, 4, 29, 1, 36, 37, 22, 10, 11, 7, 8, 64, 14, 16
		};

		constexpr const wchar_t* kOverrideDepthPath = L"Data\\Shaders\\Upscaling\\OverrideDepthCS.hlsl";
		constexpr const wchar_t* kOverrideLinearDepthPath = L"Data\\Shaders\\Upscaling\\OverrideLinearDepthCS.hlsl";

		// Native SetUseDynamicResolutionViewportAsDefaultViewport; toggles the render-target viewport.
		void SetUseDynamicResolutionViewport(RE::BSGraphics::RenderTargetManager* a_manager, bool a_enabled)
		{
			using func_t = void (*)(RE::BSGraphics::RenderTargetManager*, bool);
			static REL::Relocation<func_t> func{ REL::ID({ 0, 2277194, 2277194 }) };
			func(a_manager, a_enabled);
		}

		template <class T>
		void SafeRelease(T*& a_ptr)
		{
			if (a_ptr) {
				a_ptr->Release();
				a_ptr = nullptr;
			}
		}
	}

	void DynamicResolution::ReleaseProxy(int a_index)
	{
		auto& proxy = proxyRenderTargets[a_index];
		SafeRelease(proxy.uaView);
		SafeRelease(proxy.srView);
		SafeRelease(proxy.rtView);
		SafeRelease(proxy.texture);
		proxy = {};
	}

	DynamicResolution::ProxyTexture DynamicResolution::GetProxyTexture(int a_index) const noexcept
	{
		if (a_index < 0 || a_index >= static_cast<int>(std::size(proxyRenderTargets))) {
			return {};
		}

		const auto& proxy = proxyRenderTargets[a_index];
		auto* texture = reinterpret_cast<ID3D11Texture2D*>(proxy.texture);
		auto* view = reinterpret_cast<ID3D11ShaderResourceView*>(proxy.srView);
		if (!texture || !view) {
			return {};
		}

		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		return { view, desc.Width, desc.Height };
	}

	void DynamicResolution::UpdateRenderTarget(int a_index, float a_widthRatio, float a_heightRatio)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}

		// Keep the engine's copyTexture/copySRView pointers so a swap-in never nulls them.
		originalRenderTargets[a_index] = rendererData->renderTargets[a_index];
		proxyRenderTargets[a_index] = originalRenderTargets[a_index];
		proxyRenderTargets[a_index].texture = nullptr;
		proxyRenderTargets[a_index].rtView = nullptr;
		proxyRenderTargets[a_index].srView = nullptr;
		proxyRenderTargets[a_index].uaView = nullptr;

		auto& original = originalRenderTargets[a_index];
		auto& proxy = proxyRenderTargets[a_index];

		if (a_widthRatio == 1.0f && a_heightRatio == 1.0f) {
			return;
		}

		auto* originalTexture = reinterpret_cast<ID3D11Texture2D*>(original.texture);
		if (!originalTexture) {
			return;
		}

		D3D11_TEXTURE2D_DESC textureDesc{};
		originalTexture->GetDesc(&textureDesc);

		D3D11_RENDER_TARGET_VIEW_DESC rtViewDesc{};
		if (auto* rtv = reinterpret_cast<ID3D11RenderTargetView*>(original.rtView)) {
			rtv->GetDesc(&rtViewDesc);
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC srViewDesc{};
		if (auto* srv = reinterpret_cast<ID3D11ShaderResourceView*>(original.srView)) {
			srv->GetDesc(&srViewDesc);
		}
		D3D11_UNORDERED_ACCESS_VIEW_DESC uaViewDesc{};
		if (auto* uav = reinterpret_cast<ID3D11UnorderedAccessView*>(original.uaView)) {
			uav->GetDesc(&uaViewDesc);
		}

		textureDesc.Width = static_cast<std::uint32_t>(static_cast<float>(textureDesc.Width) * a_widthRatio);
		textureDesc.Height = static_cast<std::uint32_t>(static_cast<float>(textureDesc.Height) * a_heightRatio);

		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);

		DX::ThrowIfFailed(device->CreateTexture2D(
			&textureDesc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&proxy.texture)));
		const auto baseName = std::format("Upscaling/DynamicResolutionProxy[{}]", a_index);
		cs::render::annotation::SetName(
			reinterpret_cast<ID3D11Texture2D*>(proxy.texture),
			baseName + ".Texture");

		auto* proxyTexture = reinterpret_cast<ID3D11Texture2D*>(proxy.texture);
		if (original.rtView) {
			DX::ThrowIfFailed(device->CreateRenderTargetView(
				proxyTexture, &rtViewDesc, reinterpret_cast<ID3D11RenderTargetView**>(&proxy.rtView)));
			cs::render::annotation::SetName(
				reinterpret_cast<ID3D11RenderTargetView*>(proxy.rtView),
				baseName + ".RTV");
		}
		if (original.srView) {
			DX::ThrowIfFailed(device->CreateShaderResourceView(
				proxyTexture, &srViewDesc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxy.srView)));
			cs::render::annotation::SetName(
				reinterpret_cast<ID3D11ShaderResourceView*>(proxy.srView),
				baseName + ".SRV");
		}
		if (original.uaView) {
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(
				proxyTexture, &uaViewDesc, reinterpret_cast<ID3D11UnorderedAccessView**>(&proxy.uaView)));
			cs::render::annotation::SetName(
				reinterpret_cast<ID3D11UnorderedAccessView*>(proxy.uaView),
				baseName + ".UAV");
		}
	}

	void DynamicResolution::UpdateRenderTargets(float a_widthRatio, float a_heightRatio)
	{
		if (_previousWidthRatio == a_widthRatio && _previousHeightRatio == a_heightRatio && _hasProxies) {
			return;
		}
		_previousWidthRatio = a_widthRatio;
		_previousHeightRatio = a_heightRatio;

		for (const int index : renderTargetsPatch) {
			ReleaseProxy(index);
			UpdateRenderTarget(index, a_widthRatio, a_heightRatio);
		}

		_depthOverrideTexture = nullptr;

		auto* frameBufferSRV = cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kFrameBuffer);
		if (!frameBufferSRV) {
			_hasProxies = false;
			return;
		}

		winrt::com_ptr<ID3D11Resource> frameBufferResource;
		frameBufferSRV->GetResource(frameBufferResource.put());
		winrt::com_ptr<ID3D11Texture2D> frameBufferTexture;
		if (!frameBufferResource ||
			FAILED(frameBufferResource->QueryInterface(IID_PPV_ARGS(frameBufferTexture.put())))) {
			_hasProxies = false;
			return;
		}

		D3D11_TEXTURE2D_DESC texDesc{};
		frameBufferTexture->GetDesc(&texDesc);
		_hasProxies = true;

		if (a_widthRatio == 1.0f && a_heightRatio == 1.0f) {
			return;
		}

		texDesc.Width = static_cast<std::uint32_t>(static_cast<float>(texDesc.Width) * a_widthRatio);
		texDesc.Height = static_cast<std::uint32_t>(static_cast<float>(texDesc.Height) * a_heightRatio);
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.CPUAccessFlags = 0;
		texDesc.MiscFlags = 0;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;

		_depthOverrideTexture = std::make_unique<cs::buffer::Texture2D>(texDesc);
		_depthOverrideTexture->CreateSRV(srvDesc);
		_depthOverrideTexture->CreateUAV(uavDesc);
		_depthOverrideTexture->SetName(
			"Upscaling/DepthOverride.Texture",
			"Upscaling/DepthOverride.SRV",
			"Upscaling/DepthOverride.UAV");
	}

	void DynamicResolution::OverrideRenderTarget(int a_index, bool a_doCopy)
	{
		if (!originalRenderTargets[a_index].texture || !proxyRenderTargets[a_index].texture) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		rendererData->renderTargets[a_index] = proxyRenderTargets[a_index];

		if (!a_doCopy) {
			return;
		}

		auto* proxyTexture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[a_index].texture);
		auto* originalTexture = reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[a_index].texture);

		D3D11_TEXTURE2D_DESC proxyDesc{};
		proxyTexture->GetDesc(&proxyDesc);

		D3D11_BOX box{ 0, 0, 0, proxyDesc.Width, proxyDesc.Height, 1 };
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		context->CopySubresourceRegion(proxyTexture, 0, 0, 0, 0, originalTexture, 0, &box);
	}

	void DynamicResolution::ResetRenderTarget(int a_index, bool a_doCopy)
	{
		if (!originalRenderTargets[a_index].texture || !proxyRenderTargets[a_index].texture) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();

		if (a_doCopy) {
			auto* proxyTexture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[a_index].texture);
			auto* originalTexture = reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[a_index].texture);

			D3D11_TEXTURE2D_DESC proxyDesc{};
			proxyTexture->GetDesc(&proxyDesc);

			D3D11_BOX box{ 0, 0, 0, proxyDesc.Width, proxyDesc.Height, 1 };
			auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
			context->CopySubresourceRegion(originalTexture, 0, 0, 0, 0, proxyTexture, 0, &box);
		}

		rendererData->renderTargets[a_index] = originalRenderTargets[a_index];
	}

	void DynamicResolution::OverrideRenderTargets(const std::vector<int>& a_indicesToCopy)
	{
		if (!_hasProxies) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope(
			"Upscaling/DynamicResolution/CopyToProxies");

		for (const int index : renderTargetsPatch) {
			const bool shouldCopy =
				std::find(a_indicesToCopy.begin(), a_indicesToCopy.end(), index) != a_indicesToCopy.end();
			OverrideRenderTarget(index, shouldCopy);
		}

		auto* renderTargetManager = cs::engine::GetRenderTargetManager();
		if (!renderTargetManager) {
			return;
		}

		const float widthRatio = renderTargetManager->GetDynamicWidthRatio();
		const float heightRatio = renderTargetManager->GetDynamicHeightRatio();

		// Report the scaled dimensions so callers that query render-target sizes stay correct.
		for (int i = 0; i < 100; i++) {
			originalRenderTargetData[i] = renderTargetManager->renderTargetData[i];
			renderTargetManager->renderTargetData[i].width = static_cast<std::uint32_t>(
				static_cast<float>(renderTargetManager->renderTargetData[i].width) * widthRatio);
			renderTargetManager->renderTargetData[i].height = static_cast<std::uint32_t>(
				static_cast<float>(renderTargetManager->renderTargetData[i].height) * heightRatio);
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

		// Repoint any pixel-shader SRV already bound from an original target to its proxy.
		ID3D11ShaderResourceView* boundSRVs[16] = {};
		context->PSGetShaderResources(0, 16, boundSRVs);
		for (int slot = 0; slot < 16; slot++) {
			if (!boundSRVs[slot]) {
				continue;
			}
			for (const int index : renderTargetsPatch) {
				auto* originalSRV = reinterpret_cast<ID3D11ShaderResourceView*>(originalRenderTargets[index].srView);
				auto* proxySRV = reinterpret_cast<ID3D11ShaderResourceView*>(proxyRenderTargets[index].srView);
				if (boundSRVs[slot] == originalSRV && proxySRV) {
					context->PSSetShaderResources(slot, 1, &proxySRV);
					break;
				}
			}
			boundSRVs[slot]->Release();
		}

		SetUseDynamicResolutionViewport(renderTargetManager, false);
		_renderTargetsOverridden = true;
	}

	void DynamicResolution::ResetRenderTargets(const std::vector<int>& a_indicesToCopy)
	{
		if (!_renderTargetsOverridden) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope(
			"Upscaling/DynamicResolution/CopyFromProxies");

		for (const int index : renderTargetsPatch) {
			const bool shouldCopy = a_indicesToCopy.empty() ||
				std::find(a_indicesToCopy.begin(), a_indicesToCopy.end(), index) != a_indicesToCopy.end();
			ResetRenderTarget(index, shouldCopy);
		}

		auto* renderTargetManager = cs::engine::GetRenderTargetManager();
		if (!renderTargetManager) {
			return;
		}

		for (int i = 0; i < 100; i++) {
			renderTargetManager->renderTargetData[i] = originalRenderTargetData[i];
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

		ID3D11ShaderResourceView* boundSRVs[16] = {};
		context->PSGetShaderResources(0, 16, boundSRVs);
		for (int slot = 0; slot < 16; slot++) {
			if (!boundSRVs[slot]) {
				continue;
			}
			for (const int index : renderTargetsPatch) {
				auto* originalSRV = reinterpret_cast<ID3D11ShaderResourceView*>(originalRenderTargets[index].srView);
				auto* proxySRV = reinterpret_cast<ID3D11ShaderResourceView*>(proxyRenderTargets[index].srView);
				if (boundSRVs[slot] == proxySRV && originalSRV) {
					context->PSSetShaderResources(slot, 1, &originalSRV);
					break;
				}
			}
			boundSRVs[slot]->Release();
		}

		SetUseDynamicResolutionViewport(renderTargetManager, true);
		_renderTargetsOverridden = false;
	}

	void DynamicResolution::OverrideDepth(bool a_doCopy)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !_depthOverrideTexture) {
			return;
		}

		// Fail closed: without the depth compute shaders the override texture is never populated.
		if (!GetOverrideDepthCS() || !GetOverrideLinearDepthCS()) {
			return;
		}

		auto& mainDepth =
			rendererData->depthStencilTargets[static_cast<uint>(cs::engine::DepthStencilTarget::kMain)];
		_originalDepthView = reinterpret_cast<ID3D11ShaderResourceView*>(mainDepth.srViewDepth);

		if (a_doCopy) {
			const std::uint64_t frame = cs::engine::GetGraphicsState()
				? cs::engine::GetGraphicsState()->frameCount
				: 0;
			if (_depthCopyFrame != frame) {
				CopyDepth();
				_depthCopyFrame = frame;
			}
		}

		mainDepth.srViewDepth =
			reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(_depthOverrideTexture->srv.get());
		_depthOverridden = true;
	}

	void DynamicResolution::ResetDepth()
	{
		if (!_depthOverridden) {
			return;
		}
		_depthOverridden = false;

		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}
		rendererData->depthStencilTargets[static_cast<uint>(cs::engine::DepthStencilTarget::kMain)].srViewDepth =
			reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(_originalDepthView);
	}

	void DynamicResolution::CopyDepth()
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* state = cs::engine::GetGraphicsState();
		if (!context || !state || !_depthOverrideTexture) {
			return;
		}

		auto* depthSRV = cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMain);
		auto* linearDepthUAV = cs::engine::GetRenderTargetUAV(cs::engine::RenderTarget::kMainDepthMips);
		auto* depthUAV = _depthOverrideTexture->uav.get();
		if (!depthSRV || !linearDepthUAV || !depthUAV) {
			return;
		}

		auto* linearDepthCS = GetOverrideLinearDepthCS();
		auto* overrideDepthCS = GetOverrideDepthCS();
		if (!linearDepthCS || !overrideDepthCS) {
			return;
		}

		const float2 screenSize{ static_cast<float>(state->screenWidth), static_cast<float>(state->screenHeight) };
		auto* renderTargetManager = cs::engine::GetRenderTargetManager();
		const float widthRatio = renderTargetManager ? renderTargetManager->GetDynamicWidthRatio() : 1.0f;
		const float heightRatio = renderTargetManager ? renderTargetManager->GetDynamicHeightRatio() : 1.0f;
		const float2 renderSize{ screenSize.x * widthRatio, screenSize.y * heightRatio };

		// Unbind the output-merger before sampling the engine depth in compute.
		cs::engine::OMScope omScope(context);
		cs::ComputeScope computeScope(context);

		UpdateAndBindUpscalingCB(context, screenSize, renderSize);

		{
			cs::render::annotation::ScopedEvent annotationScope(
				"Upscaling/DynamicResolution/LinearizeDepth");
			ID3D11ShaderResourceView* views[] = { depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);
			ID3D11UnorderedAccessView* uavs[] = { linearDepthUAV };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
			context->CSSetShader(linearDepthCS, nullptr, 0);
			context->Dispatch(
				static_cast<std::uint32_t>(std::ceil(screenSize.x / 8.0f)),
				static_cast<std::uint32_t>(std::ceil(screenSize.y / 8.0f)),
				1);
		}

		{
			cs::render::annotation::ScopedEvent annotationScope(
				"Upscaling/DynamicResolution/OverrideDepth");
			ID3D11ShaderResourceView* views[] = { depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);
			ID3D11UnorderedAccessView* uavs[] = { depthUAV };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
			context->CSSetShader(overrideDepthCS, nullptr, 0);
			context->Dispatch(
				static_cast<std::uint32_t>(std::ceil(renderSize.x / 8.0f)),
				static_cast<std::uint32_t>(std::ceil(renderSize.y / 8.0f)),
				1);
		}
	}

	void DynamicResolution::Release()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* renderTargetManager = cs::engine::GetRenderTargetManager();

		// Unwind an in-progress render-target override so no engine state is left mutated.
		if (_renderTargetsOverridden) {
			if (rendererData) {
				for (const int index : renderTargetsPatch) {
					auto* proxyTexture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture);
					auto* liveTexture = reinterpret_cast<ID3D11Texture2D*>(rendererData->renderTargets[index].texture);
					if (proxyTexture && liveTexture == proxyTexture && originalRenderTargets[index].texture) {
						rendererData->renderTargets[index] = originalRenderTargets[index];
					}
				}
			}
			if (renderTargetManager) {
				for (int i = 0; i < 100; i++) {
					renderTargetManager->renderTargetData[i] = originalRenderTargetData[i];
				}
				SetUseDynamicResolutionViewport(renderTargetManager, true);
			}
			_renderTargetsOverridden = false;
		}

		// Restore the engine depth SRV if the override is still applied.
		if (_depthOverridden) {
			if (rendererData) {
				rendererData->depthStencilTargets[static_cast<uint>(cs::engine::DepthStencilTarget::kMain)].srViewDepth =
					reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(_originalDepthView);
			}
			_depthOverridden = false;
		}

		for (const int index : renderTargetsPatch) {
			ReleaseProxy(index);
			originalRenderTargets[index] = {};
		}

		_originalDepthView = nullptr;
		_depthOverrideTexture = nullptr;
		_previousWidthRatio = -1.0f;
		_previousHeightRatio = -1.0f;
		_depthCopyFrame = std::numeric_limits<std::uint64_t>::max();
		_hasProxies = false;
	}

	ID3D11ComputeShader* DynamicResolution::GetOverrideDepthCS()
	{
		if (!_overrideDepthCS && !_overrideDepthCSFailed) {
			L->debug("Compiling OverrideDepthCS.hlsl");
			_overrideDepthCS.attach(
				static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(kOverrideDepthPath, {}, "cs_5_0")));
			if (!_overrideDepthCS) {
				_overrideDepthCSFailed = true;
				L->error("OverrideDepthCS.hlsl failed to compile; depth override is disabled");
			} else {
				cs::render::annotation::SetName(
					_overrideDepthCS.get(), "Upscaling/OverrideDepth.CS");
			}
		}
		return _overrideDepthCS.get();
	}

	ID3D11ComputeShader* DynamicResolution::GetOverrideLinearDepthCS()
	{
		if (!_overrideLinearDepthCS && !_overrideLinearDepthCSFailed) {
			L->debug("Compiling OverrideLinearDepthCS.hlsl");
			_overrideLinearDepthCS.attach(
				static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(kOverrideLinearDepthPath, {}, "cs_5_0")));
			if (!_overrideLinearDepthCS) {
				_overrideLinearDepthCSFailed = true;
				L->error("OverrideLinearDepthCS.hlsl failed to compile; depth override is disabled");
			} else {
				cs::render::annotation::SetName(
					_overrideLinearDepthCS.get(), "Upscaling/OverrideLinearDepth.CS");
			}
		}
		return _overrideLinearDepthCS.get();
	}

	cs::buffer::ConstantBuffer* DynamicResolution::GetUpscalingCB()
	{
		if (!_upscalingCB) {
			_upscalingCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<UpscalingCB>());
			_upscalingCB->SetName(
				"Upscaling/DynamicResolutionConstants.Buffer");
		}
		return _upscalingCB.get();
	}

	void DynamicResolution::UpdateAndBindUpscalingCB(
		ID3D11DeviceContext* a_context,
		float2               a_screenSize,
		float2               a_renderSize)
	{
		const float cameraNear = cs::engine::GetCameraNear();
		const float cameraFar = cs::engine::GetCameraFar();

		UpscalingCB data{};
		data.ScreenSize[0] = static_cast<std::uint32_t>(a_screenSize.x);
		data.ScreenSize[1] = static_cast<std::uint32_t>(a_screenSize.y);
		data.RenderSize[0] = static_cast<std::uint32_t>(a_renderSize.x);
		data.RenderSize[1] = static_cast<std::uint32_t>(a_renderSize.y);
		data.CameraData[0] = cameraFar;
		data.CameraData[1] = cameraNear;
		data.CameraData[2] = cameraFar - cameraNear;
		data.CameraData[3] = cameraFar * cameraNear;

		auto* upscalingCB = GetUpscalingCB();
		upscalingCB->Update(data);
		auto* buffer = upscalingCB->CB();
		a_context->CSSetConstantBuffers(0, 1, &buffer);
	}
}
