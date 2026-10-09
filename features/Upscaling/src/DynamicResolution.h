#pragma once

#include <d3d11.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>

#include <winrt/base.h>

#include "Render/Engine.h"
#include "Utils/CSBuffer.h"

namespace cs::features
{
	class DynamicResolution
	{
	public:
		struct ProxyTexture
		{
			ID3D11ShaderResourceView* view = nullptr;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
		};

		struct UpscalingCB
		{
			std::uint32_t ScreenSize[2];
			std::uint32_t RenderSize[2];
			float CameraData[4];
		};

		void UpdateRenderTargets(float a_widthRatio, float a_heightRatio);

		void OverrideRenderTargets(std::initializer_list<cs::engine::RenderTarget> a_toCopy = {});
		// An empty list copies every proxied target back.
		void ResetRenderTargets(std::initializer_list<cs::engine::RenderTarget> a_toCopy = {});

		void OverrideDepth(bool a_doCopy = true);
		void ResetDepth();

		// Ratio-1 draws into render-resolution proxies so stock TexCoord spans [0, 1].
		[[nodiscard]] bool ProxyPassAvailable() const;
		bool BeginProxyPass(std::initializer_list<cs::engine::RenderTarget> a_toProxy);
		// Copies only a_fromProxy back to the engine targets.
		void EndProxyPass(std::initializer_list<cs::engine::RenderTarget> a_fromProxy);
		// Pooled targets: the platform slot exists only between acquire and release.
		void ProxyAcquiredRenderTarget(cs::engine::RenderTarget a_target);
		// Must precede the engine release; the slot is unmapped afterwards.
		void ReleaseAcquiredRenderTarget(cs::engine::RenderTarget a_target);

		void Release();

		[[nodiscard]] bool HasProxies() const noexcept { return _hasProxies; }
		[[nodiscard]] ProxyTexture GetProxyTexture(cs::engine::RenderTarget a_target) const noexcept;

	private:
		void UpdateRenderTarget(cs::engine::RenderTarget a_target, float a_widthRatio, float a_heightRatio);
		// Snapshots the engine target and mirrors its fields into the proxy.
		void CaptureOriginal(cs::engine::RenderTarget a_target, const RE::BSGraphics::RenderTarget& a_engineTarget);
		void OverrideRenderTarget(cs::engine::RenderTarget a_target, bool a_doCopy);
		void ResetRenderTarget(cs::engine::RenderTarget a_target, bool a_doCopy);
		bool CopyDepth();
		void ReleaseProxy(cs::engine::RenderTarget a_target);
		void RestoreEngineSlot(cs::engine::RenderTarget a_target);
		bool IsProxyTexture(const void* a_texture) const noexcept;
		void AbortProxyPass();

		ID3D11ComputeShader* GetOverrideDepthCS();
		ID3D11ComputeShader* GetOverrideLinearDepthCS();
		cs::buffer::ConstantBuffer* GetUpscalingCB();
		bool UpdateAndBindUpscalingCB(
			ID3D11DeviceContext* a_context,
			float2 a_screenSize,
			float2 a_renderSize);

		// Indexed by logical ID.
		RE::BSGraphics::RenderTarget originalRenderTargets[static_cast<std::size_t>(cs::engine::RenderTarget::kCount)]{};
		RE::BSGraphics::RenderTarget proxyRenderTargets[static_cast<std::size_t>(cs::engine::RenderTarget::kCount)]{};
		RE::BSGraphics::RenderTargetProperties originalRenderTargetData[100]{};

		ID3D11ShaderResourceView* _originalDepthView = nullptr;
		std::unique_ptr<cs::buffer::Texture2D> _depthOverrideTexture;

		winrt::com_ptr<ID3D11ComputeShader> _overrideDepthCS;
		winrt::com_ptr<ID3D11ComputeShader> _overrideLinearDepthCS;
		std::unique_ptr<cs::buffer::ConstantBuffer> _upscalingCB;

		float _previousWidthRatio = -1.0f;
		float _previousHeightRatio = -1.0f;
		std::uint64_t _depthCopyFrame = std::numeric_limits<std::uint64_t>::max();
		bool _hasProxies = false;
		bool _renderTargetsOverridden = false;
		bool _depthOverridden = false;
		bool _proxyPass = false;
		float _proxyPassWidthRatio = 1.0f;
		float _proxyPassHeightRatio = 1.0f;
	};
}
