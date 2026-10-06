#pragma once
#include "Render/RenderExtents.h"

#include "Render/TemporalRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <REX/TScopeExit.h>
#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "ProviderOutputPreview.h"
#include "Render/Annotation.h"
#include "Render/ComputeScope.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/SwapChainHook.h"
#include "Render/TemporalPipeline.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "UpscalingAnchors.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	[[nodiscard]] inline bool PrepareUpscalingPassthrough(
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_privateOutput,
		ID3D11Texture2D* a_input) noexcept
	{
		if (!a_context || !a_privateOutput || !a_input ||
			a_privateOutput == a_input) {
			return false;
		}

		D3D11_TEXTURE2D_DESC outputDesc{};
		D3D11_TEXTURE2D_DESC inputDesc{};
		a_privateOutput->GetDesc(&outputDesc);
		a_input->GetDesc(&inputDesc);
		if (outputDesc.Width != inputDesc.Width ||
			outputDesc.Height != inputDesc.Height ||
			outputDesc.MipLevels != inputDesc.MipLevels ||
			outputDesc.ArraySize != inputDesc.ArraySize ||
			outputDesc.Format != inputDesc.Format ||
			outputDesc.SampleDesc.Count != inputDesc.SampleDesc.Count ||
			outputDesc.SampleDesc.Quality != inputDesc.SampleDesc.Quality) {
			return false;
		}

		cs::render::annotation::ScopedEvent annotationScope(
			"Upscaling/PreparePassthrough");
		cs::engine::CopyResourcePreservingOM(
			a_context, a_privateOutput, a_input);
		return true;
	}

	[[nodiscard]] inline bool PublishUpscalingOutput(
		ID3D11DeviceContext* a_context,
		ID3D11Resource* a_frameBuffer,
		ID3D11Resource* a_providerOutput,
		bool a_providerSucceeded) noexcept
	{
		if (!a_providerSucceeded || !a_context || !a_frameBuffer || !a_providerOutput ||
			a_frameBuffer == a_providerOutput) {
			return false;
		}

		cs::render::annotation::ScopedEvent annotationScope(
			"Upscaling/PublishOutput");
		cs::engine::CopyResourcePreservingOM(a_context, a_frameBuffer, a_providerOutput);
		return true;
	}
}

namespace cs::render::renderer_detail
{
	using namespace cs::features;

	inline auto* L = cs::log::Get("cs.feature.upscaling");

	constexpr const wchar_t* kEncodeTexturesPath = L"Data\\Shaders\\FO4\\Upscaling\\EncodeTexturesCS.hlsl";
	constexpr const wchar_t* kDepthRefractionUpscalePath = L"Data\\Shaders\\FO4\\Upscaling\\DepthRefractionUpscalePS.hlsl";
	constexpr const wchar_t* kUpscaleVSPath = L"Data\\Shaders\\FO4\\Upscaling\\UpscaleVS.hlsl";
	constexpr const wchar_t* kSpatialFallbackPath =
		L"Data\\Shaders\\Upscaling\\SpatialFallbackPS.hlsl";
	constexpr const wchar_t* kCopyDepthForFrameGenerationPath =
		L"Data\\Shaders\\FrameGeneration\\CopyDepthForFrameGenerationCS.hlsl";

	// kMainTemp supplies post-alpha color for first-person alpha conditioning.
	constexpr auto kSceneColorTarget = cs::engine::RenderTarget::kMainTemp;
	constexpr auto kPreAlphaColorTarget = cs::engine::RenderTarget::kMain;
	constexpr auto kMotionVectorTarget = cs::engine::RenderTarget::kMotionVectors;
	constexpr auto kNormalsTarget = cs::engine::RenderTarget::kGbufferNormal;
	constexpr auto kRefractionNormalTarget = cs::engine::RenderTarget::kRefractionNormal;

	inline std::string_view UpscaleMethodName(TemporalRenderer::UpscaleMethod a_method) noexcept
	{
		switch (a_method) {
		case TemporalRenderer::UpscaleMethod::kNONE:
			return "None";
		case TemporalRenderer::UpscaleMethod::kTAA:
			return "TAA";
		case TemporalRenderer::UpscaleMethod::kFSR:
			return "FSR 3";
		case TemporalRenderer::UpscaleMethod::kDLSS:
			return "DLSS";
		case TemporalRenderer::UpscaleMethod::kFSR4:
			return "FSR 4";
		}
		return "Unknown";
	}

	inline render::temporal::SuperResolutionMethod ToCoreMethod(
		TemporalRenderer::UpscaleMethod a_method) noexcept
	{
		switch (a_method) {
		case TemporalRenderer::UpscaleMethod::kTAA:
			return render::temporal::SuperResolutionMethod::kTAA;
		case TemporalRenderer::UpscaleMethod::kFSR:
			return render::temporal::SuperResolutionMethod::kFSR3;
		case TemporalRenderer::UpscaleMethod::kDLSS:
			return render::temporal::SuperResolutionMethod::kDLSS;
		case TemporalRenderer::UpscaleMethod::kFSR4:
			return render::temporal::SuperResolutionMethod::kFSR4;
		default:
			return render::temporal::SuperResolutionMethod::kNone;
		}
	}

	inline std::string_view QualityModeName(std::uint32_t a_mode) noexcept
	{
		switch (a_mode) {
		case 0:
			return "Native AA";
		case 1:
			return "Quality";
		case 2:
			return "Balanced";
		case 3:
			return "Performance";
		case 4:
			return "Ultra Performance";
		default:
			return "Unknown";
		}
	}

