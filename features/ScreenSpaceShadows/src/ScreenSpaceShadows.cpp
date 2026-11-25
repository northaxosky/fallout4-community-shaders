#include "ScreenSpaceShadows.h"
#include "Render/SharedDataLayout.h"
#include "Render/SubstrateSlots.h"

#include <DearModdingUI/Client.h>
#include <DirectXMath.h>
#include <d3d11.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/CanonicalDepth.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/RenderExtents.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"
#include "Render/ShaderInjection.h"
#include "ScreenSpaceShadowsMath.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "World/Sky.h"

#pragma warning(push)
#pragma warning(disable: 4244 4838)
#include "bend_sss_cpu.h"
#pragma warning(pop)

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.screenspaceshadows");

		constexpr const wchar_t* kRaymarchPath = L"Data\\Shaders\\ScreenSpaceShadows\\RaymarchCS.hlsl";

		// Engine depth uses near=0, far=1.
		constexpr float kFarDepthValue = 1.0f;
		constexpr float kNearDepthValue = 0.0f;

		std::unique_ptr<cs::buffer::Texture2D> CreateShadowTexture(
			std::uint32_t a_width, std::uint32_t a_height, DXGI_FORMAT a_format, std::string_view a_name)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = a_format;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			auto texture = std::make_unique<cs::buffer::Texture2D>(desc);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = a_format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			texture->CreateSRV(srv);
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = a_format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			texture->CreateUAV(uav);
			texture->SetName(std::format("{}.Texture", a_name), std::format("{}.SRV", a_name), std::format("{}.UAV", a_name));
			return texture;
		}

	}

	ScreenSpaceShadows* ScreenSpaceShadows::GetSingleton()
	{
		static ScreenSpaceShadows instance;
		return &instance;
	}

	std::span<const FeatureDebugView>
	ScreenSpaceShadows::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "shadow_mask",
				.label = "Shadow-mask preview",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const ScreenSpaceShadows&>(a_feature)
			            .GetShadowMaskDebugTexture();
				} }
		};
		return views;
	}

	void ScreenSpaceShadows::SetDebugView(std::string_view a_view) noexcept
	{
		_debugPreviewEnabled.store(
			a_view == "shadow_mask",
			std::memory_order_release);
	}

	FeatureDebugTexture
	ScreenSpaceShadows::GetShadowMaskDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText = "Mask not allocated."
		};
		if (!_debugPreviewEnabled.load(std::memory_order_acquire) || !_maskTexture || !_maskTexture->srv || _allocWidth == 0 || _allocHeight == 0) {
			return texture;
		}
		texture.texture = _maskTexture->srv.get();
		texture.width = _allocWidth;
		texture.height = _allocHeight;
		return texture;
	}

	bool ScreenSpaceShadows::Configure(const toml::table& a_config, std::string& a_error)
	{
		return _live.Configure(a_config, a_error);
	}

	void ScreenSpaceShadows::Load()
	{
		// FO4: directional consumers execute inside DeferredLightsImpl under shader ownership.
		const bool replacementRegistered =
			cs::engine::RegisterFeatureShaderBindings(*this);
		if (!replacementRegistered) {
			FailLoad("Failed to register the ScreenSpaceShadows BSDF light shader replacement");
			return;
		}

		cs::engine::RegisterPreDeferredLightsImpl(*this, [] {
			ScreenSpaceShadows::GetSingleton()->OnPreDeferredLights();
			ScreenSpaceShadows::GetSingleton()->Prepass();
		});
		_registrationsReady.store(true, std::memory_order_release);
		L->info(
			"Registered deferred-lights callbacks (enabled={}).",
			IsEnabled());
	}

	void ScreenSpaceShadows::CreateMaskTexture(std::uint32_t a_width, std::uint32_t a_height)
	{
		_maskTexture = CreateShadowTexture(a_width, a_height, DXGI_FORMAT_R8G8_UNORM, "ScreenSpaceShadows/Mask");
		_allocWidth = a_width;
		_allocHeight = a_height;
	}

	void ScreenSpaceShadows::PublishRequiredMaskExtent(render::Extent a_required)
	{
		_requiredMaskExtent = a_required;
		_requiredMaskWidthTelemetry.store(a_required.width, std::memory_order_relaxed);
		_requiredMaskHeightTelemetry.store(a_required.height, std::memory_order_relaxed);
	}

	bool ScreenSpaceShadows::TryGetMaskExtents(
		render::Extent& a_required,
		render::Extent& a_allocation) const
	{
		const auto resolution = render::GetRenderResolution();
		if (!resolution.render.IsValid())
			return false;
		a_required = resolution.render;
		a_allocation = resolution.output;
		return true;
	}

	bool ScreenSpaceShadows::CreateWhiteFallback(
		ID3D11Device* a_device,
		render::Extent a_allocation)
	{
		if (!a_device || a_allocation.width == 0 || a_allocation.height == 0 || a_allocation.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || a_allocation.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || a_allocation.width > std::numeric_limits<std::size_t>::max() / a_allocation.height) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::err,
					"Rejected invalid full-extent white SSS fallback allocation ({}x{}).",
					a_allocation.width,
					a_allocation.height);
			}
			return false;
		}

		const auto pixelCount =
			static_cast<std::size_t>(a_allocation.width) * a_allocation.height;
		std::vector<std::uint8_t> white;
		try {
			white.assign(
				pixelCount * 2,
				sss_resources::kWhiteR8Unorm);
		} catch (...) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::err,
					"Failed to allocate CPU initialization bytes for the full-extent white SSS fallback ({}x{}).",
					a_allocation.width,
					a_allocation.height);
			}
			return false;
		}

		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = a_allocation.width;
		textureDesc.Height = a_allocation.height;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA initialData{};
		initialData.pSysMem = white.data();
		initialData.SysMemPitch = a_allocation.width * 2;
		initialData.SysMemSlicePitch =
			static_cast<UINT>(pixelCount * 2);

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_device->CreateTexture2D(
				&textureDesc,
				&initialData,
				texture.put()))) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::err,
					"Failed to create the full-extent white SSS fallback texture ({}x{}).",
					a_allocation.width,
					a_allocation.height);
			}
			return false;
		}
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		if (FAILED(a_device->CreateShaderResourceView(
				texture.get(),
				nullptr,
				srv.put()))) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(
					L,
					5000,
					spdlog::level::err,
					"Failed to create the full-extent white SSS fallback SRV ({}x{}).",
					a_allocation.width,
					a_allocation.height);
			}
			return false;
		}
		cs::render::annotation::SetName(
			texture.get(), "ScreenSpaceShadows/WhiteFallback.Texture");
		cs::render::annotation::SetName(
			srv.get(), "ScreenSpaceShadows/WhiteFallback.SRV");
		_whiteFallbackSRV = std::move(srv);
		(void)_whiteFallbackExtent.CompleteAllocation(
			a_allocation,
			true);
		return true;
	}

	bool ScreenSpaceShadows::EnsureWhiteFallback(
		ID3D11Device* a_device,
		render::Extent a_required,
		render::Extent a_allocation)
	{
		const sss_resources::FallbackAllocationKey key{
			a_allocation,
			_deviceGeneration
		};
		const bool needsAllocation =
			!_whiteFallbackSRV || _whiteFallbackExtent.allocated != a_allocation;
		if (!needsAllocation) {
			if (_whiteFallbackBackoff.HasFailure() && !_whiteFallbackBackoff.IsLatchedFor(key)) {
				_whiteFallbackBackoff.RecordSuccess();
			}
		} else if (_whiteFallbackBackoff.ShouldAttempt(key)) {
			_whiteFallbackAllocationAttempts.fetch_add(
				1,
				std::memory_order_relaxed);
			if (CreateWhiteFallback(a_device, a_allocation)) {
				_whiteFallbackBackoff.RecordSuccess();
			} else {
				_whiteFallbackBackoff.RecordFailure(key);
				_whiteFallbackAllocationFailures.fetch_add(
					1,
					std::memory_order_relaxed);
			}
		}
		const bool ready =
			_whiteFallbackSRV && _whiteFallbackExtent.IsCompatible(a_required);
		_whiteFallbackFailureLatchedTelemetry.store(
			needsAllocation && _whiteFallbackBackoff.IsLatchedFor(key),
			std::memory_order_relaxed);
		_whiteFallbackWidthTelemetry.store(
			_whiteFallbackExtent.allocated.width,
			std::memory_order_relaxed);
		_whiteFallbackHeightTelemetry.store(
			_whiteFallbackExtent.allocated.height,
			std::memory_order_relaxed);
		_whiteFallbackReady.store(ready, std::memory_order_release);
		return ready;
	}

	void ScreenSpaceShadows::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_registrationsReady.load(std::memory_order_acquire) || !a_device)
			return;
		if (_deviceIdentity != a_device) {
			_deviceIdentity = a_device;
			++_deviceGeneration;
			_resourcesReady.store(false, std::memory_order_release);
			_whiteFallbackReady.store(false, std::memory_order_release);
			_resourceInitFailed = false;
			_realMaskReadyForDraw = false;
			_raymarchCB.reset();
			_maskTexture.reset();
			_pointBorderSampler = nullptr;
			_raymarchCS = nullptr;
			_whiteFallbackSRV = nullptr;
			_allocWidth = 0;
			_allocHeight = 0;
			_lastCompiledSampleCount = 0;
			_whiteFallbackExtent = {};
			_whiteFallbackBackoff.RecordSuccess();
		}
		render::Extent required;
		render::Extent allocation;
		if (TryGetMaskExtents(required, allocation)) {
			(void)EnsureWhiteFallback(
				a_device,
				required,
				allocation);
		}
		PublishRequiredMaskExtent(required);
		(void)EnsureResources();
	}

	void ScreenSpaceShadows::WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB& a_fo4)
	{
		// Refreshed after the prepass bind; a stock slot would read stale shadows.
		a_fo4.EnabledScreenSpaceShadows = _maskBound.load(std::memory_order_relaxed) ? 1u : 0u;
	}

	bool ScreenSpaceShadows::EnsureResources()
	{
		if (_resourcesReady.load(std::memory_order_acquire)) {
			return true;
		}
		if (_resourceInitFailed) {
			return false;
		}

		auto* device = cs::util::GetD3DDevice();
		if (!device) {
			return false;
		}

		try {
			_raymarchCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<RaymarchCB>());
			_raymarchCB->SetName("ScreenSpaceShadows/RaymarchConstants.Buffer");

			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
			samplerDesc.MaxAnisotropy = 1;
			samplerDesc.BorderColor[0] = kFarDepthValue;
			samplerDesc.BorderColor[1] = kFarDepthValue;
			samplerDesc.BorderColor[2] = kFarDepthValue;
			samplerDesc.BorderColor[3] = kFarDepthValue;
			samplerDesc.MinLOD = 0.0f;
			samplerDesc.MaxLOD = FLT_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, _pointBorderSampler.put()));
			cs::render::annotation::SetName(
				_pointBorderSampler.get(), "ScreenSpaceShadows/PointBorder.Sampler");

			render::Extent required;
			render::Extent allocation;
			if (!TryGetMaskExtents(required, allocation)) {
				throw std::runtime_error("graphics state has no screen dimensions");
			}
			CreateMaskTexture(allocation.width, allocation.height);
			_resourcesReady.store(true, std::memory_order_release);
			L->info("Resources ready ({}x{}).", _allocWidth, _allocHeight);
			return true;
		} catch (const std::exception& e) {
			_resourceInitFailed = true;
			_pointBorderSampler = nullptr;
			_maskTexture.reset();
			_raymarchCB.reset();
			_allocWidth = 0;
			_allocHeight = 0;
			L->error("Resource creation failed: {}", e.what());
			return false;
		} catch (...) {
			_resourceInitFailed = true;
			_pointBorderSampler = nullptr;
			_maskTexture.reset();
			_raymarchCB.reset();
			_allocWidth = 0;
			_allocHeight = 0;
			L->error("Resource creation failed.");
			return false;
		}
	}

	std::uint32_t ScreenSpaceShadows::GetScaledSampleCount() const
	{
		const auto resolution = render::GetRenderResolution();
		if (!resolution.render.IsValid()) {
			return sss_math::kMinSampleCount;
		}
		return sss_math::ScaleSampleCount(_settings.SampleCount,
			static_cast<float>(resolution.render.width), static_cast<float>(resolution.render.height));
	}

	ID3D11ComputeShader* ScreenSpaceShadows::GetComputeRaymarch()
	{
		const auto scaledSampleCount = GetScaledSampleCount();
		if (scaledSampleCount != _lastCompiledSampleCount) {
			_lastCompiledSampleCount = scaledSampleCount;
			_raymarchCS = nullptr;
		}

		if (!_raymarchCS) {
			const auto sampleCount = std::to_string(scaledSampleCount);
			const std::vector<std::pair<const char*, const char*>> defines{
				{ "SAMPLE_COUNT", sampleCount.c_str() },
				// FO4: upstream's R32_FLOAT declaration also serves canonical depth.
				{ "TERRAIN_BLENDING", "" }
			};
			_raymarchCS = cs::util::CompileComputeShader("ScreenSpaceShadows/Raymarch.CS", kRaymarchPath, defines);
			if (_raymarchCS)
				L->info("Compiled raymarch shader with {} samples.", scaledSampleCount);
		}
		return _raymarchCS.get();
	}

	void ScreenSpaceShadows::OnPreDeferredLights()
	{
		_realMaskReadyForDraw = false;
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			return;
		}

		_dispatchedLastFrame.store(0, std::memory_order_relaxed);
		render::Extent required;
		render::Extent allocation;
		auto* device = cs::util::GetD3DDevice();
		const bool extentsValid =
			device && TryGetMaskExtents(required, allocation);
		PublishRequiredMaskExtent(required);
		if (extentsValid) {
			(void)EnsureWhiteFallback(
				device,
				required,
				allocation);
		}
		if (!extentsValid)
			return;
		if (!EnsureResources()) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}

		const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		if (allocation.width != _allocWidth || allocation.height != _allocHeight) {
			try {
				CreateMaskTexture(
					allocation.width,
					allocation.height);
			} catch (const std::exception& e) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Mask resize failed: {}", e.what());
				return;
			} catch (...) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "Mask resize failed.");
				return;
			}
		}
		const render::Extent realExtent{
			_allocWidth,
			_allocHeight
		};
		if (_maskTexture && _maskTexture->uav && sss_resources::Covers(realExtent, required)) {
			cs::render::annotation::ScopedEvent annotationScope(
				"ScreenSpaceShadows::ClearMask");
			context->ClearUnorderedAccessViewFloat(
				_maskTexture->uav.get(),
				white);
			_realMaskReadyForDraw = true;
		}
		if (!_realMaskReadyForDraw)
			return;

		auto* shader = (IsEnabled() && cs::engine::IsFullSky()) ?
		                   GetComputeRaymarch() :
		                   nullptr;

		float sx = 0.0f;
		float sy = 0.0f;
		float sz = 0.0f;
		if (shader && cs::engine::TryGetSunDirectionWS(sx, sy, sz)) {
			// FO4: canonical depth puts first-person casters in the world projection.
			auto* depthSRV = cs::render::GetCanonicalSceneDepthSRV();
			if (depthSRV && _raymarchCB && _pointBorderSampler) {
				const auto resolution = render::GetRenderResolution();
				int viewportSize[2] = {
					static_cast<int>(resolution.render.width),
					static_cast<int>(resolution.render.height)
				};

				if (viewportSize[0] > 0 && viewportSize[1] > 0) {
					cs::render::annotation::ScopedEvent annotationScope(
						"ScreenSpaceShadows::Raymarch");
					cs::engine::ComputeOMScope scope(context);

					const auto sceneCamera = cs::engine::GetCapturedWorldCameraRecord(cs::engine::GetFrameCount());
					if (!sceneCamera) {
						return;
					}
					// FO4: sun row zero propagates light; Bend projects the toward-light direction.
					DirectX::XMVECTOR sunDir = DirectX::XMVectorSet(-sx, -sy, -sz, 0.0f);
					DirectX::XMMATRIX vp = DirectX::XMLoadFloat4x4(&sceneCamera->ViewProjection);
					DirectX::XMVECTOR clip = DirectX::XMVector4Transform(sunDir, vp);
					float lightProj[4] = {
						DirectX::XMVectorGetX(clip),
						DirectX::XMVectorGetY(clip),
						DirectX::XMVectorGetZ(clip),
						DirectX::XMVectorGetW(clip)
					};

					int minBounds[2] = { 0, 0 };
					int maxBounds[2] = { viewportSize[0], viewportSize[1] };
					auto dispatchList = Bend::BuildDispatchList(lightProj, viewportSize, minBounds, maxBounds);

					_sunX.store(sx, std::memory_order_relaxed);
					_sunY.store(sy, std::memory_order_relaxed);
					_sunZ.store(sz, std::memory_order_relaxed);
					_lightX.store(dispatchList.LightCoordinate_Shader[0], std::memory_order_relaxed);
					_lightY.store(dispatchList.LightCoordinate_Shader[1], std::memory_order_relaxed);
					_clipW.store(lightProj[3], std::memory_order_relaxed);

					// Bend handles both sun directions; degenerate frames stay lit.
					{
						ID3D11ShaderResourceView* srvs[1] = { depthSRV };
						ID3D11UnorderedAccessView* uavs[1] = { _maskTexture->uav.get() };
						ID3D11SamplerState* samplers[1] = { _pointBorderSampler.get() };
						ID3D11Buffer* buffers[1] = { _raymarchCB->CB() };
						context->CSSetShaderResources(0, 1, srvs);
						context->CSSetSamplers(0, 1, samplers);
						context->CSSetConstantBuffers(1, 1, buffers);
						context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
						context->CSSetShader(shader, nullptr, 0);

						for (int i = 0; i < dispatchList.DispatchCount; ++i) {
							const auto& dispatch = dispatchList.Dispatch[i];
							RaymarchCB cb{};
							std::copy_n(dispatchList.LightCoordinate_Shader, 4, cb.LightCoordinate);
							std::copy_n(dispatch.WaveOffset_Shader, 2, cb.WaveOffset);
							cb.FarDepthValue = kFarDepthValue;
							cb.NearDepthValue = kNearDepthValue;
							cb.InvDepthTextureSize[0] = 1.0f / static_cast<float>(viewportSize[0]);
							cb.InvDepthTextureSize[1] = 1.0f / static_cast<float>(viewportSize[1]);
							cb.DynamicRes[0] = resolution.widthRatio;
							cb.DynamicRes[1] = resolution.heightRatio;
							cb.settings = _settings;
							_raymarchCB->Update(cb);
							context->Dispatch(
								static_cast<UINT>(dispatch.WaveCount[0]),
								static_cast<UINT>(dispatch.WaveCount[1]),
								static_cast<UINT>(dispatch.WaveCount[2]));
							_dispatchedLastFrame.fetch_add(1, std::memory_order_relaxed);
						}

						ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
						ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
						ID3D11SamplerState* nullSamplers[1] = { nullptr };
						ID3D11Buffer* nullBuffers[1] = { nullptr };
						context->CSSetShaderResources(0, 1, nullSRVs);
						context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
						context->CSSetShader(nullptr, nullptr, 0);
						context->CSSetSamplers(0, 1, nullSamplers);
						context->CSSetConstantBuffers(1, 1, nullBuffers);
					}
				}
			}
		}
	}

	void ScreenSpaceShadows::Prepass()
	{
		if (auto* context = cs::engine::GetImmediateContext())
			BindShadowMask(context);
	}

	void ScreenSpaceShadows::BindShadowMask(
		ID3D11DeviceContext* a_context)
	{
		auto* fallback = _whiteFallbackExtent.IsCompatible(_requiredMaskExtent) ? _whiteFallbackSRV.get() : nullptr;
		auto* selected = _realMaskReadyForDraw && _maskTexture && _maskTexture->srv &&
		                         sss_resources::Covers({ _allocWidth, _allocHeight }, _requiredMaskExtent) ?
		                     _maskTexture->srv.get() :
		                     fallback;
		if (!selected) {
			_maskBound.store(false, std::memory_order_relaxed);
			return;
		}
		const auto bind = [&](ID3D11ShaderResourceView* a_view) {
			cs::engine::BindFrameShaderResources(a_context, cs::engine::ShaderStage::kPixel, cs::render::kScreenSpaceShadowsMaskSlot.first, 1, &a_view);
			winrt::com_ptr<ID3D11ShaderResourceView> actual;
			a_context->PSGetShaderResources(cs::render::kScreenSpaceShadowsMaskSlot.first, 1, actual.put());
			return actual.get() == a_view;
		};
		_maskBound.store(
			bind(selected) || (selected != fallback && fallback && bind(fallback)),
			std::memory_order_relaxed);
	}

	void ScreenSpaceShadows::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("resources_ready", _resourcesReady.load(std::memory_order_acquire))
			.Field("white_fallback_ready", _whiteFallbackReady.load(std::memory_order_acquire))
			.Field(
				"white_fallback_allocation_attempts",
				static_cast<std::int64_t>(
					_whiteFallbackAllocationAttempts.load(
						std::memory_order_relaxed)))
			.Field(
				"white_fallback_allocation_failures",
				static_cast<std::int64_t>(
					_whiteFallbackAllocationFailures.load(
						std::memory_order_relaxed)))
			.Field(
				"white_fallback_failure_latched",
				_whiteFallbackFailureLatchedTelemetry.load(
					std::memory_order_relaxed))
			.Field(
				"white_fallback_stock_latched",
				_whiteFallbackFailureLatchedTelemetry.load(
					std::memory_order_relaxed) &&
					!_whiteFallbackReady.load(
						std::memory_order_acquire))
			.Field("mask_bound", _maskBound.load(std::memory_order_relaxed))
			.Field("dispatches", static_cast<std::int64_t>(_dispatchedLastFrame.load(std::memory_order_relaxed)))
			.Dimensions("mask", _allocWidth, _allocHeight)
			.Dimensions(
				"required_extent",
				_requiredMaskWidthTelemetry.load(std::memory_order_relaxed),
				_requiredMaskHeightTelemetry.load(std::memory_order_relaxed))
			.Dimensions(
				"white_fallback",
				_whiteFallbackWidthTelemetry.load(std::memory_order_relaxed),
				_whiteFallbackHeightTelemetry.load(std::memory_order_relaxed))
			.Field("sun_x", static_cast<double>(_sunX.load(std::memory_order_relaxed)))
			.Field("sun_y", static_cast<double>(_sunY.load(std::memory_order_relaxed)))
			.Field("sun_z", static_cast<double>(_sunZ.load(std::memory_order_relaxed)))
			.Field("light_x", static_cast<double>(_lightX.load(std::memory_order_relaxed)))
			.Field("light_y", static_cast<double>(_lightY.load(std::memory_order_relaxed)))
			.Field("clip_w", static_cast<double>(_clipW.load(std::memory_order_relaxed)));
	}

	void ScreenSpaceShadows::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(_live);
		if (const ui::Section section{ "sss-general", "General" }; section) {
			field(&Settings::SurfaceThickness, { .label = "Surface thickness" });
			field(&Settings::BilinearThreshold, { .label = "Bilinear threshold" });
			field(&Settings::ShadowContrast, { .label = "Shadow contrast" });
			field(&Settings::SampleCount, { .label = "Sample count multiplier" });
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(ScreenSpaceShadows::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
