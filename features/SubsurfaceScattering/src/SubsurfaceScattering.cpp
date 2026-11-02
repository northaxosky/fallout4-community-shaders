#include "SubsurfaceScattering.h"

#include <DearModdingUI/Client.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <span>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/CanonicalDepth.h"
#include "Render/ComputePasses.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/FrameBindings.h"
#include "Render/FrameProfiler.h"
#include "Render/PrepassExtraTargets.h"
#include "Render/PrepassTechnique.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"
#include "Render/ShaderObservers.h"
#include "Render/ShaderSubclassHooks.h"
#include "Render/SharedData.h"
#include "Render/TemporalRenderer.h"
#include "Settings/SettingsPersistence.h"
#include "SkinProfile.h"
#include "SubsurfaceScatteringKernel.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.subsurfacescattering");
		using RT = RE::BSGraphics::RenderTargetID;
		using cs::render::ComputePasses;
		using cs::render::CreateComputeTexture;
		namespace sss = subsurface_scattering;

		// Burley and the vertical pass read the target they write.
		bool SupportsTypedUAV(ID3D11Device* a_device, DXGI_FORMAT a_format)
		{
			D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{ a_format, 0 };
			constexpr UINT required = D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE;
			return a_device && SUCCEEDED(a_device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support))) &&
			       (support.OutFormatSupport2 & required) == required;
		}
	}

	SubsurfaceScattering* SubsurfaceScattering::GetSingleton()
	{
		static SubsurfaceScattering instance;
		return &instance;
	}

	bool SubsurfaceScattering::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(sss::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(sss::kSchema, _settings);
		return true;
	}

	bool SubsurfaceScattering::SaveSettings()
	{
		return settings::SaveDelta(sss::kSchema, GetConfigKey(), _settings, *L);
	}

	void SubsurfaceScattering::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings(*this, cs::engine::AttachPrepassExtraTargets)) {
			FailLoad("Subsurface Scattering shader contribution registration failed.");
			return;
		}
		_profileTarget = cs::engine::RegisterPrepassExtraTarget(*this, { .name = "SubsurfaceScattering/Profile", .rtvSlot = kProfileRTVSlot, .format = DXGI_FORMAT_R8_UNORM, .active = [this] { return CanRun(); } });
		if (!_profileTarget || !cs::engine::RegisterPrepassDrawClassifier(*this, cs::engine::PrepassLaneComponent::kW, &sss::IsHumanSkinDraw)) {
			FailLoad("Subsurface Scattering skin profile registration failed.");
			return;
		}
		if (!cs::engine::RegisterPreDeferredLightsImpl(*this, [] { GetSingleton()->ApplyNativeControls(); }) ||
			!cs::engine::RegisterPreDeferredComposite(*this, [] { GetSingleton()->OnPreDeferredComposite(); }) ||
			!cs::engine::RegisterPreSecondDeferredComposite(*this, [] { GetSingleton()->OnPreSecondDeferredComposite(); })) {
			FailLoad("Subsurface Scattering render hooks could not register.");
			return;
		}
		try {
			cs::engine::ObserveShaderTechnique(cs::engine::ShaderInjectionTarget::kDeferredPrepass, { &OnPrepassTechnique });
		} catch (const std::exception& e) {
			FailLoad(std::format("Subsurface Scattering skin-draw hook failed: {}", e.what()));
			return;
		}
		_registrationsReady = true;
	}

	void SubsurfaceScattering::OnDataLoaded()
	{
		sss::ResolveBaseRaceKeywords();
	}

	bool SubsurfaceScattering::CompileShaders()
	{
		constexpr auto extract = L"Data\\Shaders\\SubsurfaceScattering\\DiffuseExtractionCS.hlsl";
		constexpr auto separable = L"Data\\Shaders\\SubsurfaceScattering\\SeparableSSSCS.hlsl";
		_prepare = cs::util::CompileComputeShader("SubsurfaceScattering/Prepare.CS", L"Data\\Shaders\\FO4\\SubsurfaceScattering\\PrepareCS.hlsl");
		_extract = cs::util::CompileComputeShader("SubsurfaceScattering/DiffuseExtraction.CS", extract);
		_horizontal = cs::util::CompileComputeShader("SubsurfaceScattering/HorizontalBlur.CS", separable, { { "HORIZONTAL", "" } });
		_vertical = cs::util::CompileComputeShader("SubsurfaceScattering/VerticalBlur.CS", separable);
		_burley = cs::util::CompileComputeShader("SubsurfaceScattering/Burley.CS", separable, { { "BURLEY", "" } });
		const bool ready = static_cast<bool>(_prepare) && static_cast<bool>(_extract) && static_cast<bool>(_horizontal) && static_cast<bool>(_vertical) && static_cast<bool>(_burley);
		return ready;
	}

	void SubsurfaceScattering::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_registrationsReady || !a_device)
			return;
		try {
			_constants = std::make_unique<cs::buffer::ConstantBuffer>(cs::buffer::ConstantBufferDesc<sss::Constants>());
			_consumer = std::make_unique<cs::buffer::ConstantBuffer>(cs::buffer::ConstantBufferDesc<ConsumerCB>());
			_consumerData.reset();
			_constants->SetName("SubsurfaceScattering/Constants.Buffer");
			_consumer->SetName("SubsurfaceScattering/Consumer.Buffer");
			_failed = !CompileShaders();
		} catch (const std::exception& e) {
			_failed = true;
			L->error("Subsurface Scattering initialization failed: {}", e.what());
		}
	}

	bool SubsurfaceScattering::EnsureResources(ID3D11Texture2D* a_target)
	{
		D3D11_TEXTURE2D_DESC target{};
		a_target->GetDesc(&target);
		if (_resourcesReady && target.Width == _textures.width && target.Height == _textures.height && target.Format == _textures.format)
			return true;
		_resourcesReady = false;
		if (!SupportsTypedUAV(cs::engine::GetDevice(), target.Format)) {
			_unsupported = true;
			L->error("Subsurface Scattering is unavailable: the device lacks typed UAV loads for DXGI format {}.", static_cast<int>(target.Format));
			return false;
		}
		try {
			Resources resources;
			resources.width = target.Width;
			resources.height = target.Height;
			resources.format = target.Format;
			resources.mask = CreateComputeTexture(target.Width, target.Height, DXGI_FORMAT_R8G8_UNORM, "SubsurfaceScattering/Mask");
			resources.normal = CreateComputeTexture(target.Width, target.Height, DXGI_FORMAT_R16G16_FLOAT, "SubsurfaceScattering/Normal");
			resources.albedo = CreateComputeTexture(target.Width, target.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, "SubsurfaceScattering/Albedo");
			resources.diffuseNoAlbedo = CreateComputeTexture(target.Width, target.Height, target.Format, "SubsurfaceScattering/DiffuseNoAlbedo");
			resources.horizontal = CreateComputeTexture(target.Width, target.Height, target.Format, "SubsurfaceScattering/BlurHorizontalTemp");
			_textures = std::move(resources);
			_resourcesReady = true;
			return true;
		} catch (const std::exception& e) {
			_failed = true;
			L->error("Subsurface Scattering resource creation failed: {}", e.what());
			return false;
		}
	}

	void SubsurfaceScattering::UpdateKernels()
	{
		const auto& s = _settings;
		const std::array<float, 12> key{
			s.baseProfileStrength[0], s.baseProfileStrength[1], s.baseProfileStrength[2],
			s.baseProfileFalloff[0], s.baseProfileFalloff[1], s.baseProfileFalloff[2],
			s.humanProfileStrength[0], s.humanProfileStrength[1], s.humanProfileStrength[2],
			s.humanProfileFalloff[0], s.humanProfileFalloff[1], s.humanProfileFalloff[2]
		};
		if (_kernelsBuilt && key == _kernelKey)
			return;
		const std::span kernels{ _data.Kernels };
		sss::CalculateKernel(s.baseProfileStrength, s.baseProfileFalloff, kernels.first<sss::kKernelSamples>());
		sss::CalculateKernel(s.humanProfileStrength, s.humanProfileFalloff, kernels.last<sss::kKernelSamples>());
		_kernelKey = key;
		_kernelsBuilt = true;
	}

	void SubsurfaceScattering::UpdateConstants(const cs::engine::WorldCameraRecord& a_camera)
	{
		const auto& s = _settings;
		UpdateKernels();
		// FO4: row-vector projections keep upstream's m[0][0] at _11.
		_data.SSSS_FOVY = std::atan(1.0f / a_camera.Projection._11) * 2.0f * (180.0f / 3.14159265359f);
		_data.BaseProfile = { s.baseProfileBlurRadius, s.baseProfileThickness, 0, 0 };
		_data.HumanProfile = { s.humanProfileBlurRadius, s.humanProfileThickness, 0, 0 };
		_data.BurleySamples = s.burleySamples;
		// Burley always removes and reapplies the full albedo.
		_data.ScatterMode = s.ssMode == sss::kSeparable ?
		                        static_cast<std::uint32_t>(std::clamp(s.scatterMode, static_cast<int>(sss::kPreScatter), static_cast<int>(sss::kPreAndPostScatter))) :
		                        static_cast<std::uint32_t>(sss::kPostScatter);
		_data.MeanFreePathBase = { s.meanFreePathBase[0], s.meanFreePathBase[1], s.meanFreePathBase[2], s.meanFreePathBase[3] };
		_data.MeanFreePathHuman = { s.meanFreePathHuman[0], s.meanFreePathHuman[1], s.meanFreePathHuman[2], s.meanFreePathHuman[3] };
		_constants->Update(_data);
	}

	void SubsurfaceScattering::ApplyNativeControls()
	{
		const auto controls = cs::engine::GetNativeSubsurfaceControls();
		if (!controls)
			return;
		// A feature that cannot dispatch must leave the native pipeline stock.
		if (!CanRun()) {
			RestoreNativeControls();
			return;
		}
		if (!_native)
			_native = NativeSnapshot{ controls->screenSpaceSubsurfaceScatteringEnabled, controls->characterLightEnabled,
				controls->characterLightParams.r, controls->characterLightParams.g };
		// FO4: the t15 hook needs the native flag, which adds a second composite pass.
		controls->screenSpaceSubsurfaceScatteringEnabled = SkinDrawnThisFrame();
		controls->characterLightEnabled = _settings.enableCharacterLighting;
		if (_settings.enableCharacterLighting) {
			// FO4: DFLight reads only the rim (x) and fill (y) character light lanes.
			controls->characterLightParams.r = _native->rim * _settings.characterLightingStrength;
			controls->characterLightParams.g = _native->fill * _settings.characterLightingStrength;
		}
		_nativeApplied = true;
	}

	void SubsurfaceScattering::RestoreNativeControls() noexcept
	{
		const auto controls = cs::engine::GetNativeSubsurfaceControls();
		if (controls && _native) {
			controls->screenSpaceSubsurfaceScatteringEnabled = _native->screenSpaceSubsurfaceScattering;
			controls->characterLightEnabled = _native->characterLight;
			controls->characterLightParams.r = _native->rim;
			controls->characterLightParams.g = _native->fill;
		}
		_native.reset();
		_nativeApplied = false;
	}

	void SubsurfaceScattering::UpdateConsumer(bool a_produced)
	{
		const ConsumerCB data{ a_produced, {} };
		if (_consumerData && _consumerData->Produced == data.Produced)
			return;
		_consumer->Update(data);
		_consumerData = data;
	}

	void SubsurfaceScattering::OnPrepassTechnique(RE::BSShader*, std::uint32_t a_technique) noexcept
	{
		namespace pt = cs::engine::prepass_technique;
		if (!(a_technique & (pt::kSkinTint | pt::kFace)))
			return;
		auto* self = GetSingleton();
		self->_skinFrame.store(cs::engine::GetFrameCount(), std::memory_order_relaxed);
		self->_skinBuckets.fetch_add(1, std::memory_order_relaxed);
	}

	void SubsurfaceScattering::OnPreDeferredComposite()
	{
		_produced = false;
		auto* context = cs::engine::GetImmediateContext();
		if (!context || !_consumer)
			return;
		// Every frame starts on the stock blur; the dispatch opts out of it.
		UpdateConsumer(false);
		auto* cb = _consumer->CB();
		cs::engine::BindFrameConstantBuffers(context, cs::engine::ShaderStage::kPixel, kConsumerSlot, 1, &cb);
	}

	void SubsurfaceScattering::OnPreSecondDeferredComposite()
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* manager = cs::engine::GetRenderTargetManager();
		if (!CanRun() || !context || !manager || !_consumer ||
			!_prepare || !_extract || !_horizontal || !_vertical || !_burley)
			return;
		// Upstream dispatches only on frames that drew skin.
		if (!SkinDrawnThisFrame()) {
			_skippedFrames.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		const auto camera = cs::engine::GetWorldCameraRecord();
		auto* depth = cs::render::GetCanonicalSceneDepthSRV();
		// FO4: raw depth marks first-person pixels, which canonical depth cannot.
		auto* rawDepth = cs::engine::GetSceneDepthSRV();
		auto* normals = cs::engine::GetRenderTargetSRV(RT::kGBufferNormal);
		// FO4: GBufferAmbient carries the material class in its alpha.
		auto* material = cs::engine::GetRenderTargetSRV(RT::kGBufferAmbient);
		auto* albedo = cs::engine::GetRenderTargetSRV(RT::kGBufferAlbedoSpecular);
		auto* profile = _profileTarget ? cs::engine::GetPrepassExtraTargetSRV(*_profileTarget) : nullptr;
		auto* color = cs::engine::GetRenderTargetSRV(RT::kMainCopy);
		auto* colorUAV = cs::engine::GetRenderTargetUAV(RT::kMainCopy);
		auto* colorTexture = cs::engine::GetRenderTargetTexture(RT::kMainCopy);
		if (!camera || !depth || !rawDepth || !normals || !material || !albedo || !profile || !color || !colorUAV || !colorTexture ||
			!cs::render::IsSharedDataReady() || !EnsureResources(colorTexture))
			return;
		auto& t = _textures;
		const UINT width = static_cast<UINT>(std::floor(t.width * manager->GetDynamicWidthRatio()));
		const UINT height = static_cast<UINT>(std::floor(t.height * manager->GetDynamicHeightRatio()));
		if (!width || !height)
			return;
		try {
			UpdateConstants(*camera);
			// FO4: the main copy is the second composite's t10; blur it in place.
			cs::engine::ComputeOMScope scope(context, 8, 2, 6, 2);
			cs::render::ScopedComputeSharedDataBinding substrate(context);
			auto* sampler = cs::render::GetPointClampSampler();
			context->CSSetSamplers(0, 1, &sampler);
			auto* constants = _constants->CB();
			context->CSSetConstantBuffers(kConstantsSlot, 1, &constants);
			ComputePasses pass(context);
			cs::render::annotation::ScopedEvent dispatch("SubsurfaceScattering::Dispatch", false);
			const auto scatter = [&](ID3D11ComputeShader* a_shader, ID3D11ShaderResourceView* a_input,
									 ID3D11UnorderedAccessView* a_output, const char* a_name) {
				pass.Run(a_shader, { a_input, depth, t.mask->srv.get(), t.albedo->srv.get(), t.normal->srv.get() },
					{ a_output }, width, height, 8, a_name);
			};
			pass.Run(_prepare.get(), { normals, material, albedo, rawDepth, profile },
				{ t.mask->uav.get(), t.normal->uav.get(), t.albedo->uav.get() }, width, height, 8, "SubsurfaceScattering::Prepare");
			pass.Run(_extract.get(), { color, nullptr, nullptr, t.albedo->srv.get() },
				{ t.diffuseNoAlbedo->uav.get() }, width, height, 8, "SubsurfaceScattering::DiffuseExtraction");
			if (_settings.ssMode == sss::kSeparable) {
				scatter(_horizontal.get(), t.diffuseNoAlbedo->srv.get(), t.horizontal->uav.get(), "SubsurfaceScattering::HorizontalBlur");
				scatter(_vertical.get(), t.horizontal->srv.get(), colorUAV, "SubsurfaceScattering::VerticalBlur");
			} else {
				scatter(_burley.get(), t.diffuseNoAlbedo->srv.get(), colorUAV, "SubsurfaceScattering::Burley");
			}
			_lastMode = static_cast<std::uint8_t>(_settings.ssMode);
		} catch (const std::exception& e) {
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Subsurface Scattering dispatch failed: {}", e.what());
			return;
		}
		// UAV write unbound the main copy behind engine t10/t3 shadow state.
		cs::engine::ResubmitPixelShaderResource(color);
		// The second composite's proxy copy predates this write.
		cs::render::TemporalRenderer::GetSingleton()->InvalidateProxy(RT::kMainCopy);
		++_dispatches;
		_produced = true;
		UpdateConsumer(true);
	}

	std::span<const FeatureDebugView> SubsurfaceScattering::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{ .id = "mask", .label = "Skin mask", .kind = FeatureDebugViewKind::kTexturePreview, .textureProvider = [](const Feature& a_feature) {
								 return static_cast<const SubsurfaceScattering&>(a_feature).GetDebugTexture(Preview::kMask);
							 } },
			FeatureDebugView{ .id = "diffuse", .label = "Diffuse without albedo", .kind = FeatureDebugViewKind::kTexturePreview, .textureProvider = [](const Feature& a_feature) {
								 return static_cast<const SubsurfaceScattering&>(a_feature).GetDebugTexture(Preview::kDiffuse);
							 } }
		};
		return views;
	}

	void SubsurfaceScattering::SetDebugView(std::string_view a_view) noexcept
	{
		_preview = a_view == "mask"    ? Preview::kMask :
		           a_view == "diffuse" ? Preview::kDiffuse :
		                                 Preview::kNone;
	}

	FeatureDebugTexture SubsurfaceScattering::GetDebugTexture(Preview a_view) const
	{
		FeatureDebugTexture result{ .unavailableText = "Buffer not allocated." };
		if (_preview == a_view && _resourcesReady) {
			result.texture = (a_view == Preview::kMask ? _textures.mask : _textures.diffuseNoAlbedo)->srv.get();
			result.width = _textures.width;
			result.height = _textures.height;
		}
		return result;
	}

	void SubsurfaceScattering::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink.Field("hooks_registered", _registrationsReady.load()).Field("resources_ready", _resourcesReady.load()).Field("resource_init_failed", _failed.load()).Field("typed_uav_unsupported", _unsupported.load()).Field("produced", _produced.load()).Field("dispatches", static_cast<std::int64_t>(_dispatches.load())).Field("skin_drawn", _skinFrame.load(std::memory_order_relaxed) == cs::engine::GetFrameCount()).Field("skin_buckets", static_cast<std::int64_t>(_skinBuckets.load(std::memory_order_relaxed))).Field("skipped_frames", static_cast<std::int64_t>(_skippedFrames.load(std::memory_order_relaxed))).Field("mode", _lastMode.load() == sss::kSeparable ? "separable" : "burley").Field("native_controls_applied", _nativeApplied.load()).Dimensions("working", _textures.width, _textures.height);
		cs::render::profiling::CollectPassTimings(a_sink, "SubsurfaceScattering::");
	}

	void SubsurfaceScattering::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(sss::kSchema, _settings);
		// The mean free path member splits into a color and a distance field.
		const auto meanFreePath = [&](settings::Color4 Settings::* a_member) {
			auto& value = _settings.*a_member;
			const Settings defaultSettings{};
			const auto& defaults = defaultSettings.*a_member;
			const std::string key(sss::kSchema.Find(a_member).key);
			settings::Float3 color{ value[0], value[1], value[2] };
			const settings::Float3 defaultColor{ defaults[0], defaults[1], defaults[2] };
			if (edit.Apply(ui::EditSetting({ key + "-color", { .label = "Mean Free Path Color", .help = "How far light travels under the surface in red, green and blue; scaled by the distance." } }, color, defaultColor)))
				std::ranges::copy(color, value.begin());
			edit.Apply(ui::EditSetting({ key + "-distance", { .label = "Mean Free Path Distance", .help = "Distance that the mean free path color travels under the surface.", .format = "%.2f" } },
				value[3], defaults[3], settings::Range{ 0.01f, 10.0f }));
		};
		if (_unsupported)
			dmui::ui::Text("%s", "Unavailable: this GPU lacks typed UAV loads for the main color format.");
		if (const ui::Section section{ "sss-method", "Method" }; section) {
			static const std::array modes{
				dmui::ChoiceOption<int>{ sss::kSeparable, "Separable SSS", "separable" },
				dmui::ChoiceOption<int>{ sss::kBurley, "Burley", "burley" }
			};
			field.Choice(&Settings::ssMode, modes, { .label = "Mode" });
			if (_settings.ssMode == sss::kSeparable) {
				static const std::array scatter{
					dmui::ChoiceOption<int>{ sss::kPreScatter, "Pre-scatter", "pre" },
					dmui::ChoiceOption<int>{ sss::kPostScatter, "Post-scatter", "post" },
					dmui::ChoiceOption<int>{ sss::kPreAndPostScatter, "Pre and Post", "pre-post" }
				};
				field.Choice(&Settings::scatterMode, scatter, { .label = "Albedo Handling" });
			} else {
				field(&Settings::burleySamples, { .label = "Burley Samples" });
			}
		}
		if (_settings.ssMode == sss::kSeparable) {
			if (const ui::Section section{ "sss-base-profile", "Base Profile" }; section) {
				field(&Settings::baseProfileBlurRadius, { .label = "Blur Radius", .format = "%.2f" });
				field(&Settings::baseProfileThickness, { .label = "Thickness", .format = "%.2f" });
				field(&Settings::baseProfileStrength, { .label = "Strength" });
				field(&Settings::baseProfileFalloff, { .label = "Falloff" });
			}
			if (const ui::Section section{ "sss-human-profile", "Human Profile" }; section) {
				field(&Settings::humanProfileBlurRadius, { .label = "Blur Radius", .format = "%.2f" });
				field(&Settings::humanProfileThickness, { .label = "Thickness", .format = "%.2f" });
				field(&Settings::humanProfileStrength, { .label = "Strength" });
				field(&Settings::humanProfileFalloff, { .label = "Falloff" });
			}
		} else {
			if (const ui::Section section{ "sss-base-profile", "Base Profile" }; section)
				meanFreePath(&Settings::meanFreePathBase);
			if (const ui::Section section{ "sss-human-profile", "Human Profile" }; section)
				meanFreePath(&Settings::meanFreePathHuman);
		}
		if (const ui::Section section{ "sss-character-light", "Character Light" }; section) {
			field(&Settings::enableCharacterLighting, { .label = "Enable Character Lighting" });
			if (_settings.enableCharacterLighting)
				field(&Settings::characterLightingStrength, { .label = "Strength", .format = "%.2f" });
		}
	}

	void SubsurfaceScattering::RestoreDefaultSettings()
	{
		_settings = Settings{};
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(SubsurfaceScattering::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