	inline bool TryDescribeTextureView(
		ID3D11ShaderResourceView* a_view,
		D3D11_TEXTURE2D_DESC& a_desc) noexcept
	{
		if (!a_view) {
			return false;
		}

		winrt::com_ptr<ID3D11Resource> resource;
		a_view->GetResource(resource.put());
		winrt::com_ptr<ID3D11Texture2D> texture;
		if (!resource ||
			FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.put())))) {
			return false;
		}
		texture->GetDesc(&a_desc);
		return a_desc.Width > 0 && a_desc.Height > 0;
	}

	inline std::int32_t GetJitterPhaseCount(std::int32_t a_renderWidth, std::int32_t a_displayWidth)
	{
		return static_cast<std::int32_t>(
			8.0f * std::pow(static_cast<float>(a_displayWidth) / a_renderWidth, 2.0f));
	}

	inline float Halton(std::int32_t a_index, std::int32_t a_base)
	{
		float fraction = 1.0f;
		float result = 0.0f;
		for (auto index = a_index; index > 0; index /= a_base) {
			fraction /= static_cast<float>(a_base);
			result += fraction * static_cast<float>(index % a_base);
		}
		return result;
	}

	inline void GetJitterOffset(
		float& a_outX,
		float& a_outY,
		std::int32_t a_index,
		std::int32_t a_phaseCount)
	{
		const auto sequenceIndex = (a_index % a_phaseCount) + 1;
		a_outX = Halton(sequenceIndex, 2) - 0.5f;
		a_outY = Halton(sequenceIndex, 3) - 0.5f;
	}

	inline float CalculateMipBias(float a_renderWidth, float a_screenWidth, bool a_isDLSS)
	{
		if (a_screenWidth <= 0.0f || a_renderWidth <= 0.0f) {
			return 0.0f;
		}
		return std::log2(a_renderWidth / a_screenWidth) - (a_isDLSS ? 1.0f : 0.0f);
	}

	inline void ForceViewportToRenderTargetDimensions()
	{
		// Null until InitD3D finishes; the engine dereferences it.
		if (!cs::engine::GetActiveContext()) {
			return;
		}
		RE::BSGraphics::RenderTargetManager::SetCurrentViewportForceToRenderTargetDimensions();
	}

	// ImageSpaceEffectTemporalAA::IsActive reads the graphics state's TAA flag.
	inline void SetTemporalEnabled(bool a_enabled)
	{
		if (auto* state = cs::engine::GetGraphicsState()) {
			state->SetTAAState(a_enabled ? RE::BSGraphics::TAA_STATE::kEnabled : RE::BSGraphics::TAA_STATE::kDisabled);
		}
	}

	// Engine thunks quarantine faults instead of unwinding through engine frames.
	template <class Fn>
	inline void GuardedThunkBody(const char* a_where, Fn&& a_fn) noexcept
	{
		try {
			a_fn();
			return;
		} catch (const std::exception& e) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "{} failed: {}", a_where, e.what());
		} catch (...) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "{} failed", a_where);
		}
		TemporalRenderer::GetSingleton()->QuarantineAfterException(a_where);
	}

	inline bool ViewReferencesTexture(
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

	inline bool HaveMatchingCopyContract(
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

	inline bool HaveSameTextureDescription(
		const D3D11_TEXTURE2D_DESC& a_left,
		const D3D11_TEXTURE2D_DESC& a_right) noexcept
	{
		return a_left.Width == a_right.Width &&
		       a_left.Height == a_right.Height &&
		       a_left.MipLevels == a_right.MipLevels &&
		       a_left.ArraySize == a_right.ArraySize &&
		       a_left.Format == a_right.Format &&
		       a_left.SampleDesc.Count == a_right.SampleDesc.Count &&
		       a_left.SampleDesc.Quality == a_right.SampleDesc.Quality &&
		       a_left.Usage == a_right.Usage &&
		       a_left.BindFlags == a_right.BindFlags &&
		       a_left.CPUAccessFlags == a_right.CPUAccessFlags &&
		       a_left.MiscFlags == a_right.MiscFlags;
	}

	enum class FrameBufferTextureFailure : std::uint8_t
	{
		kNone,
		kNoRTV,
		kNoTexture
	};

	inline bool HasSDRUpscalingContract(const D3D11_TEXTURE2D_DESC& a_desc) noexcept
	{
		return a_desc.Width > 0 &&
		       a_desc.Height > 0 &&
		       a_desc.MipLevels == 1 &&
		       a_desc.ArraySize == 1 &&
		       a_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
		       a_desc.SampleDesc.Count == 1 &&
		       a_desc.SampleDesc.Quality == 0;
	}

	inline bool TryGetFrameBufferTexture(
		winrt::com_ptr<ID3D11Texture2D>& a_texture,
		D3D11_TEXTURE2D_DESC& a_desc,
		FrameBufferTextureFailure* a_failure = nullptr) noexcept
	{
		if (a_failure) {
			*a_failure = FrameBufferTextureFailure::kNone;
		}
		auto* rtv = cs::engine::GetRenderTargetRTV(cs::engine::RenderTarget::kFrameBuffer);
		if (!rtv) {
			if (a_failure) {
				*a_failure = FrameBufferTextureFailure::kNoRTV;
			}
			return false;
		}

		winrt::com_ptr<ID3D11Resource> resource;
		rtv->GetResource(resource.put());
		if (!resource || FAILED(resource->QueryInterface(IID_PPV_ARGS(a_texture.put())))) {
			if (a_failure) {
				*a_failure = FrameBufferTextureFailure::kNoTexture;
			}
			return false;
		}

		a_texture->GetDesc(&a_desc);
		return true;
	}

	inline bool MatchesTextureContract(
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

	inline std::uint64_t GetEngineFrame() noexcept
	{
		auto* state = cs::engine::GetGraphicsState();
		return state ? state->frameCount : 0;
	}

}
