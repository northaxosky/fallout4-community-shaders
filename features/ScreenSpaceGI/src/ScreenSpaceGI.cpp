#include "ScreenSpaceGI.h"

#include <DirectXTex.h>
#include <d3d11.h>
#include <DearModdingUI/Client.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Menu.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderVariantRuntimeResolver.h"
#include "Settings/SettingsPersistence.h"
#include "Menu/SettingsEdit.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.screenspacegi");

		constexpr const wchar_t* kUpsamplePath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\upsample.cs.hlsl";
		constexpr const wchar_t* kPrefilterPath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\prefilterDepths.cs.hlsl";
		constexpr const wchar_t* kPrefilterRadiancePath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\prefilterRadiance.cs.hlsl";
		constexpr const wchar_t* kPrefilterNormalPath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\prefilterNormal.cs.hlsl";
		constexpr const wchar_t* kRadianceDisoccPath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\radianceDisocc.cs.hlsl";
		constexpr const wchar_t* kAOPath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\gi.cs.hlsl";
		constexpr const wchar_t* kNoisePath = L"Data\\Shaders\\ScreenSpaceGI\\fast_2uges.dds";
		constexpr const wchar_t* kBlurPath = L"Data\\Shaders\\ScreenSpaceGI\\XeGTAO\\blur.cs.hlsl";

		// occlusion and bounce both read as "no contribution" at zero
		constexpr std::array<float, 4> kOpenIdentity{ 0.0f, 0.0f, 0.0f, 0.0f };

		bool IsFullResolutionHDR(
			ID3D11ShaderResourceView* a_srv,
			std::uint32_t a_width,
			std::uint32_t a_height,
			D3D11_TEXTURE2D_DESC& a_desc)
		{
			if (!a_srv) {
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			a_srv->GetDesc(&srvDesc);
			if (srvDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				srvDesc.Format != DXGI_FORMAT_R11G11B10_FLOAT) {
				return false;
			}

			winrt::com_ptr<ID3D11Resource> resource;
			a_srv->GetResource(resource.put());
			auto texture = resource.try_as<ID3D11Texture2D>();
			if (!texture) {
				return false;
			}

			texture->GetDesc(&a_desc);
			return a_desc.Width == a_width &&
				a_desc.Height == a_height &&
				a_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
				a_desc.ArraySize == 1 &&
				a_desc.SampleDesc.Count == 1;
		}

		bool IsFullResolutionMotion(
			ID3D11ShaderResourceView* a_srv,
			std::uint32_t a_width,
			std::uint32_t a_height)
		{
			if (!a_srv) {
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			a_srv->GetDesc(&srvDesc);
			if (srvDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				srvDesc.Format != DXGI_FORMAT_R16G16_FLOAT) {
				return false;
			}

			winrt::com_ptr<ID3D11Resource> resource;
			a_srv->GetResource(resource.put());
			auto texture = resource.try_as<ID3D11Texture2D>();
			if (!texture) {
				return false;
			}

			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			return desc.Width == a_width &&
				desc.Height == a_height &&
				desc.Format == DXGI_FORMAT_R16G16_FLOAT &&
				desc.ArraySize == 1 &&
				desc.SampleDesc.Count == 1;
		}

		std::unique_ptr<cs::buffer::Texture2D> CreateTexture(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_format,
			std::string_view a_name,
			std::uint32_t a_mipLevels = 1,
			bool a_createMipZeroUAV = true)
		{
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = a_width;
			textureDesc.Height = a_height;
			textureDesc.MipLevels = a_mipLevels;
			textureDesc.ArraySize = 1;
			textureDesc.Format = a_format;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			auto texture = std::make_unique<cs::buffer::Texture2D>(textureDesc);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = textureDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = a_mipLevels;
			texture->CreateSRV(srvDesc);

			if (a_createMipZeroUAV) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
				uavDesc.Format = textureDesc.Format;
				uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				uavDesc.Texture2D.MipSlice = 0;
				texture->CreateUAV(uavDesc);
			}
			texture->SetName(
				std::format("{}.Texture", a_name),
				std::format("{}.SRV", a_name),
				std::format("{}.UAV", a_name));

			return texture;
		}

		void CreateMipUAVs(
			ID3D11Device* a_device,
			const cs::buffer::Texture2D& a_texture,
			DXGI_FORMAT a_format,
			std::span<winrt::com_ptr<ID3D11UnorderedAccessView>> a_out,
			std::string_view a_name)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = a_format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			for (std::size_t mip = 0; mip < a_out.size(); ++mip) {
				uavDesc.Texture2D.MipSlice = static_cast<UINT>(mip);
				DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(
					a_texture.resource.get(), &uavDesc, a_out[mip].put()));
				cs::render::annotation::SetName(
					a_out[mip].get(),
					std::format("{}[{}].UAV", a_name, mip));
			}
		}

		ID3D11ShaderResourceView* SRVOf(const std::unique_ptr<cs::buffer::Texture2D>& a_texture)
		{
			return a_texture ? a_texture->srv.get() : nullptr;
		}

		void ClearToIdentity(
			ID3D11DeviceContext* a_context,
			const std::unique_ptr<cs::buffer::Texture2D>& a_texture)
		{
			if (a_texture && a_texture->uav) {
				a_context->ClearUnorderedAccessViewFloat(a_texture->uav.get(), kOpenIdentity.data());
			}
		}

		std::uint32_t DispatchGroups(int a_extent, std::uint32_t a_groupSize)
		{
			return (static_cast<std::uint32_t>(a_extent) + a_groupSize - 1u) / a_groupSize;
		}

		std::uint32_t ActiveExtent(std::uint32_t a_extent, float a_ratio)
		{
			if (!std::isfinite(a_ratio) || a_ratio <= 0.0f) {
				return 0;
			}
			return std::min(
				a_extent,
				static_cast<std::uint32_t>(static_cast<double>(a_extent) * a_ratio));
		}

		// Binds exactly what a pass needs, dispatches, then narrow-unbinds the same slots.
		class ComputePass
		{
		public:
			ComputePass(
				ID3D11DeviceContext* a_context,
				ID3D11Buffer* a_constants,
				std::array<ID3D11SamplerState*, 2> a_samplers) noexcept :
				_context(a_context),
				_constants(a_constants),
				_samplers(a_samplers)
			{
				// Tiled lighting can leave its B buffers in the owned low slots.
				static constexpr ID3D11ShaderResourceView* nullSRVs[16]{};
				static constexpr ID3D11UnorderedAccessView* nullUAVs[8]{};
				_context->CSSetShaderResources(0, 16, nullSRVs);
				_context->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);
			}

			void Dispatch(
				ID3D11ComputeShader* a_shader,
				std::span<ID3D11ShaderResourceView* const> a_srvs,
				std::span<ID3D11UnorderedAccessView* const> a_uavs,
				std::uint32_t a_groupsX,
				std::uint32_t a_groupsY,
				std::string_view a_name)
			{
				cs::render::annotation::ScopedEvent annotationScope(a_name);
				const auto srvCount = static_cast<UINT>(a_srvs.size());
				const auto uavCount = static_cast<UINT>(a_uavs.size());
				_context->CSSetShaderResources(0, srvCount, a_srvs.data());
				_context->CSSetUnorderedAccessViews(0, uavCount, a_uavs.data(), nullptr);
				_context->CSSetConstantBuffers(0, 1, &_constants);
				_context->CSSetSamplers(0, static_cast<UINT>(_samplers.size()), _samplers.data());
				_context->CSSetShader(a_shader, nullptr, 0);
				_context->Dispatch(a_groupsX, a_groupsY, 1);

				static constexpr ID3D11ShaderResourceView* nullSRVs[16]{};
				static constexpr ID3D11UnorderedAccessView* nullUAVs[8]{};
				static constexpr ID3D11SamplerState* nullSamplers[2]{};
				ID3D11Buffer* nullConstants = nullptr;
				_context->CSSetShaderResources(0, srvCount, nullSRVs);
				_context->CSSetUnorderedAccessViews(0, uavCount, nullUAVs, nullptr);
				_context->CSSetConstantBuffers(0, 1, &nullConstants);
				_context->CSSetSamplers(0, 2, nullSamplers);
				_context->CSSetShader(nullptr, nullptr, 0);
			}

		private:
			ID3D11DeviceContext* _context;
			ID3D11Buffer* _constants;
			std::array<ID3D11SamplerState*, 2> _samplers;
		};
	}

	ScreenSpaceGI* ScreenSpaceGI::GetSingleton()
	{
		static ScreenSpaceGI instance;
		return &instance;
	}

	std::span<const FeatureDebugView> ScreenSpaceGI::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "occlusion",
				.label = "Occlusion preview",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const ScreenSpaceGI&>(a_feature)
						.GetOcclusionDebugTexture();
				}
			}
		};
		return views;
	}

	void ScreenSpaceGI::SetDebugView(std::string_view a_view) noexcept
	{
		_debugPreviewEnabled.store(
			a_view == "occlusion",
			std::memory_order_release);
	}

	FeatureDebugTexture ScreenSpaceGI::GetOcclusionDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText = "Buffer not allocated."
		};
		const auto& source = _upsampledLastFrame ? _aoUpsampledTex : _aoTex;
		if (!_debugPreviewEnabled.load(std::memory_order_acquire)
			|| !source
			|| !source->srv
			|| _allocW == 0
			|| _allocH == 0) {
			return texture;
		}
		texture.texture = source->srv.get();
		texture.width = _allocW;
		texture.height = _allocH;
		texture.caption = std::format(
			"Occlusion (bright = occluded) {}x{}",
			_allocW,
			_allocH);
		return texture;
	}

	bool ScreenSpaceGI::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(ssgi_settings::kSchema, a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		return true;
	}

	bool ScreenSpaceGI::SaveSettings()
	{
		return settings::SaveDelta(ssgi_settings::kSchema, GetConfigKey(), _settings, *L);
	}

	void ScreenSpaceGI::Load()
	{
		std::vector<cs::engine::ShaderSlotClaim> slotClaims;
		slotClaims.reserve(kCompositionPSSlotCount);
		for (std::uint32_t offset = 0; offset < kCompositionPSSlotCount; ++offset) {
			slotClaims.push_back({
				.stage = cs::engine::ShaderStage::kPixel,
				.resourceType = cs::engine::ShaderResourceType::kShaderResource,
				.slot = kCompositionPSSlot + offset
			});
		}

		const bool registered = cs::engine::RegisterReplacement({
			.targetId = cs::engine::ShaderInjectionTarget::kBsdfComposite,
			.contributor = "ScreenSpaceGI",
			.defines = {
				{
					cs::engine::shader_injection_defines::kScreenSpaceGi,
					"1"
				}
			},
			.bind = [this](ID3D11DeviceContext* a_context) {
				BindComposition(a_context);
			},
			.slotClaims = std::move(slotClaims)
		});
		if (!registered) {
			FailLoad(
				"ScreenSpaceGI composes through the reconstructed BSDFComposite shader; "
				"registering that replacement failed, so there is no delivery path");
			return;
		}

		_injectionRegistered.store(true, std::memory_order_release);
		const bool compositionScopeRegistered =
			cs::engine::RegisterPreDeferredComposite([] {
				ScreenSpaceGI::GetSingleton()->SaveCompositionBindings();
			}, cs::engine::HookPriority::Early)
			&& cs::engine::RegisterPostDeferredComposite([] {
				ScreenSpaceGI::GetSingleton()->RestoreCompositionBindings();
			}, cs::engine::HookPriority::Late);
		if (!compositionScopeRegistered) {
			FailLoad(
				"ScreenSpaceGI needs a paired composite save and restore to hand its "
				"bindings back to the engine; registering that pair failed");
			return;
		}
		// Deferred lighting has written the radiance source by this anchor.
		cs::engine::RegisterPostDeferredLightsImpl([] {
			ScreenSpaceGI::GetSingleton()->OnPostDeferredLights();
		});
		// Precedes DrawWorld::ImagespaceSAO and the composite's APPLY_AO selection.
		if (!cs::engine::RegisterPostDeferredPrePass([] {
				ScreenSpaceGI::GetSingleton()->ApplyVanillaSSAO();
			})) {
			L->warn("Vanilla SSAO control could not register; the engine's AO stays as configured.");
		}

		// Seed the transition detectors so the first frame is not a spurious re-enable.
		_lastTemporalEnabled = _settings.enableTemporalDenoiser;
		_started.store(true, std::memory_order_release);
		L->info(
			"Registered composite injection and post-deferred-lights callback (enabled={}).",
			_settings.enabled);
	}

	void ScreenSpaceGI::OnDataLoaded()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->RegisterSink<RE::MenuOpenCloseEvent>(this);
		}
	}

	RE::BSEventNotifyControl ScreenSpaceGI::ProcessEvent(
		const RE::MenuOpenCloseEvent& a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		// The event thread only queues; the render thread owns history and GPU state.
		if (a_event.menuName == RE::LoadingMenu::MENU_NAME && !a_event.opening) {
			_queuedHistoryReset.store(true, std::memory_order_release);
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void ScreenSpaceGI::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_started.load(std::memory_order_acquire) || !a_device) return;

		auto compile = [](
			winrt::com_ptr<ID3D11ComputeShader>& a_target,
			const wchar_t* a_path,
			const std::vector<std::pair<const char*, const char*>>& a_defines,
			const char* a_label,
			std::string_view a_name) {
			a_target.attach(reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(a_path, a_defines, "cs_5_0")));
			if (!a_target) {
				L->warn("Failed to compile XeGTAO {} shader.", a_label);
			} else {
				cs::render::annotation::SetName(a_target.get(), a_name);
			}
		};

		static constexpr std::array<const char*, kResolutionModes> kResolutionDefines{ nullptr, "HALF_RES", "QUARTER_RES" };
		static constexpr std::array<const char*, kResolutionModes> kResolutionNames{ "Full", "Half", "Quarter" };
		for (std::size_t mode = 0; mode < kResolutionModes; ++mode) {
			auto& shaders = _shaders[mode];
			const auto defines = [&](bool a_gi, bool a_temporal, std::initializer_list<std::pair<const char*, const char*>> a_extra = {}) {
				std::vector<std::pair<const char*, const char*>> result(a_extra);
				if (kResolutionDefines[mode])
					result.emplace_back(kResolutionDefines[mode], "1");
				if (a_gi)
					result.emplace_back("GI", "1");
				if (a_temporal)
					result.emplace_back("TEMPORAL_DENOISER", "1");
				return result;
			};
			const auto name = [&](std::string_view a_pass) {
				return std::format("ScreenSpaceGI/{}{}.CS", a_pass, kResolutionNames[mode]);
			};
			compile(shaders.prefilterDepth, kPrefilterPath, defines(false, false, { { "LINEAR_FILTER", "1" } }), "depth prefilter", name("PrefilterDepth"));
			compile(shaders.prefilterRadiance, kPrefilterRadiancePath, defines(false, false), "radiance prefilter", name("PrefilterRadiance"));
			compile(shaders.prefilterNormal, kPrefilterNormalPath, defines(false, false), "normal prefilter", name("PrefilterNormal"));
			compile(shaders.ao, kAOPath, defines(false, false), "AO", name("AO"));
			for (std::size_t temporal = 0; temporal < 2; ++temporal) {
				const bool on = temporal != 0;
				compile(shaders.radianceDisocc[temporal], kRadianceDisoccPath, defines(true, on), "radiance disocclusion", name(on ? "RadianceDisocclusionTemporal" : "RadianceDisocclusion"));
				compile(shaders.gi[temporal], kAOPath, defines(true, on), "GI", name(on ? "GITemporal" : "GI"));
				compile(shaders.blur[temporal], kBlurPath, defines(true, on), "blur", name(on ? "BlurTemporal" : "Blur"));
			}
			if (kResolutionDefines[mode])
				compile(shaders.upsample, kUpsamplePath, defines(false, false), "upsample", name("Upsample"));
		}

		const auto createSampler = [&](winrt::com_ptr<ID3D11SamplerState>& a_sampler, D3D11_FILTER a_filter, std::string_view a_name) {
			if (a_sampler)
				return;
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = a_filter;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxAnisotropy = 1;
			samplerDesc.MinLOD = 0.0f;
			samplerDesc.MaxLOD = FLT_MAX;
			DX::ThrowIfFailed(a_device->CreateSamplerState(&samplerDesc, a_sampler.put()));
			cs::render::annotation::SetName(a_sampler.get(), a_name);
		};
		createSampler(_pointClampSampler, D3D11_FILTER_MIN_MAG_MIP_POINT, "ScreenSpaceGI/PointClamp.Sampler");
		createSampler(_linearClampSampler, D3D11_FILTER_MIN_MAG_MIP_LINEAR, "ScreenSpaceGI/LinearClamp.Sampler");

		// the injected variant is runtime-toggled, so allocate regardless of the setting
		EnsureResources();
	}

	const ScreenSpaceGI::ResolutionShaders& ScreenSpaceGI::ActiveShaders() const noexcept
	{
		return _shaders[static_cast<std::size_t>(std::clamp(_settings.resolutionMode, 0, 2))];
	}

	bool ScreenSpaceGI::IsGeneratorReady() const noexcept
	{
		const auto& shaders = ActiveShaders();
		const bool upsampleReady = _settings.resolutionMode == 0 ||
			(shaders.upsample && _aoUpsampledTex && _bounceSHUpsampledTex && _bounceCoCgUpsampledTex);
		return shaders.prefilterDepth && shaders.prefilterNormal && shaders.ao && upsampleReady &&
			_workingDepthTex && _normalTex && _aoTex &&
			_noiseSRV && _pointClampSampler && _linearClampSampler && _xegtaoCB &&
			_workingDepthMipUAVs[kMipCount - 1] && _normalMipUAVs[kMipCount - 1];
	}

	bool ScreenSpaceGI::IsTemporalReady() const noexcept
	{
		const auto& shaders = ActiveShaders();
		const auto temporal = _settings.enableTemporalDenoiser ? 1u : 0u;
		return shaders.gi[temporal] && shaders.radianceDisocc[temporal] &&
			(!_settings.enableBlur || shaders.blur[temporal]) &&
			shaders.prefilterRadiance &&
			_radianceTempTex && _radianceTex && _radianceMipUAVs[kMipCount - 1] &&
			_bounceSHRawTex && _bounceCoCgRawTex && _accumBlurTex &&
			_bounceSHTex[0] && _bounceSHTex[1] &&
			_bounceCoCgTex[0] && _bounceCoCgTex[1] &&
			_accumTex[0] && _accumTex[1] &&
			_prevGeoTex[0] && _prevGeoTex[1];
	}
	cs::ScreenSpaceGIFeatureData ScreenSpaceGI::GetCommonBufferData()
	{
		if (!_settings.enabled ||
			!_injectionRegistered.load(std::memory_order_acquire) ||
			!_resourcesReady.load(std::memory_order_acquire) ||
			!_aoProducedLastFrame.load(std::memory_order_relaxed) ||
			!IsGeneratorReady() ||
			!cs::engine::GetRenderTargetSRV(
				cs::engine::RenderTarget::kGbufferNormal)) {
			return {};
		}
		ScreenSpaceGIFeatureData data{ .EnableScreenSpaceGI = 1 };
		std::array<DirectX::XMFLOAT4, 3> ambient{};
		if (!cs::engine::TryGetDirectionalAmbientRows(ambient)) {
			return {};
		}
		for (std::size_t row = 0; row < ambient.size(); ++row) {
			data.DirectionalAmbient[row][0] = ambient[row].x;
			data.DirectionalAmbient[row][1] = ambient[row].y;
			data.DirectionalAmbient[row][2] = ambient[row].z;
			data.DirectionalAmbient[row][3] = ambient[row].w;
		}
		return data;
	}

	bool ScreenSpaceGI::EnsureResources()
	{
		if (_resourceInitFailed.load(std::memory_order_acquire)) {
			return false;
		}
		if (!cs::util::GetD3DDevice()) {
			return false;
		}

		const bool resourcesReady = _resourcesReady.load(std::memory_order_acquire);
		const bool hadResources = _aoTex != nullptr;

		// an engine that has not published dimensions or a context yet is not a failure
		auto* state = cs::engine::GetGraphicsState();
		if (!state || state->screenWidth == 0 || state->screenHeight == 0) {
			return resourcesReady;
		}
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ?
			reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			nullptr;
		if (!context) {
			return resourcesReady;
		}

		// Full-resolution allocation avoids dynamic-resolution churn.
		const std::uint32_t width = state->screenWidth;
		const std::uint32_t height = state->screenHeight;
		if (resourcesReady && width == _allocW && height == _allocH) {
			return true;
		}

		try {
			if (hadResources) {
				_resourcesReady.store(false, std::memory_order_release);
			}

			auto* device = cs::util::GetD3DDevice();

			auto workingDepthTex = CreateTexture(width, height, DXGI_FORMAT_R16_FLOAT, "ScreenSpaceGI/WorkingDepth", kMipCount, false);
			auto normalTex = CreateTexture(width, height, DXGI_FORMAT_R8G8_UNORM, "ScreenSpaceGI/Normal", kMipCount, false);
			auto radianceTempTex = CreateTexture(width, height, DXGI_FORMAT_R11G11B10_FLOAT, "ScreenSpaceGI/RadianceTemp");
			auto radianceTex = CreateTexture(width, height, DXGI_FORMAT_R11G11B10_FLOAT, "ScreenSpaceGI/Radiance", kMipCount, false);
			auto aoTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM, "ScreenSpaceGI/AO");
			auto bounceSHRawTex = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, "ScreenSpaceGI/BounceSHRaw");
			auto bounceCoCgRawTex = CreateTexture(width, height, DXGI_FORMAT_R16G16_FLOAT, "ScreenSpaceGI/BounceCoCgRaw");
			auto accumBlurTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM, "ScreenSpaceGI/AccumulationBlur");
			auto aoUpsampledTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM, "ScreenSpaceGI/AOUpsampled");
			auto bounceSHUpsampledTex = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, "ScreenSpaceGI/BounceSHUpsampled");
			auto bounceCoCgUpsampledTex = CreateTexture(width, height, DXGI_FORMAT_R16G16_FLOAT, "ScreenSpaceGI/BounceCoCgUpsampled");
			std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> bounceSHTex;
			std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> bounceCoCgTex;
			std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> accumTex;
			std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> prevGeoTex;
			for (std::size_t index = 0; index < 2; ++index) {
				bounceSHTex[index] = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, std::format("ScreenSpaceGI/BounceSH[{}]", index));
				bounceCoCgTex[index] = CreateTexture(width, height, DXGI_FORMAT_R16G16_FLOAT, std::format("ScreenSpaceGI/BounceCoCg[{}]", index));
				accumTex[index] = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM, std::format("ScreenSpaceGI/Accumulation[{}]", index));
				prevGeoTex[index] = CreateTexture(width, height, DXGI_FORMAT_R11G11B10_FLOAT, std::format("ScreenSpaceGI/PreviousGeometry[{}]", index));
			}
			auto xegtaoCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<XeGTAOCB>());
			xegtaoCB->SetName("ScreenSpaceGI/Constants.Buffer");

			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> workingDepthMipUAVs;
			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> normalMipUAVs;
			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> radianceMipUAVs;
			CreateMipUAVs(device, *workingDepthTex, DXGI_FORMAT_R16_FLOAT, workingDepthMipUAVs, "ScreenSpaceGI/WorkingDepth");
			CreateMipUAVs(device, *normalTex, DXGI_FORMAT_R8G8_UNORM, normalMipUAVs, "ScreenSpaceGI/Normal");
			CreateMipUAVs(device, *radianceTex, DXGI_FORMAT_R11G11B10_FLOAT, radianceMipUAVs, "ScreenSpaceGI/Radiance");

			winrt::com_ptr<ID3D11Texture2D> noiseTex;
			winrt::com_ptr<ID3D11ShaderResourceView> noiseSRV;
			if (!_noiseTex) {
				// EA fastnoise, 128x128x64.
				DirectX::ScratchImage loaded;
				DirectX::TexMetadata metadata{};
				DX::ThrowIfFailed(DirectX::LoadFromDDSFile(
					kNoisePath, DirectX::DDS_FLAGS_NONE, &metadata, loaded));
				DX::ThrowIfFailed(DirectX::CreateShaderResourceView(
					device, loaded.GetImages(), loaded.GetImageCount(), metadata, noiseSRV.put()));
				winrt::com_ptr<ID3D11Resource> noiseResource;
				noiseSRV->GetResource(noiseResource.put());
				noiseTex = noiseResource.try_as<ID3D11Texture2D>();
				if (!noiseTex) {
					throw std::runtime_error("fast_2uges.dds is not a 2D texture");
				}
				cs::render::annotation::SetName(
					noiseTex.get(), "ScreenSpaceGI/Noise.Texture");
				cs::render::annotation::SetName(
					noiseSRV.get(), "ScreenSpaceGI/Noise.SRV");
			}
			_resourcesReady.store(false, std::memory_order_release);
			_workingDepthTex = std::move(workingDepthTex);
			_workingDepthMipUAVs = std::move(workingDepthMipUAVs);
			_normalTex = std::move(normalTex);
			_normalMipUAVs = std::move(normalMipUAVs);
			_radianceTempTex = std::move(radianceTempTex);
			_radianceTex = std::move(radianceTex);
			_radianceMipUAVs = std::move(radianceMipUAVs);
			_aoTex = std::move(aoTex);
			_bounceSHRawTex = std::move(bounceSHRawTex);
			_bounceCoCgRawTex = std::move(bounceCoCgRawTex);
			_bounceSHTex = std::move(bounceSHTex);
			_bounceCoCgTex = std::move(bounceCoCgTex);
			_accumTex = std::move(accumTex);
			_prevGeoTex = std::move(prevGeoTex);
			_accumBlurTex = std::move(accumBlurTex);
			_aoUpsampledTex = std::move(aoUpsampledTex);
			_bounceSHUpsampledTex = std::move(bounceSHUpsampledTex);
			_bounceCoCgUpsampledTex = std::move(bounceCoCgUpsampledTex);
			_xegtaoCB = std::move(xegtaoCB);
			if (noiseTex) {
				_noiseTex = std::move(noiseTex);
				_noiseSRV = std::move(noiseSRV);
			}

			// fresh allocations must read as fully open until the generator runs
			_occlusionOutputsDirty = true;
			_bounceOutputsDirty = true;
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			ClearTemporalHistory(context);

			_allocW = width;
			_allocH = height;
			++_generation;
			ResetHistory(hadResources ?
				ssgi::HistoryResetReason::kResize :
				ssgi::HistoryResetReason::kResourceCreate);
			_resourcesReady.store(true, std::memory_order_release);
			L->info("Resources ready ({}x{}, generation {}).", _allocW, _allocH, _generation);
			return true;
		} catch (const std::exception& e) {
			if (hadResources) {
				L->error("Resource resize failed: {}", e.what());
			} else {
				_resourceInitFailed.store(true, std::memory_order_release);
				L->error("Resource creation failed: {}", e.what());
			}
			return false;
		} catch (...) {
			if (hadResources) {
				L->error("Resource resize failed.");
			} else {
				_resourceInitFailed.store(true, std::memory_order_release);
				L->error("Resource creation failed.");
			}
			return false;
		}
	}

	void ScreenSpaceGI::ApplyVanillaSSAO()
	{
		const auto state = cs::engine::GetScalableAOComputeState();
		if (!state) {
			return;
		}
		if (!_vanillaSSAOSnapshot) {
			_vanillaSSAOSnapshot = *state->applied;
		}
		// Independent of `enabled`, as upstream; per frame because DrawModel and console commands rewrite it.
		const bool applied = _settings.enableVanillaSSAO && *_vanillaSSAOSnapshot;
		*state->applied = applied;
		*state->active = _settings.enableVanillaSSAO && (*state->base || applied);
		_vanillaSSAOAppliedLastFrame.store(applied, std::memory_order_relaxed);
	}

	void ScreenSpaceGI::ResetHistory(ssgi::HistoryResetReason a_reason)
	{
		_history.Reset(a_reason);
		_historyResetCount.store(_history.ResetCount(), std::memory_order_relaxed);
		_lastResetReason.store(static_cast<std::uint32_t>(a_reason), std::memory_order_relaxed);
	}

	void ScreenSpaceGI::ClearOcclusionOutputs(ID3D11DeviceContext* a_context)
	{
		if (!_occlusionOutputsDirty || !a_context) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope(
			"ScreenSpaceGI/ClearOcclusionOutputs");
		ClearToIdentity(a_context, _aoTex);
		ClearToIdentity(a_context, _aoUpsampledTex);
		_occlusionOutputsDirty = false;
	}

	void ScreenSpaceGI::ClearBounceOutputs(ID3D11DeviceContext* a_context)
	{
		if (!_bounceOutputsDirty || !a_context) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope(
			"ScreenSpaceGI/ClearBounceOutputs");
		ClearToIdentity(a_context, _bounceSHRawTex);
		ClearToIdentity(a_context, _bounceCoCgRawTex);
		ClearToIdentity(a_context, _bounceSHUpsampledTex);
		ClearToIdentity(a_context, _bounceCoCgUpsampledTex);
		for (std::size_t index = 0; index < 2; ++index) {
			ClearToIdentity(a_context, _bounceSHTex[index]);
			ClearToIdentity(a_context, _bounceCoCgTex[index]);
		}
		_bounceOutputsDirty = false;
	}

	void ScreenSpaceGI::ClearTemporalHistory(ID3D11DeviceContext* a_context)
	{
		if (!a_context) {
			return;
		}
		cs::render::annotation::ScopedEvent annotationScope(
			"ScreenSpaceGI/ClearTemporalHistory");
		for (std::size_t index = 0; index < 2; ++index) {
			ClearToIdentity(a_context, _bounceSHTex[index]);
			ClearToIdentity(a_context, _bounceCoCgTex[index]);
			ClearToIdentity(a_context, _accumTex[index]);
			ClearToIdentity(a_context, _prevGeoTex[index]);
		}
	}

	void ScreenSpaceGI::OnPostDeferredLights()
	{
		if (!_started.load(std::memory_order_acquire)) {
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

		auto* state = cs::engine::GetGraphicsState();
		if (state) {
			// The anchor can fire more than once per engine frame; only the first dispatches.
			if (_lastCallbackFrameValid && state->frameCount == _lastCallbackFrame) {
				_repeatCallbacks.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			_lastCallbackFrame = state->frameCount;
			_lastCallbackFrameValid = true;
		}

		_aoProducedLastFrame.store(false, std::memory_order_relaxed);
		_bounceProducedLastFrame.store(false, std::memory_order_relaxed);
		_bounceDenoisedLastFrame.store(false, std::memory_order_relaxed);
		_radianceAvailableLastFrame.store(false, std::memory_order_relaxed);
		_normalBoundLastFrame.store(false, std::memory_order_relaxed);
		_historyValidLastFrame.store(false, std::memory_order_relaxed);
		_motionAvailableLastFrame.store(false, std::memory_order_relaxed);
		_tiledBAvailable.store(false, std::memory_order_relaxed);
		_compositionBindsLastFrame.store(0, std::memory_order_relaxed);
		_temporalDispatchesLastFrame.store(0, std::memory_order_relaxed);
		_radianceSourceCount.store(0, std::memory_order_relaxed);
		_cameraReadyLastFrame.store(false, std::memory_order_relaxed);
		_cameraOriginXLastFrame.store(0.0f, std::memory_order_relaxed);
		_cameraOriginYLastFrame.store(0.0f, std::memory_order_relaxed);
		_cameraOriginZLastFrame.store(0.0f, std::memory_order_relaxed);
		_cameraPreviousOriginXLastFrame.store(0.0f, std::memory_order_relaxed);
		_cameraPreviousOriginYLastFrame.store(0.0f, std::memory_order_relaxed);
		_cameraPreviousOriginZLastFrame.store(0.0f, std::memory_order_relaxed);

		if (_queuedHistoryReset.exchange(false, std::memory_order_acq_rel)) {
			ResetHistory(ssgi::HistoryResetReason::kLoadingScreenClosed);
		}
		const bool temporalEnabled = _settings.enableTemporalDenoiser;
		const int resolutionMode = std::clamp(_settings.resolutionMode, 0, 2);
		if (temporalEnabled != _lastTemporalEnabled || resolutionMode != _lastResolutionMode) {
			ResetHistory(ssgi::HistoryResetReason::kShaderVariantChanged);
		}
		_lastTemporalEnabled = temporalEnabled;
		_lastResolutionMode = resolutionMode;

		if (!_settings.enabled || !EnsureResources()) {
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			return;
		}

		auto* rtm = cs::engine::GetRenderTargetManager();
		const auto& frameBuffer = cs::engine::GetFrameBuffer();
		const bool cameraReady =
			frameBuffer.valid && cs::engine::HasUsableWorldCamera(frameBuffer.data);
		_cameraReadyLastFrame.store(cameraReady, std::memory_order_relaxed);
		DirectX::XMFLOAT4X4 worldProj{};
		DirectX::XMFLOAT4X4 worldInvProj{};
		DirectX::XMFLOAT4 worldNdcToViewMul{};
		DirectX::XMFLOAT4 worldNdcToViewAdd{};
		auto* depthSRV = cs::engine::GetSceneDepthSRV();
		auto* normalSRV = cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferNormal);
		if (!state || !rtm || !cameraReady || !IsGeneratorReady() || !depthSRV || !normalSRV ||
			!cs::engine::TryGetWorldSceneProjection(
				worldProj, worldInvProj, worldNdcToViewMul, worldNdcToViewAdd)) {
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			return;
		}

		const std::uint32_t frameW = ActiveExtent(_allocW, rtm->GetDynamicWidthRatio());
		const std::uint32_t frameH = ActiveExtent(_allocH, rtm->GetDynamicHeightRatio());
		if (frameW == 0 || frameH == 0) {
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			return;
		}

		D3D11_TEXTURE2D_DESC radianceDesc{};
		auto* radianceSRV = cs::engine::GetRenderTargetSRV(kRadianceSourceA);
		auto* albedoSRV = cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferAlbedo);
		auto* emissiveSRV = cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferEmissive);
		const bool radianceAvailable =
			_settings.enableGI &&
			IsTemporalReady() &&
			albedoSRV &&
			emissiveSRV &&
			IsFullResolutionHDR(radianceSRV, _allocW, _allocH, radianceDesc);
		_radianceAvailableLastFrame.store(radianceAvailable, std::memory_order_relaxed);
		if (_settings.enableGI && !radianceAvailable) {
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"SSGI indirect lighting unavailable: it needs the albedo and emissive targets and full-resolution R11G11B10_FLOAT diffuse light.");
		}

		const bool tiledLighting = cs::engine::QueryTiledLightingEnabled();
		_tiledLightingActive.store(tiledLighting, std::memory_order_relaxed);
		ID3D11ShaderResourceView* radianceBSRV = nullptr;
		if (radianceAvailable && tiledLighting) {
			auto* candidate = cs::engine::GetRenderTargetSRV(kRadianceSourceB);
			D3D11_TEXTURE2D_DESC candidateDesc{};
			if (IsFullResolutionHDR(candidate, radianceDesc.Width, radianceDesc.Height, candidateDesc)) {
				radianceBSRV = candidate;
			} else {
				CS_LOG_ONCE(
					L,
					spdlog::level::warn,
					"SSGI tiled lighting is active but the second radiance buffer does not match the first; using one source.");
			}
		}
		const bool includeSourceB = radianceBSRV != nullptr;
		_tiledBAvailable.store(includeSourceB, std::memory_order_relaxed);
		_radianceSourceCount.store(
			radianceAvailable ? (includeSourceB ? 2u : 1u) : 0u, std::memory_order_relaxed);

		auto* motionSRV = cs::engine::GetRenderTargetSRV(kMotionSource);
		const bool motionAvailable = IsFullResolutionMotion(motionSRV, _allocW, _allocH);
		_motionAvailableLastFrame.store(motionAvailable, std::memory_order_relaxed);

		CameraTransform camera{};
		for (std::size_t row = 0; row < 3; ++row) {
			const auto& entry = frameBuffer.data.ViewToWorld[row];
			camera.rows[row * 4 + 0] = entry.x;
			camera.rows[row * 4 + 1] = entry.y;
			camera.rows[row * 4 + 2] = entry.z;
			camera.rows[row * 4 + 3] = 0.0f;
		}
		const auto cameraOrigin = cs::engine::CameraWorldOrigin(frameBuffer.data);
		_cameraOriginXLastFrame.store(cameraOrigin.x, std::memory_order_relaxed);
		_cameraOriginYLastFrame.store(cameraOrigin.y, std::memory_order_relaxed);
		_cameraOriginZLastFrame.store(cameraOrigin.z, std::memory_order_relaxed);
		const auto previousCameraOrigin =
			cs::engine::CameraPreviousWorldOrigin(frameBuffer.data);
		_cameraPreviousOriginXLastFrame.store(
			previousCameraOrigin.x,
			std::memory_order_relaxed);
		_cameraPreviousOriginYLastFrame.store(
			previousCameraOrigin.y,
			std::memory_order_relaxed);
		_cameraPreviousOriginZLastFrame.store(
			previousCameraOrigin.z,
			std::memory_order_relaxed);
		camera.ndcToViewMul[0] = worldNdcToViewMul.x;
		camera.ndcToViewMul[1] = worldNdcToViewMul.y;
		camera.ndcToViewAdd[0] = worldNdcToViewAdd.x;
		camera.ndcToViewAdd[1] = worldNdcToViewAdd.y;

		// Upstream only resets on loading screens; disocclusion rejects everything else.
		const auto historyFrame = _history.Prepare();
		_historyResetCount.store(_history.ResetCount(), std::memory_order_relaxed);
		_lastResetReason.store(
			static_cast<std::uint32_t>(_history.LastResetReason()), std::memory_order_relaxed);
		const bool useHistory = historyFrame.useHistory && temporalEnabled;
		_historyValidLastFrame.store(useHistory, std::memory_order_relaxed);

		try {
			cs::engine::ComputeOMScope scope(context);

			if (_history.ConsumeClearPending()) {
				ClearTemporalHistory(context);
			}

			cs::render::annotation::ScopedEvent annotationScope(
				"ScreenSpaceGI/Generate");
			const float texWidth = static_cast<float>(_allocW);
			const float texHeight = static_cast<float>(_allocH);
			const float frameWidth = static_cast<float>(frameW);
			const float frameHeight = static_cast<float>(frameH);
			const float prevFrameWidth =
				useHistory ? static_cast<float>(_prevFrameW) : frameWidth;
			const float prevFrameHeight =
				useHistory ? static_cast<float>(_prevFrameH) : frameHeight;

			// one screen-space radius drives the sweep; AO and GI cut it at their own fractions
			const float effectRadius = std::max(
				1.0f, std::max(_settings.aoRadius, _settings.giRadius));

			XeGTAOCB xegtaoCB{};
			xegtaoCB.NDCToViewMul[0] = worldNdcToViewMul.x;
			xegtaoCB.NDCToViewMul[1] = worldNdcToViewMul.y;
			xegtaoCB.NDCToViewMul[2] = worldNdcToViewMul.z;
			xegtaoCB.NDCToViewMul[3] = worldNdcToViewMul.w;
			xegtaoCB.NDCToViewAdd[0] = worldNdcToViewAdd.x;
			xegtaoCB.NDCToViewAdd[1] = worldNdcToViewAdd.y;
			xegtaoCB.NDCToViewAdd[2] = worldNdcToViewAdd.z;
			xegtaoCB.NDCToViewAdd[3] = worldNdcToViewAdd.w;
			xegtaoCB.TexDim[0] = texWidth;
			xegtaoCB.TexDim[1] = texHeight;
			xegtaoCB.RcpTexDim[0] = 1.0f / texWidth;
			xegtaoCB.RcpTexDim[1] = 1.0f / texHeight;
			xegtaoCB.FrameDim[0] = frameWidth;
			xegtaoCB.FrameDim[1] = frameHeight;
			xegtaoCB.RcpFrameDim[0] = 1.0f / frameWidth;
			xegtaoCB.RcpFrameDim[1] = 1.0f / frameHeight;
			xegtaoCB.PrevFrameDim[0] = prevFrameWidth;
			xegtaoCB.PrevFrameDim[1] = prevFrameHeight;
			xegtaoCB.RcpPrevFrameDim[0] = 1.0f / prevFrameWidth;
			xegtaoCB.RcpPrevFrameDim[1] = 1.0f / prevFrameHeight;
			xegtaoCB.FrameIndex = static_cast<std::uint32_t>(state->frameCount);
			xegtaoCB.NumSlices = static_cast<std::uint32_t>(_settings.numSlices);
			xegtaoCB.NumSteps = static_cast<std::uint32_t>(_settings.numSteps);
			xegtaoCB.MinScreenRadius = _settings.minScreenRadius * frameWidth;
			xegtaoCB.AORadius = std::clamp(_settings.aoRadius / effectRadius, 0.0f, 1.0f);
			xegtaoCB.EffectRadius = effectRadius;
			xegtaoCB.Thickness = _settings.thickness;
			xegtaoCB.GIRadius = std::clamp(_settings.giRadius / effectRadius, 0.0f, 1.0f);
			// Metric-scale fades suppress almost all AO.
			xegtaoCB.DepthFadeRange[0] = _settings.depthFadeStart;
			xegtaoCB.DepthFadeRange[1] = _settings.depthFadeEnd;
			const float depthFadeSpan = _settings.depthFadeEnd - _settings.depthFadeStart;
			xegtaoCB.DepthFadeScaleConst = depthFadeSpan > 1.0f ? 1.0f / depthFadeSpan : 1.0f;
			xegtaoCB.BlurRadius = _settings.blurRadius;
			xegtaoCB.DistanceNormalisation = _settings.distanceNormalisation;
			xegtaoCB.NormalDisocclusion = _settings.normalDisocclusion;
			xegtaoCB.DepthDisocclusion = _settings.depthDisocclusion;
			xegtaoCB.MaxAccumFrames = static_cast<std::uint32_t>(_settings.maxAccumFrames);
			xegtaoCB.TemporalFlags =
				(useHistory ? 2u : 0u) |
				(includeSourceB ? 4u : 0u);
			xegtaoCB.GISaturation = _settings.giSaturation;
			xegtaoCB.GIDistanceCompensation = _settings.giDistanceCompensation;
			xegtaoCB.GICompensationMaxDist = _settings.aoRadius;
			xegtaoCB.AOPower = _settings.aoPower;
			xegtaoCB.GIStrength = _settings.giStrength;
			const CameraTransform& previousCamera = useHistory ? _prevCamera : camera;
			const DirectX::XMFLOAT3 temporalPreviousOrigin =
				useHistory ? previousCameraOrigin : cameraOrigin;
			std::memcpy(
				xegtaoCB.PrevNDCToViewMul,
				previousCamera.ndcToViewMul,
				sizeof(previousCamera.ndcToViewMul));
			std::memcpy(
				xegtaoCB.PrevNDCToViewAdd,
				previousCamera.ndcToViewAdd,
				sizeof(previousCamera.ndcToViewAdd));
			std::memcpy(xegtaoCB.ViewToWorld, camera.rows, sizeof(camera.rows));
			std::memcpy(
				xegtaoCB.PrevViewToWorld,
				previousCamera.rows,
				sizeof(previousCamera.rows));
			xegtaoCB.CameraOrigin[0] = cameraOrigin.x;
			xegtaoCB.CameraOrigin[1] = cameraOrigin.y;
			xegtaoCB.CameraOrigin[2] = cameraOrigin.z;
			xegtaoCB.PrevCameraOrigin[0] = temporalPreviousOrigin.x;
			xegtaoCB.PrevCameraOrigin[1] = temporalPreviousOrigin.y;
			xegtaoCB.PrevCameraOrigin[2] = temporalPreviousOrigin.z;
			// Row-vector inverse projection, so z and w are its third and fourth columns.
			for (std::size_t row = 0; row < 4; ++row) {
				xegtaoCB.InvProjZ[row] = worldInvProj.m[row][2];
				xegtaoCB.InvProjW[row] = worldInvProj.m[row][3];
			}
			_xegtaoCB->Update(xegtaoCB);

			ComputePass pass(
				context, _xegtaoCB->CB(), { _pointClampSampler.get(), _linearClampSampler.get() });
			const auto& shaders = ActiveShaders();
			const std::uint32_t internalW = frameW >> resolutionMode;
			const std::uint32_t internalH = frameH >> resolutionMode;
			const auto groups8X = DispatchGroups(static_cast<int>(internalW), 8u);
			const auto groups8Y = DispatchGroups(static_cast<int>(internalH), 8u);
			const auto groups16X = DispatchGroups(static_cast<int>(internalW), 16u);
			const auto groups16Y = DispatchGroups(static_cast<int>(internalH), 16u);
			const auto readIndex = historyFrame.readIndex;
			const auto writeIndex = historyFrame.writeIndex;
			std::uint32_t temporalDispatches = 0;

			ID3D11ShaderResourceView* depthPrefilterSRVs[]{ depthSRV };
			ID3D11UnorderedAccessView* depthPrefilterUAVs[]{
				_workingDepthMipUAVs[0].get(),
				_workingDepthMipUAVs[1].get(),
				_workingDepthMipUAVs[2].get(),
				_workingDepthMipUAVs[3].get(),
				_workingDepthMipUAVs[4].get()
			};
			pass.Dispatch(
				shaders.prefilterDepth.get(), depthPrefilterSRVs, depthPrefilterUAVs,
				DispatchGroups(static_cast<int>(frameW), 16u),
				DispatchGroups(static_cast<int>(frameH), 16u),
				"ScreenSpaceGI/PrefilterDepth");

			const std::size_t variant = temporalEnabled ? 1u : 0u;
			if (radianceAvailable) {
				ID3D11ShaderResourceView* disoccSRVs[]{
					radianceSRV,
					radianceBSRV,
					_workingDepthTex->srv.get(),
					motionAvailable ? motionSRV : nullptr,
					_prevGeoTex[readIndex]->srv.get(),
					_accumTex[readIndex]->srv.get(),
					_bounceSHTex[readIndex]->srv.get(),
					_bounceCoCgTex[readIndex]->srv.get(),
					albedoSRV,
					emissiveSRV
				};
				ID3D11UnorderedAccessView* disoccUAVs[]{
					_radianceTempTex->uav.get(),
					_accumTex[writeIndex]->uav.get(),
					_bounceSHTex[writeIndex]->uav.get(),
					_bounceCoCgTex[writeIndex]->uav.get()
				};
				pass.Dispatch(
					shaders.radianceDisocc[variant].get(), disoccSRVs, disoccUAVs,
					groups8X, groups8Y, "ScreenSpaceGI/RadianceDisocclusion");
				if (temporalEnabled) {
					++temporalDispatches;
				}

				ID3D11ShaderResourceView* radianceSRVs[]{ _radianceTempTex->srv.get() };
				ID3D11UnorderedAccessView* radianceUAVs[]{
					_radianceMipUAVs[0].get(),
					_radianceMipUAVs[1].get(),
					_radianceMipUAVs[2].get(),
					_radianceMipUAVs[3].get(),
					_radianceMipUAVs[4].get()
				};
				pass.Dispatch(
					shaders.prefilterRadiance.get(), radianceSRVs, radianceUAVs,
					groups16X, groups16Y, "ScreenSpaceGI/PrefilterRadiance");
			}

			ID3D11ShaderResourceView* normalPrefilterSRVs[]{ normalSRV };
			ID3D11UnorderedAccessView* normalPrefilterUAVs[]{
				_normalMipUAVs[0].get(),
				_normalMipUAVs[1].get(),
				_normalMipUAVs[2].get(),
				_normalMipUAVs[3].get(),
				_normalMipUAVs[4].get()
			};
			pass.Dispatch(
				shaders.prefilterNormal.get(), normalPrefilterSRVs, normalPrefilterUAVs,
				groups16X, groups16Y, "ScreenSpaceGI/PrefilterNormal");

			if (radianceAvailable) {
				ID3D11ShaderResourceView* giSRVs[]{
					_workingDepthTex->srv.get(),
					_normalTex->srv.get(),
					_radianceTex->srv.get(),
					_noiseSRV.get(),
					_accumTex[writeIndex]->srv.get(),
					_bounceSHTex[writeIndex]->srv.get(),
					_bounceCoCgTex[writeIndex]->srv.get()
				};
				ID3D11UnorderedAccessView* giUAVs[]{
					_aoTex->uav.get(),
					_bounceSHRawTex->uav.get(),
					_bounceCoCgRawTex->uav.get(),
					_prevGeoTex[writeIndex]->uav.get()
				};
				pass.Dispatch(
					shaders.gi[variant].get(), giSRVs, giUAVs, groups8X, groups8Y,
					"ScreenSpaceGI/GI");
				if (temporalEnabled) {
					++temporalDispatches;
				}
				_bounceOutputsDirty = true;
				_bounceProducedLastFrame.store(true, std::memory_order_relaxed);
			} else {
				ID3D11ShaderResourceView* aoSRVs[]{
					_workingDepthTex->srv.get(),
					_normalTex->srv.get(),
					nullptr,
					_noiseSRV.get()
				};
				ID3D11UnorderedAccessView* aoUAVs[]{ _aoTex->uav.get() };
				pass.Dispatch(
					shaders.ao.get(), aoSRVs, aoUAVs, groups8X, groups8Y,
					"ScreenSpaceGI/AO");
				ClearBounceOutputs(context);
			}
			_occlusionOutputsDirty = true;
			_aoProducedLastFrame.store(true, std::memory_order_relaxed);

			// Upstream never blurs AO.
			if (radianceAvailable && _settings.enableBlur) {
				ID3D11ShaderResourceView* blurSRVs[]{
					_workingDepthTex->srv.get(),
					normalSRV,
					_accumTex[writeIndex]->srv.get(),
					_bounceSHRawTex->srv.get(),
					_bounceCoCgRawTex->srv.get()
				};
				ID3D11UnorderedAccessView* blurUAVs[]{
					_accumBlurTex->uav.get(),
					_bounceSHTex[writeIndex]->uav.get(),
					_bounceCoCgTex[writeIndex]->uav.get()
				};
				pass.Dispatch(
					shaders.blur[variant].get(), blurSRVs, blurUAVs,
					groups8X, groups8Y, "ScreenSpaceGI/Blur");
				if (temporalEnabled) {
					++temporalDispatches;
					cs::render::annotation::ScopedEvent historyScope(
						"ScreenSpaceGI/CopyAccumulationHistory");
					context->CopyResource(
						_accumTex[writeIndex]->resource.get(), _accumBlurTex->resource.get());
				}
				_bounceDenoisedLastFrame.store(true, std::memory_order_relaxed);
			} else if (radianceAvailable) {
				cs::render::annotation::ScopedEvent historyScope(
					"ScreenSpaceGI/CopyBounceHistory");
				context->CopyResource(
					_bounceSHTex[writeIndex]->resource.get(), _bounceSHRawTex->resource.get());
				context->CopyResource(
					_bounceCoCgTex[writeIndex]->resource.get(), _bounceCoCgRawTex->resource.get());
			}

			_upsampledLastFrame = resolutionMode != 0;
			if (_upsampledLastFrame) {
				// Without GI the bounce history is cleared, so it upsamples to zero.
				ID3D11ShaderResourceView* upsampleSRVs[]{
					_workingDepthTex->srv.get(),
					_aoTex->srv.get(),
					_bounceSHTex[writeIndex]->srv.get(),
					_bounceCoCgTex[writeIndex]->srv.get()
				};
				ID3D11UnorderedAccessView* upsampleUAVs[]{
					_aoUpsampledTex->uav.get(),
					_bounceSHUpsampledTex->uav.get(),
					_bounceCoCgUpsampledTex->uav.get()
				};
				pass.Dispatch(
					shaders.upsample.get(), upsampleSRVs, upsampleUAVs,
					DispatchGroups(static_cast<int>(frameW), 8u),
					DispatchGroups(static_cast<int>(frameH), 8u),
					"ScreenSpaceGI/Upsample");
			}
			if (radianceAvailable) {
				_history.Publish();
				_prevCamera = camera;
				_prevFrameW = frameW;
				_prevFrameH = frameH;
			} else {
				ClearBounceOutputs(context);
			}
			_temporalDispatchesLastFrame.store(temporalDispatches, std::memory_order_relaxed);
		} catch (const std::exception& e) {
			ResetHistory(ssgi::HistoryResetReason::kGenerationFailed);
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			_aoProducedLastFrame.store(false, std::memory_order_relaxed);
			_bounceProducedLastFrame.store(false, std::memory_order_relaxed);
			_bounceDenoisedLastFrame.store(false, std::memory_order_relaxed);
			_radianceAvailableLastFrame.store(false, std::memory_order_relaxed);
			_historyValidLastFrame.store(false, std::memory_order_relaxed);
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "SSGI generation failed: {}", e.what());
			}
		} catch (...) {
			ResetHistory(ssgi::HistoryResetReason::kGenerationFailed);
			ClearOcclusionOutputs(context);
			ClearBounceOutputs(context);
			_aoProducedLastFrame.store(false, std::memory_order_relaxed);
			_bounceProducedLastFrame.store(false, std::memory_order_relaxed);
			_bounceDenoisedLastFrame.store(false, std::memory_order_relaxed);
			_radianceAvailableLastFrame.store(false, std::memory_order_relaxed);
			_historyValidLastFrame.store(false, std::memory_order_relaxed);
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "SSGI generation failed.");
			}
		}
	}

	void ScreenSpaceGI::SaveCompositionBindings()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ?
			reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			nullptr;
		if (!_compositionBindingSnapshot.Save(context, kCompositionPSSlot) &&
			_compositionBindingSnapshot.IsSaved()) {
			CS_LOG_ONCE(
				L,
				spdlog::level::err,
				"SSGI composition binding scopes overlap; preserving the active snapshot.");
		}
	}

	void ScreenSpaceGI::RestoreCompositionBindings()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ?
			reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			nullptr;
		_compositionBindingSnapshot.Restore(context);
	}

	void ScreenSpaceGI::BindComposition(ID3D11DeviceContext* a_context)
	{
		if (!a_context || !_started.load(std::memory_order_acquire)) {
			return;
		}

		auto* normalSRV =
			cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferNormal);
		const bool compositionReady =
			_aoProducedLastFrame.load(std::memory_order_relaxed) &&
			IsGeneratorReady() &&
			normalSRV;
		const auto publishedIndex = _history.ReadIndex();
		const bool upsampled = _upsampledLastFrame;
		ID3D11ShaderResourceView* composition[kCompositionPSSlotCount] = {
			compositionReady ? SRVOf(upsampled ? _aoUpsampledTex : _aoTex) : nullptr,
			compositionReady ? SRVOf(upsampled ? _bounceSHUpsampledTex : _bounceSHTex[publishedIndex]) : nullptr,
			compositionReady ? SRVOf(upsampled ? _bounceCoCgUpsampledTex : _bounceCoCgTex[publishedIndex]) : nullptr,
			compositionReady ? normalSRV : nullptr
		};
		a_context->PSSetShaderResources(
			kCompositionPSSlot, kCompositionPSSlotCount, composition);

		_normalBoundLastFrame.store(compositionReady, std::memory_order_relaxed);
		_compositionBindsLastFrame.fetch_add(1, std::memory_order_relaxed);
		if (!normalSRV) {
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"SSGI normal source is unavailable; composition is neutral for this draw.");
		}
	}

	void ScreenSpaceGI::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto resetReason = static_cast<ssgi::HistoryResetReason>(
			_lastResetReason.load(std::memory_order_relaxed));
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("injection_registered", _injectionRegistered.load(std::memory_order_acquire))
			.Field("resources_ready", _resourcesReady.load(std::memory_order_acquire))
			.Field("resource_init_failed", _resourceInitFailed.load(std::memory_order_acquire))
			.Field("ao_produced", _aoProducedLastFrame.load(std::memory_order_relaxed))
			.Field("vanilla_ssao_applied", _vanillaSSAOAppliedLastFrame.load(std::memory_order_relaxed))
			.Field("radiance_available", _radianceAvailableLastFrame.load(std::memory_order_relaxed))
			.Field("bounce_produced", _bounceProducedLastFrame.load(std::memory_order_relaxed))
			.Field("bounce_denoised", _bounceDenoisedLastFrame.load(std::memory_order_relaxed))
			.Field("normal_bound", _normalBoundLastFrame.load(std::memory_order_relaxed))
			.Field("history_valid", _historyValidLastFrame.load(std::memory_order_relaxed))
			.Field("motion_available", _motionAvailableLastFrame.load(std::memory_order_relaxed))
			.Field(
				"temporal_dispatches",
				static_cast<std::int64_t>(
					_temporalDispatchesLastFrame.load(std::memory_order_relaxed)))
			.Field(
				"reset_count",
				static_cast<std::int64_t>(_historyResetCount.load(std::memory_order_relaxed)))
			.Field("last_reset_reason", std::string_view(ssgi::HistoryResetReasonName(resetReason)))
			.Field("tiled_lighting_active", _tiledLightingActive.load(std::memory_order_relaxed))
			.Field("tiled_b_available", _tiledBAvailable.load(std::memory_order_relaxed))
			.Field(
				"radiance_source_count",
				static_cast<std::int64_t>(_radianceSourceCount.load(std::memory_order_relaxed)))
			.Field(
				"repeat_callbacks",
				static_cast<std::int64_t>(_repeatCallbacks.load(std::memory_order_relaxed)))
			.Field("contaminated_light_classes", kContaminatedLightClasses)
			.Field("contaminated_routes", kContaminatedRoutes)
			.Field("camera_ready", _cameraReadyLastFrame.load(std::memory_order_relaxed))
			.Field(
				"camera_origin",
				std::format(
					"{} {} {}",
					_cameraOriginXLastFrame.load(std::memory_order_relaxed),
					_cameraOriginYLastFrame.load(std::memory_order_relaxed),
					_cameraOriginZLastFrame.load(std::memory_order_relaxed)))
			.Field(
				"camera_previous_origin",
				std::format(
					"{} {} {}",
					_cameraPreviousOriginXLastFrame.load(std::memory_order_relaxed),
					_cameraPreviousOriginYLastFrame.load(std::memory_order_relaxed),
					_cameraPreviousOriginZLastFrame.load(std::memory_order_relaxed)))
			.Field(
				"composition_binds",
				static_cast<std::int64_t>(
					_compositionBindsLastFrame.load(std::memory_order_relaxed)))
			.Dimensions("working", _allocW, _allocH)
			.Field("generation", static_cast<std::int64_t>(_generation));
	}

	void ScreenSpaceGI::DrawSettings()
	{
		// Session-only, as upstream.
		static bool showAdvanced = false;
		settings::SettingsEdit edit{ *this };
		const auto tooltip = [](const char* a_text) {
			if (dmui::ui::IsItemHovered())
				dmui::ui::SetTooltip("%s", a_text);
		};
		const auto slider = [&](const char* a_label, auto Settings::* a_member, const char* a_format = nullptr) {
			const auto range = ssgi_settings::kSchema.EditRange(a_member);
			edit.Continuous(dmui::ui::SliderScalar(
				a_label, &(_settings.*a_member), &range.min, &range.max, a_format));
		};
		const auto percentSlider = [&](const char* a_label, float Settings::* a_member) {
			const auto range = ssgi_settings::kSchema.EditRange(a_member);
			float percent = _settings.*a_member * 100.0f;
			const float minimum = range.min * 100.0f;
			const float maximum = range.max * 100.0f;
			if (edit.Continuous(dmui::ui::SliderScalar(a_label, &percent, &minimum, &maximum, "%.1f%%")))
				_settings.*a_member = percent * 0.01f;
		};
		const auto applyPreset = [&](int a_slices, int a_steps, int a_resolutionMode, bool a_gi) {
			_settings.numSlices = a_slices;
			_settings.numSteps = a_steps;
			if (a_resolutionMode >= 0)
				_settings.resolutionMode = a_resolutionMode;
			_settings.enableBlur = true;
			_settings.enableGI = a_gi;
			edit.Discrete(true);
		};

		dmui::ui::Separator();
		dmui::ui::Text("Toggles");
		static_cast<void>(dmui::ui::Checkbox("Show Advanced Options", &showAdvanced));
		edit.Discrete(dmui::ui::Checkbox("Enabled", &_settings.enabled));
		tooltip("Enable Screen Space Global Illumination. When disabled, all other settings are ignored.");
		dmui::ui::BeginDisabled(!_settings.enabled);
		edit.Discrete(dmui::ui::Checkbox("Indirect Lighting (IL)", &_settings.enableGI));
		dmui::ui::EndDisabled();
		edit.Discrete(dmui::ui::Checkbox("Vanilla SSAO", &_settings.enableVanillaSSAO));
		tooltip("Enable Fallout 4's built-in SSAO. Usually disabled when using SSGI to avoid double-darkening.");

		dmui::ui::Separator();
		dmui::ui::Text("Quality/Performance");
		dmui::ui::BeginDisabled(!_settings.enabled);
		if (dmui::ui::Button("AO only"))
			applyPreset(1, 6, -1, false);
		tooltip("1 Slice, 6 Steps, blur enabled, no GI");
		dmui::ui::SameLine();
		if (dmui::ui::Button("Low"))
			applyPreset(10, 12, 2, true);
		tooltip("Quarter res and blurry.");
		dmui::ui::SameLine();
		if (dmui::ui::Button("Standard"))
			applyPreset(4, 8, 1, true);
		tooltip("Half res and somewhat stable.");
		dmui::ui::SameLine();
		if (dmui::ui::Button("Extreme"))
			applyPreset(4, 8, 0, true);
		tooltip("Full res and clean.");
		dmui::ui::SameLine();
		if (dmui::ui::Button("Reference"))
			applyPreset(8, 10, 0, true);
		tooltip("Reference mode.");
		if (showAdvanced) {
			slider("Slices", &Settings::numSlices);
			tooltip("How many directions do the samples take.\nControls noise.");
			slider("Steps Per Slice", &Settings::numSteps);
			tooltip("How many samples does it take in one direction.\nControls accuracy of lighting, and noise when effect radius is large.");
		}
		static constexpr std::array<const char*, kResolutionModes> kResolutionLabels{ "Full Res", "Half Res", "Quarter Res" };
		for (int mode = 0; mode < static_cast<int>(kResolutionModes); ++mode) {
			if (mode != 0)
				dmui::ui::SameLine();
			if (dmui::ui::Selectable(kResolutionLabels[mode], _settings.resolutionMode == mode, dmui::ui::SelectableFlags::kNone, { 120.0f, 0.0f })) {
				_settings.resolutionMode = mode;
				edit.Discrete(true);
			}
		}
		dmui::ui::EndDisabled();

		dmui::ui::Separator();
		dmui::ui::Text("Visual");
		dmui::ui::BeginDisabled(!_settings.enabled);
		slider("AO Power", &Settings::aoPower, "%.2f");
		dmui::ui::BeginDisabled(!_settings.enableGI);
		slider("IL Source Brightness", &Settings::giStrength, "%.2f");
		dmui::ui::EndDisabled();
		slider("AO radius", &Settings::aoRadius, "%.1f units");
		tooltip("A smaller radius produces tighter AO.");
		dmui::ui::BeginDisabled(!_settings.enableGI);
		slider("IL radius", &Settings::giRadius, "%.1f units");
		tooltip("A larger radius produces wider IL.");
		dmui::ui::EndDisabled();
		if (showAdvanced) {
			slider("Min Screen Radius", &Settings::minScreenRadius, "%.3f");
			tooltip("The minimum screen-space effect radius as proportion of display width, to prevent far field AO being too small.");
		}
		slider("Depth Fade Near", &Settings::depthFadeStart, "%.0f units");
		slider("Depth Fade Far", &Settings::depthFadeEnd, "%.0f units");
		tooltip("Distance range where depth-based effects fade out.");
		if (showAdvanced) {
			slider("Thickness", &Settings::thickness, "%.1f units");
			tooltip("How thick the occluders are. Only affects AO.");
		}
		dmui::ui::EndDisabled();

		dmui::ui::Separator();
		dmui::ui::Text("Visual - IL");
		dmui::ui::BeginDisabled(!_settings.enabled || !_settings.enableGI);
		if (showAdvanced) {
			slider("IL Distance Compensation", &Settings::giDistanceCompensation, "%.1f");
			tooltip("Brighten/Dimming further radiance samples.");
		}
		percentSlider("IL Saturation", &Settings::giSaturation);
		dmui::ui::EndDisabled();

		dmui::ui::Separator();
		dmui::ui::Text("Denoising");
		dmui::ui::BeginDisabled(!_settings.enabled);
		edit.Discrete(dmui::ui::Checkbox("Temporal Denoiser", &_settings.enableTemporalDenoiser));
		dmui::ui::SameLine();
		edit.Discrete(dmui::ui::Checkbox("Blur", &_settings.enableBlur));
		if (showAdvanced) {
			dmui::ui::BeginDisabled(!_settings.enableTemporalDenoiser);
			slider("Max Frame Accumulation", &Settings::maxAccumFrames);
			tooltip("How many past frames to accumulate results with. Higher values are less noisy but potentially cause ghosting.");
			dmui::ui::EndDisabled();
			dmui::ui::BeginDisabled(!_settings.enableTemporalDenoiser && !_settings.enableGI);
			percentSlider("Movement Disocclusion", &Settings::depthDisocclusion);
			tooltip("If a pixel has moved too far from the last frame, its radiance will not be carried to this frame.\nLower values are stricter.");
			dmui::ui::EndDisabled();
			dmui::ui::BeginDisabled(!_settings.enableBlur);
			slider("Blur Radius", &Settings::blurRadius, "%.1f px");
			slider("Geometry Weight", &Settings::distanceNormalisation, "%.2f");
			tooltip("Higher value makes the blur more sensitive to differences in geometry.");
			dmui::ui::EndDisabled();
		}
		dmui::ui::EndDisabled();

		dmui::ui::Separator();		const char* status = _resourceInitFailed.load(std::memory_order_acquire) ? "failed" :
			(_resourcesReady.load(std::memory_order_acquire) ? "ready" : "not ready");
		dmui::ui::TextDisabled(
			"Resources: %s (%ux%u) | composition binds: %u | generation: %u",
			status,
			_allocW,
			_allocH,
			_compositionBindsLastFrame.load(std::memory_order_relaxed),
			_generation);
		dmui::ui::TextDisabled(
			"History: %s | motion: %s | radiance sources: %u | resets: %u (%s)",
			_historyValidLastFrame.load(std::memory_order_relaxed) ? "in use" : "seeding",
			_motionAvailableLastFrame.load(std::memory_order_relaxed) ? "yes" : "no",
			_radianceSourceCount.load(std::memory_order_relaxed),
			_historyResetCount.load(std::memory_order_relaxed),
			ssgi::HistoryResetReasonName(
				static_cast<ssgi::HistoryResetReason>(
					_lastResetReason.load(std::memory_order_relaxed))));
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void ScreenSpaceGI::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(ScreenSpaceGI::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
