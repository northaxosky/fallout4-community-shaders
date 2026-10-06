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

		// Engine render targets that hold the world and HDR imagespace chain.
		using cs::engine::RenderTarget;
		constexpr RenderTarget kProxiedTargets[] = {
			RenderTarget::kGbufferNormal,
			RenderTarget::kGbufferMetadata,
			RenderTarget::kGbufferMaterial,
			RenderTarget::kAmbientOcclusion,
			RenderTarget::kGbufferEmissive,
			RenderTarget::kDiffuseBufferA,
			RenderTarget::kSpecularBufferA,
			RenderTarget::kAmbientOcclusionHalf,
			RenderTarget::kMain,
			RenderTarget::kSSLRBlurV,
			RenderTarget::kDiffuseBufferB,
			RenderTarget::kSpecularBufferB,
			RenderTarget::kMainTemp,
			RenderTarget::kMotionVectors,
			RenderTarget::kRefractionNormal,
			RenderTarget::kUIDownscaled,
			RenderTarget::kUIDownscaledComposite,
			RenderTarget::kGbufferAlbedo,
			RenderTarget::kSSLRRayStart,
			RenderTarget::kSSLRSurfaceDepth,
			RenderTarget::kSSLRRayResult,
			RenderTarget::kSSLRBlurH,
			RenderTarget::kLuminanceDownscale,
			RenderTarget::kMainVerticalBlur,
			RenderTarget::kHdrImagespaceAux
		};

		constexpr std::size_t Index(RenderTarget a_target) noexcept
		{
			return static_cast<std::size_t>(a_target);
		}

		bool Contains(std::initializer_list<RenderTarget> a_targets, RenderTarget a_target) noexcept
		{
			return std::find(a_targets.begin(), a_targets.end(), a_target) != a_targets.end();
		}

		constexpr const wchar_t* kOverrideDepthPath = L"Data\\Shaders\\Upscaling\\OverrideDepthCS.hlsl";
		constexpr const wchar_t* kOverrideLinearDepthPath = L"Data\\Shaders\\Upscaling\\OverrideLinearDepthCS.hlsl";

		template <class T>
		void SafeRelease(T*& a_ptr)
		{
			if (a_ptr) {
				a_ptr->Release();
				a_ptr = nullptr;
			}
		}
	}

	void DynamicResolution::ReleaseProxy(RenderTarget a_target)
	{
		auto& proxy = proxyRenderTargets[Index(a_target)];
		SafeRelease(proxy.uaView);
		SafeRelease(proxy.srView);
		SafeRelease(proxy.rtView);
		SafeRelease(proxy.texture);
		proxy = {};
	}

	DynamicResolution::ProxyTexture DynamicResolution::GetProxyTexture(RenderTarget a_target) const noexcept
	{
		if (Index(a_target) >= std::size(proxyRenderTargets)) {
			return {};
		}

		const auto& proxy = proxyRenderTargets[Index(a_target)];
		auto* texture = reinterpret_cast<ID3D11Texture2D*>(proxy.texture);
		auto* view = reinterpret_cast<ID3D11ShaderResourceView*>(proxy.srView);
		if (!texture || !view) {
			return {};
		}

		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		return { view, desc.Width, desc.Height };
	}

	void DynamicResolution::UpdateRenderTarget(RenderTarget a_target, float a_widthRatio, float a_heightRatio)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* engineTarget = cs::engine::ResolveRenderTarget(a_target);
		const auto index = Index(a_target);
		originalRenderTargets[index] = {};
		if (!rendererData || !engineTarget) {
			return;
		}

		// Keep the engine's copyTexture/copySRView pointers so a swap-in never nulls them.
		originalRenderTargets[index] = *engineTarget;
		proxyRenderTargets[index] = originalRenderTargets[index];
		proxyRenderTargets[index].texture = nullptr;
		proxyRenderTargets[index].rtView = nullptr;
		proxyRenderTargets[index].srView = nullptr;
		proxyRenderTargets[index].uaView = nullptr;

		auto& original = originalRenderTargets[index];
		auto& proxy = proxyRenderTargets[index];

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
		const auto baseName = std::format("Upscaling/DynamicResolutionProxy[{}]", index);
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

		for (const auto target : kProxiedTargets) {
			ReleaseProxy(target);
			UpdateRenderTarget(target, a_widthRatio, a_heightRatio);
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

	void DynamicResolution::OverrideRenderTarget(RenderTarget a_target, bool a_doCopy)
	{
		const auto index = Index(a_target);
		auto* engineTarget = cs::engine::ResolveRenderTarget(a_target);
		if (!engineTarget || !originalRenderTargets[index].texture || !proxyRenderTargets[index].texture) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		*engineTarget = proxyRenderTargets[index];

		if (!a_doCopy) {
			return;
		}

		auto* proxyTexture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture);
		auto* originalTexture = reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture);

		D3D11_TEXTURE2D_DESC proxyDesc{};
		proxyTexture->GetDesc(&proxyDesc);

		D3D11_BOX box{ 0, 0, 0, proxyDesc.Width, proxyDesc.Height, 1 };
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		context->CopySubresourceRegion(proxyTexture, 0, 0, 0, 0, originalTexture, 0, &box);
	}

	void DynamicResolution::ResetRenderTarget(RenderTarget a_target, bool a_doCopy)
	{
		const auto index = Index(a_target);
		auto* engineTarget = cs::engine::ResolveRenderTarget(a_target);
		if (!engineTarget || !originalRenderTargets[index].texture || !proxyRenderTargets[index].texture) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();

		if (a_doCopy) {
			auto* proxyTexture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture);
			auto* originalTexture = reinterpret_cast<ID3D11Texture2D*>(originalRenderTargets[index].texture);

			D3D11_TEXTURE2D_DESC proxyDesc{};
			proxyTexture->GetDesc(&proxyDesc);

			D3D11_BOX box{ 0, 0, 0, proxyDesc.Width, proxyDesc.Height, 1 };
			auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
			context->CopySubresourceRegion(originalTexture, 0, 0, 0, 0, proxyTexture, 0, &box);
		}

		*engineTarget = originalRenderTargets[index];
	}

	void DynamicResolution::OverrideRenderTargets(std::initializer_list<RenderTarget> a_toCopy)
	{
		if (!_hasProxies) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope(
			"Upscaling/DynamicResolution/CopyToProxies");

		for (const auto target : kProxiedTargets) {
			OverrideRenderTarget(target, Contains(a_toCopy, target));
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
			for (const auto target : kProxiedTargets) {
				auto* originalSRV = reinterpret_cast<ID3D11ShaderResourceView*>(originalRenderTargets[Index(target)].srView);
				auto* proxySRV = reinterpret_cast<ID3D11ShaderResourceView*>(proxyRenderTargets[Index(target)].srView);
				if (boundSRVs[slot] == originalSRV && proxySRV) {
					context->PSSetShaderResources(slot, 1, &proxySRV);
					break;
				}
			}
			boundSRVs[slot]->Release();
		}

		renderTargetManager->SetUseDynamicResolutionViewportAsDefaultViewport(false);
		_renderTargetsOverridden = true;
	}

	void DynamicResolution::ResetRenderTargets(std::initializer_list<RenderTarget> a_toCopy)
	{
		if (!_renderTargetsOverridden) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope(
			"Upscaling/DynamicResolution/CopyFromProxies");

		for (const auto target : kProxiedTargets) {
			ResetRenderTarget(target, a_toCopy.size() == 0 || Contains(a_toCopy, target));
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
			for (const auto target : kProxiedTargets) {
				auto* originalSRV = reinterpret_cast<ID3D11ShaderResourceView*>(originalRenderTargets[Index(target)].srView);
				auto* proxySRV = reinterpret_cast<ID3D11ShaderResourceView*>(proxyRenderTargets[Index(target)].srView);
				if (boundSRVs[slot] == proxySRV && originalSRV) {
					context->PSSetShaderResources(slot, 1, &originalSRV);
					break;
				}
			}
			boundSRVs[slot]->Release();
		}

		renderTargetManager->SetUseDynamicResolutionViewportAsDefaultViewport(true);
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

		auto* mainDepth = cs::engine::ResolveDepthStencilTarget(cs::engine::DepthStencilTarget::kMain);
		if (!mainDepth) {
			return;
		}
		_originalDepthView = reinterpret_cast<ID3D11ShaderResourceView*>(mainDepth->srViewDepth);

		if (a_doCopy) {
			const std::uint64_t frame = cs::engine::GetGraphicsState() ? cs::engine::GetGraphicsState()->frameCount : 0;
			if (_depthCopyFrame != frame) {
				if (!CopyDepth())
					return;
				_depthCopyFrame = frame;
			}
		}

		mainDepth->srViewDepth =
			reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(_depthOverrideTexture->srv.get());
		_depthOverridden = true;
	}

	void DynamicResolution::ResetDepth()
	{
		if (!_depthOverridden) {
			return;
		}
		_depthOverridden = false;

		if (auto* mainDepth = cs::engine::ResolveDepthStencilTarget(cs::engine::DepthStencilTarget::kMain)) {
			mainDepth->srViewDepth = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(_originalDepthView);
		}
	}

	bool DynamicResolution::CopyDepth()
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* state = cs::engine::GetGraphicsState();
		if (!context || !state || !_depthOverrideTexture) {
			return false;
		}

		auto* depthSRV = cs::engine::GetDepthStencilDepthSRV(cs::engine::DepthStencilTarget::kMain);
		auto* linearDepthUAV = cs::engine::GetRenderTargetUAV(cs::engine::RenderTarget::kMainDepthMips);
		auto* depthUAV = _depthOverrideTexture->uav.get();
		if (!depthSRV || !linearDepthUAV || !depthUAV) {
			return false;
		}

		auto* linearDepthCS = GetOverrideLinearDepthCS();
		auto* overrideDepthCS = GetOverrideDepthCS();
		if (!linearDepthCS || !overrideDepthCS) {
			return false;
		}

		const float2 screenSize{ static_cast<float>(state->screenWidth), static_cast<float>(state->screenHeight) };
		auto* renderTargetManager = cs::engine::GetRenderTargetManager();
		const float widthRatio = renderTargetManager ? renderTargetManager->GetDynamicWidthRatio() : 1.0f;
		const float heightRatio = renderTargetManager ? renderTargetManager->GetDynamicHeightRatio() : 1.0f;
		const float2 renderSize{ screenSize.x * widthRatio, screenSize.y * heightRatio };

		// Unbind the output-merger before sampling the engine depth in compute.
		cs::engine::OMScope omScope(context);
		cs::ComputeScope computeScope(context);

		if (!UpdateAndBindUpscalingCB(context, screenSize, renderSize))
			return false;

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
		return true;
	}

	void DynamicResolution::Release()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* renderTargetManager = cs::engine::GetRenderTargetManager();

		// Unwind an in-progress render-target override so no engine state is left mutated.
		if (_renderTargetsOverridden) {
			if (rendererData) {
				for (const auto target : kProxiedTargets) {
					const auto index = Index(target);
					auto* engineTarget = cs::engine::ResolveRenderTarget(target);
					auto* proxyTexture = reinterpret_cast<ID3D11Texture2D*>(proxyRenderTargets[index].texture);
					auto* liveTexture = engineTarget ? reinterpret_cast<ID3D11Texture2D*>(engineTarget->texture) : nullptr;
					if (proxyTexture && liveTexture == proxyTexture && originalRenderTargets[index].texture) {
						*engineTarget = originalRenderTargets[index];
					}
				}
			}
			if (renderTargetManager) {
				for (int i = 0; i < 100; i++) {
					renderTargetManager->renderTargetData[i] = originalRenderTargetData[i];
				}
				renderTargetManager->SetUseDynamicResolutionViewportAsDefaultViewport(true);
			}
			_renderTargetsOverridden = false;
		}

		// Restore the engine depth SRV if the override is still applied.
		if (_depthOverridden) {
			if (auto* mainDepth = cs::engine::ResolveDepthStencilTarget(cs::engine::DepthStencilTarget::kMain)) {
				mainDepth->srViewDepth = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(_originalDepthView);
			}
			_depthOverridden = false;
		}

		for (const auto target : kProxiedTargets) {
			ReleaseProxy(target);
			originalRenderTargets[Index(target)] = {};
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
		if (!_overrideDepthCS) {
			L->debug("Compiling OverrideDepthCS.hlsl");
			_overrideDepthCS.attach(
				static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(kOverrideDepthPath, {}, "cs_5_0")));
			if (_overrideDepthCS) {
				cs::render::annotation::SetName(
					_overrideDepthCS.get(), "Upscaling/OverrideDepth.CS");
			}
		}
		return _overrideDepthCS.get();
	}

	ID3D11ComputeShader* DynamicResolution::GetOverrideLinearDepthCS()
	{
		if (!_overrideLinearDepthCS) {
			L->debug("Compiling OverrideLinearDepthCS.hlsl");
			_overrideLinearDepthCS.attach(
				static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(kOverrideLinearDepthPath, {}, "cs_5_0")));
			if (_overrideLinearDepthCS) {
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

	bool DynamicResolution::UpdateAndBindUpscalingCB(
		ID3D11DeviceContext* a_context,
		float2 a_screenSize,
		float2 a_renderSize)
	{
		const auto camera = cs::engine::GetWorldCameraRecord();
		if (!camera)
			return false;
		const auto depth = cs::engine::GetCameraDepthParameters(*camera);

		UpscalingCB data{};
		data.ScreenSize[0] = static_cast<std::uint32_t>(a_screenSize.x);
		data.ScreenSize[1] = static_cast<std::uint32_t>(a_screenSize.y);
		data.RenderSize[0] = static_cast<std::uint32_t>(a_renderSize.x);
		data.RenderSize[1] = static_cast<std::uint32_t>(a_renderSize.y);
		data.CameraData[0] = depth.x;
		data.CameraData[1] = depth.y;
		data.CameraData[2] = depth.z;
		data.CameraData[3] = depth.w;

		auto* upscalingCB = GetUpscalingCB();
		upscalingCB->Update(data);
		auto* buffer = upscalingCB->CB();
		a_context->CSSetConstantBuffers(0, 1, &buffer);
		return true;
	}
}
