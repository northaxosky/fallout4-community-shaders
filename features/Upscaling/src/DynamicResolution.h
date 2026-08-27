#pragma once

#include <d3d11.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <winrt/base.h>

#include "Utils/CSBuffer.h"

namespace cs::features
{
	// Physically render-resolution proxy render targets that keep the world and the HDR
	// imagespace chain at render resolution until the upscaler resolves the frame.
	class DynamicResolution
	{
	public:
		struct UpscalingCB
		{
			std::uint32_t ScreenSize[2];
			std::uint32_t RenderSize[2];
			float         CameraData[4];
		};

		// Rebuilds proxies and the depth-override texture whenever the scale changes.
		void UpdateRenderTargets(float a_widthRatio, float a_heightRatio);

		// Swaps the patched targets to render-resolution proxies; listed indices copy the sub-rect.
		void OverrideRenderTargets(const std::vector<int>& a_indicesToCopy = {});
		void ResetRenderTargets(const std::vector<int>& a_indicesToCopy = {});

		// Swaps the main depth SRV to a compact render-resolution depth texture.
		void OverrideDepth(bool a_doCopy = true);
		void ResetDepth();

		// Restores engine pointers and releases every proxy resource; call on device or target change.
		void Release();

		[[nodiscard]] bool HasProxies() const noexcept { return _hasProxies; }

	private:
		void UpdateRenderTarget(int a_index, float a_widthRatio, float a_heightRatio);
		void OverrideRenderTarget(int a_index, bool a_doCopy);
		void ResetRenderTarget(int a_index, bool a_doCopy);
		void CopyDepth();
		void ReleaseProxy(int a_index);

		ID3D11ComputeShader*         GetOverrideDepthCS();
		ID3D11ComputeShader*         GetOverrideLinearDepthCS();
		cs::buffer::ConstantBuffer*  GetUpscalingCB();
		void                         UpdateAndBindUpscalingCB(
									 ID3D11DeviceContext* a_context,
									 float2               a_screenSize,
									 float2               a_renderSize);

		RE::BSGraphics::RenderTarget           originalRenderTargets[101]{};
		RE::BSGraphics::RenderTarget           proxyRenderTargets[101]{};
		RE::BSGraphics::RenderTargetProperties originalRenderTargetData[100]{};

		ID3D11ShaderResourceView*             _originalDepthView = nullptr;
		std::unique_ptr<cs::buffer::Texture2D> _depthOverrideTexture;

		winrt::com_ptr<ID3D11ComputeShader>          _overrideDepthCS;
		winrt::com_ptr<ID3D11ComputeShader>          _overrideLinearDepthCS;
		std::unique_ptr<cs::buffer::ConstantBuffer>  _upscalingCB;

		float         _previousWidthRatio = -1.0f;
		float         _previousHeightRatio = -1.0f;
		std::uint64_t _depthCopyFrame = std::numeric_limits<std::uint64_t>::max();
		bool          _hasProxies = false;
		bool          _renderTargetsOverridden = false;
		bool          _depthOverridden = false;
		bool          _overrideDepthCSFailed = false;
		bool          _overrideLinearDepthCSFailed = false;
	};
}
