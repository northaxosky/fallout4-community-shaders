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

		void OverrideRenderTargets(std::initializer_list<RE::BSGraphics::RenderTargetID> a_toCopy = {});
		// An empty list copies every proxied target back.
		void ResetRenderTargets(std::initializer_list<RE::BSGraphics::RenderTargetID> a_toCopy = {});

		void OverrideDepth(bool a_doCopy = true);
		void ResetDepth();

		// Ratio-1 draws into render-resolution proxies so stock TexCoord spans [0, 1].
		[[nodiscard]] bool ProxyPassAvailable() const;
		// kUnit: stock imagespace UV spans [0, 2], not the baked 2 * render ratio.
		enum class ProxyTriangle : std::uint8_t
		{
			kEngine,
			kUnit
		};
		bool BeginProxyPass(std::initializer_list<RE::BSGraphics::RenderTargetID> a_toProxy, ProxyTriangle a_triangle = ProxyTriangle::kEngine);
		// Copies only a_fromProxy back to the engine targets.
		void EndProxyPass(std::initializer_list<RE::BSGraphics::RenderTargetID> a_fromProxy);
		// Pooled targets: the platform slot exists only between acquire and release.
		void ProxyAcquiredRenderTarget(RE::BSGraphics::RenderTargetID a_target);
		// Must precede the engine release; the slot is unmapped afterwards.
		void ReleaseAcquiredRenderTarget(RE::BSGraphics::RenderTargetID a_target);

		void Release();

		// Proxy holds the source's current contents until either side may change.
		void InvalidateProxy(RE::BSGraphics::RenderTargetID a_target) noexcept;

		[[nodiscard]] bool HasProxies() const noexcept { return _hasProxies; }
		[[nodiscard]] ProxyTexture GetProxyTexture(RE::BSGraphics::RenderTargetID a_target) const noexcept;

	private:
		void UpdateRenderTarget(RE::BSGraphics::RenderTargetID a_target, float a_widthRatio, float a_heightRatio);
		// Snapshots the engine target and mirrors its fields into the proxy.
		void CaptureOriginal(RE::BSGraphics::RenderTargetID a_target, const RE::BSGraphics::RenderTarget& a_engineTarget);
		void OverrideRenderTarget(RE::BSGraphics::RenderTargetID a_target, bool a_doCopy);
		void ResetRenderTarget(RE::BSGraphics::RenderTargetID a_target, bool a_doCopy);
		bool CopyDepth();
		void ReleaseProxy(RE::BSGraphics::RenderTargetID a_target);
		void RestoreEngineSlot(RE::BSGraphics::RenderTargetID a_target);
		bool IsProxyTexture(const void* a_texture) const noexcept;
		void AbortProxyPass();
		void InvalidateAllProxies() noexcept;

		ID3D11ComputeShader* GetOverrideDepthCS();
		ID3D11ComputeShader* GetOverrideLinearDepthCS();
		cs::buffer::ConstantBuffer* GetUpscalingCB();
		bool UpdateAndBindUpscalingCB(
			ID3D11DeviceContext* a_context,
			float2 a_screenSize,
			float2 a_renderSize);

		// Engine frame a cached copy was last filled; unset until Mark.
		struct FrameStamp
		{
			static constexpr std::uint32_t kNone = (std::numeric_limits<std::uint32_t>::max)();
			std::uint32_t frame = kNone;
			[[nodiscard]] bool IsCurrent(std::uint32_t a_frame) const noexcept { return frame == a_frame; }
			void Mark(std::uint32_t a_frame) noexcept { frame = a_frame; }
			void Reset() noexcept { frame = kNone; }
		};

		// Indexed by logical ID.
		RE::BSGraphics::RenderTarget originalRenderTargets[static_cast<std::size_t>(RE::BSGraphics::RenderTargetID::kCount)]{};
		RE::BSGraphics::RenderTarget proxyRenderTargets[static_cast<std::size_t>(RE::BSGraphics::RenderTargetID::kCount)]{};
		RE::BSGraphics::RenderTargetProperties originalRenderTargetData[100]{};

		ID3D11ShaderResourceView* _originalDepthView = nullptr;
		std::unique_ptr<cs::buffer::Texture2D> _depthOverrideTexture;

		winrt::com_ptr<ID3D11ComputeShader> _overrideDepthCS;
		winrt::com_ptr<ID3D11ComputeShader> _overrideLinearDepthCS;
		std::unique_ptr<cs::buffer::ConstantBuffer> _upscalingCB;

		float _previousWidthRatio = -1.0f;
		float _previousHeightRatio = -1.0f;
		FrameStamp _proxyFilledFrame[static_cast<std::size_t>(RE::BSGraphics::RenderTargetID::kCount)]{};
		FrameStamp _depthCopyFrame;
		bool _hasProxies = false;
		bool _renderTargetsOverridden = false;
		bool _depthOverridden = false;
		bool _proxyPass = false;
		cs::engine::ImageSpaceTriangleSwap _triangleSwap;
		float _proxyPassWidthRatio = 1.0f;
		float _proxyPassHeightRatio = 1.0f;
	};
}
