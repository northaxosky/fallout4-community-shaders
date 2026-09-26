#pragma once

#include "Render/TemporalRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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
#include "Render/Annotation.h"
#include "Render/ComputeScope.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/SwapChainHook.h"
#include "Render/TemporalPipeline.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "ProviderOutputPreview.h"
#include "UpscalingAnchors.h"
#include "UpscalingPublication.h"
#include "Utils/CSUtil.h"

namespace cs::render::renderer_detail
{
	using namespace cs::features;

		inline auto* L = cs::log::Get("cs.feature.upscaling");

		constexpr const wchar_t* kEncodeTexturesPath = L"Data\\Shaders\\Upscaling\\EncodeTexturesCS.hlsl";
		constexpr const wchar_t* kDepthRefractionUpscalePath = L"Data\\Shaders\\Upscaling\\DepthRefractionUpscalePS.hlsl";
		constexpr const wchar_t* kUpscaleVSPath = L"Data\\Shaders\\Upscaling\\UpscaleVS.hlsl";
		constexpr const wchar_t* kSpatialFallbackPath =
			L"Data\\Shaders\\Upscaling\\SpatialFallbackPS.hlsl";
		constexpr const wchar_t* kSSLRRaytracingPath = L"Data\\Shaders\\Upscaling\\BSImagespaceShaderSSLRRaytracing.hlsl";
		constexpr const wchar_t* kCopyDepthForFrameGenerationPath =
			L"Data\\Shaders\\FrameGeneration\\CopyDepthForFrameGenerationCS.hlsl";

		// RT4 supplies post-alpha color for first-person alpha conditioning.
		constexpr auto kSceneColorTarget = cs::engine::RenderTarget::kMainTemp;
		constexpr auto kPreAlphaColorTarget = cs::engine::RenderTarget::kMain;
		// RT29 holds full-resolution R16G16_FLOAT motion.
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

		inline std::uint32_t ScaledExtent(std::uint32_t a_extent, float a_ratio) noexcept
		{
			if (!std::isfinite(a_ratio) || a_ratio <= 0.0f) {
				return 0;
			}
			return std::min(
				a_extent,
				static_cast<std::uint32_t>(
					static_cast<double>(a_extent) * a_ratio));
		}

		struct ActiveExtentSnapshot
		{
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			float         widthRatio = 1.0f;
			float         heightRatio = 1.0f;
		};

		inline ActiveExtentSnapshot GetActiveExtent(
			std::uint32_t a_fullWidth,
			std::uint32_t a_fullHeight) noexcept
		{
			ActiveExtentSnapshot snapshot;
			if (const auto* manager = cs::engine::GetRenderTargetManager()) {
				snapshot.widthRatio = manager->GetDynamicWidthRatio();
				snapshot.heightRatio = manager->GetDynamicHeightRatio();
			}
			snapshot.width = ScaledExtent(a_fullWidth, snapshot.widthRatio);
			snapshot.height = ScaledExtent(a_fullHeight, snapshot.heightRatio);
			return snapshot;
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
			using func_t = void (*)();
			static REL::Relocation<func_t> func{ REL::ID({
				upscaling_anchors::kForceViewportToRenderTargetDimensions[0],
				upscaling_anchors::kForceViewportToRenderTargetDimensions[1],
				upscaling_anchors::kForceViewportToRenderTargetDimensions[2] }) };
			func();
		}

		// Fallout 4 controls TAA through this global and the graphics state.
		inline void SetTemporalEnabled(bool a_enabled)
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

		// Validate all anchors first to avoid partial hook installation.
		inline bool IsCallSiteTargeting(std::uintptr_t a_site, std::uintptr_t a_expectedTarget)
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
