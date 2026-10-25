#include "ImageBasedLighting.h"

#include <DearModdingUI/Client.h>
#include <d3d11.h>

#include <span>
#include <stdexcept>
#include <utility>

#include <toml++/toml.hpp>

#include "DynamicCubemaps.h"
#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/FrameProfiler.h"
#include "Render/RendererContext.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Render/SkyReflectionCube.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "World/Sky.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.imagebasedlighting");

		namespace ibl = image_based_lighting;

		constexpr std::string_view kEnvironmentPass = "IBL::EnvDiffuseIBL";
		constexpr std::string_view kSkyPass = "IBL::SkyDiffuseIBL";

		void Tooltip(const char* a_text)
		{
			if (dmui::ui::IsItemHovered())
				dmui::ui::SetTooltip("%s", a_text);
		}
	}

	ImageBasedLighting* ImageBasedLighting::GetSingleton()
	{
		static ImageBasedLighting instance;
		return &instance;
	}

	bool ImageBasedLighting::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(ibl::kSchema, a_config, candidate, a_error) ||
			!_weather.Configure(a_config.get("weather"), a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(ibl::kSchema, _settings, [this] { PublishSettings(); });
		return true;
	}

	void ImageBasedLighting::PublishSettings()
	{
		const std::lock_guard lock(_settingsMutex);
		_published = _settings;
	}

	bool ImageBasedLighting::SaveSettings()
	{
		return settings::SaveDelta(ibl::kSchema, GetConfigKey(), _settings, *L);
	}

	void ImageBasedLighting::Load()
	{
		PublishSettings();
		// FO4: DistantTree is not a DC target; require DC at load.
		if (!DynamicCubemaps::GetSingleton()->IsLoaded()) {
			FailLoad("Image Based Lighting requires Dynamic Cubemaps to be loaded.");
			return;
		}
		if (!engine::RegisterFeatureShaderBindings("ImageBasedLighting", *this, [this](engine::ShaderReplacementRegistration& registration) {
				registration.bind = [this](ID3D11DeviceContext* a_context) { BindConsumer(a_context); };
			})) {
			FailLoad("Image Based Lighting shader contribution registration failed.");
			return;
		}
		// FO4: vanilla never renders the reflections cube; the host does.
		if (!engine::RegisterSkyReflectionCubeConsumer("ImageBasedLighting", [this] {
				return IsEnabled() && _wantsSkyCube.load(std::memory_order_acquire);
			})) {
			FailLoad("Image Based Lighting needs the host sky reflection cube.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	void ImageBasedLighting::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_registrationsReady.load(std::memory_order_acquire) || !a_device)
			return;
		const D3D11_TEXTURE2D_DESC texDesc{
			.Width = 3,
			.Height = 1,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		};
		const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		const D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
		winrt::com_ptr<ID3D11DeviceContext> context;
		a_device->GetImmediateContext(context.put());
		const auto create = [&](std::unique_ptr<buffer::Texture2D>& a_texture, std::string_view a_name) {
			a_texture = std::make_unique<buffer::Texture2D>(texDesc);
			a_texture->CreateSRV(srvDesc);
			a_texture->CreateUAV(uavDesc);
			a_texture->SetName(std::string(a_name), std::string(a_name) + ".SRV", std::string(a_name) + ".UAV");
			// A zero SH is neutral until the first projection.
			const float zero[4]{};
			context->ClearUnorderedAccessViewFloat(a_texture->uav.get(), zero);
		};
		create(_envSH, "IBL/EnvSH");
		create(_skySH, "IBL/SkySH");

		// FO4: the substrate defines the CS shared-data inputs.
		_projectCompute.attach(static_cast<ID3D11ComputeShader*>(util::CompileShader(
			L"Data\\Shaders\\IBL\\DiffuseIBLCS.hlsl", { { "FO4CS_SUBSTRATE", "1" } }, "cs_5_0")));
		if (!_projectCompute)
			throw std::runtime_error("the diffuse IBL compute shader failed to compile");
		render::annotation::SetName(_projectCompute.get(), "IBL/DiffuseIBL.CS");
		_resourcesReady.store(true, std::memory_order_release);
		L->info("SH textures ready: 2x 3x1 R16G16B16A16_FLOAT at t{}-t{}.", kSHSlot, kSHSlot + kSHSlotCount - 1);
	}

	bool ImageBasedLighting::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire) || !_resourcesReady.load(std::memory_order_acquire)) {
			a_error = "the shader contribution or SH resources are unavailable";
			return false;
		}
		if (!render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so b6 carries no IBL settings";
			return false;
		}
		if (!engine::ValidateShaderInjectionRoutes("ImageBasedLighting", a_error))
			return false;
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	bool ImageBasedLighting::IsSceneDisabled(const Settings& a_settings)
	{
		return a_settings.DisableInLoadingScreen && engine::IsMainOrLoadingMenuOpen();
	}

	const ImageBasedLighting::FrameState& ImageBasedLighting::EvaluateFrame()
	{
		const auto* graphics = engine::GetGraphicsState();
		const auto frame = graphics ? graphics->frameCount : UINT32_MAX;
		if (frame != UINT32_MAX && frame == _frame.frame)
			return _frame;
		Settings published;
		{
			const std::lock_guard lock(_settingsMutex);
			published = _published;
		}
		_frame.settings = _weather.EvaluateCurrentWeather(published);
		_frame.sceneDisabled = IsSceneDisabled(_frame.settings);
		_frame.active = IsEnabled() && _frame.settings.EnableIBL && !_frame.sceneDisabled;
		_frame.frame = frame;
		_wantsSkyCube.store(_frame.active, std::memory_order_release);
		_counters.weatherEnabled.store(_frame.settings.EnableIBL, std::memory_order_relaxed);
		_counters.sceneDisabled.store(_frame.sceneDisabled, std::memory_order_relaxed);
		_counters.dalcMode.store(_frame.settings.DALCMode, std::memory_order_relaxed);
		return _frame;
	}

	render::IBLSettings ImageBasedLighting::GetCommonBufferData()
	{
		const auto& frame = EvaluateFrame();
		const auto& settings = frame.settings;
		return {
			.EnableIBL = frame.active ? 1u : 0u,
			.PreserveFogLuminance = settings.PreserveFogLuminance ? 1u : 0u,
			.UseStaticIBL = 0u,
			.DALCAmount = settings.DALCAmount,
			.EnvIBLScale = settings.EnvIBLScale,
			.SkyIBLScale = settings.SkyIBLScale,
			.EnvIBLSaturation = settings.EnvIBLSaturation,
			.SkyIBLSaturation = settings.SkyIBLSaturation,
			.FogAmount = settings.FogAmount,
			.DALCMode = settings.DALCMode
		};
	}

	void ImageBasedLighting::BindConsumer(ID3D11DeviceContext* a_context)
	{
		if (!a_context || !_resourcesReady.load(std::memory_order_acquire))
			return;
		// Disabled scenes keep the bindings: the zero b6 flag skips them.
		const std::array<ID3D11ShaderResourceView*, kSHSlotCount> views{ _envSH->srv.get(), _skySH->srv.get() };
		engine::BindInjectionShaderResources(a_context, kSHSlot, kSHSlotCount, views.data());
		_counters.consumerBinds.fetch_add(1, std::memory_order_relaxed);
	}

	void ImageBasedLighting::Project(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_source,
		buffer::Texture2D& a_target, std::string_view a_pass)
	{
		if (!a_source) {
			const float zero[4]{};
			a_context->ClearUnorderedAccessViewFloat(a_target.uav.get(), zero);
			_counters.cleared.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		auto* uav = a_target.uav.get();
		a_context->CSSetShaderResources(0, 1, &a_source);
		a_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		render::annotation::ScopedEvent event(a_pass);
		a_context->Dispatch(1, 1, 1);
	}

	void ImageBasedLighting::OnEnabledChanged(bool a_enabled) noexcept
	{
		if (!a_enabled)
			_clearPending.store(true, std::memory_order_release);
	}

	void ImageBasedLighting::Prepass()
	{
		if (!IsEnabled()) {
			if (_clearPending.exchange(false, std::memory_order_acq_rel) && _resourcesReady.load(std::memory_order_acquire)) {
				if (auto* context = engine::GetImmediateContext()) {
					Project(context, nullptr, *_envSH, kEnvironmentPass);
					Project(context, nullptr, *_skySH, kSkyPass);
				}
			}
			return;
		}
		const auto& frame = EvaluateFrame();
		if (!_resourcesReady.load(std::memory_order_acquire) || !_injectionsOperational.load(std::memory_order_acquire))
			return;
		if (frame.sceneDisabled) {
			_counters.skippedScene.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		auto* context = engine::GetImmediateContext();
		if (!context)
			return;

		auto* environment = DynamicCubemaps::GetSingleton()->GetEnvironmentSRV();
		auto* sky = engine::GetSkyReflectionCubeSRV();
		_counters.environmentPresent.store(environment != nullptr, std::memory_order_relaxed);
		_counters.skyPresent.store(sky != nullptr, std::memory_order_relaxed);

		engine::ComputeOMScope scope(context, 1, 1, 1, 0);
		render::ScopedComputeSharedDataBinding substrate(context);
		auto* sampler = render::GetLinearClampSampler();
		context->CSSetSamplers(0, 1, &sampler);
		context->CSSetShader(_projectCompute.get(), nullptr, 0);
		// Modes 2 and 3 take the vanilla ambient instead of the environment SH.
		if (frame.settings.DALCMode < 2) {
			Project(context, environment, *_envSH, kEnvironmentPass);
			_counters.envProjected.fetch_add(environment ? 1 : 0, std::memory_order_relaxed);
		}
		Project(context, sky, *_skySH, kSkyPass);
		_counters.skyProjected.fetch_add(sky ? 1 : 0, std::memory_order_relaxed);
		_counters.projections.fetch_add(1, std::memory_order_relaxed);
	}

	void ImageBasedLighting::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		const auto count = [](const std::atomic<std::uint64_t>& a_value) { return a_value.load(std::memory_order_relaxed); };
		a_sink.Field("weather_enabled", _counters.weatherEnabled.load(std::memory_order_relaxed))
			.Field("scene_disabled", _counters.sceneDisabled.load(std::memory_order_relaxed))
			.Field("dalc_mode", static_cast<std::int64_t>(_counters.dalcMode.load(std::memory_order_relaxed)))
			.Field("resources_ready", _resourcesReady.load(std::memory_order_relaxed))
			.Field("injection_operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("environment_present", _counters.environmentPresent.load(std::memory_order_relaxed))
			.Field("sky_present", _counters.skyPresent.load(std::memory_order_relaxed))
			.Field("projections", count(_counters.projections))
			.Field("environment_projections", count(_counters.envProjected))
			.Field("sky_projections", count(_counters.skyProjected))
			.Field("cleared_projections", count(_counters.cleared))
			.Field("skipped_scene", count(_counters.skippedScene))
			.Field("consumer_binds", count(_counters.consumerBinds));
		render::profiling::CollectPassTimings(a_sink, "IBL::");
	}

	void ImageBasedLighting::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		bool changed = false;
		const auto toggle = [&](const char* a_label, bool& a_value, const char* a_tooltip) {
			changed |= edit.Discrete(dmui::ui::Checkbox(a_label, &a_value));
			Tooltip(a_tooltip);
		};
		const auto slider = [&](const char* a_label, float ibl::Settings::* a_member, const char* a_tooltip) {
			const auto range = ibl::kSchema.EditRange(a_member);
			changed |= edit.Continuous(dmui::ui::SliderScalar(a_label, &(_settings.*a_member), &range.min, &range.max, "%.2f"));
			Tooltip(a_tooltip);
		};

		if (const ui::Section section{ "ibl-general", "General" }; section) {
			toggle("Disable in loading screens", _settings.DisableInLoadingScreen,
				"Disables IBL during loading screens and the main menu.");
		}
		if (const ui::Section section{ "ibl-sources", "Sources" }; section) {
			slider("Env IBL Scale", &ibl::Settings::EnvIBLScale,
				"Intensity multiplier for the environment IBL (from Dynamic Cubemaps).\nControls how strongly the surrounding environment contributes to ambient lighting.");
			slider("Sky IBL Scale", &ibl::Settings::SkyIBLScale,
				"Intensity multiplier for the sky IBL (from the host sky reflection cubemap).\nControls how strongly the sky contributes to ambient lighting.");
			slider("Env IBL Saturation", &ibl::Settings::EnvIBLSaturation,
				"Color saturation of the environment IBL.\nLower values produce more neutral ambient light; higher values produce more vivid color.");
			slider("Sky IBL Saturation", &ibl::Settings::SkyIBLSaturation,
				"Color saturation of the sky IBL.\nLower values produce more neutral ambient light; higher values produce more vivid color.");
		}
		if (const ui::Section section{ "ibl-ambient", "Ambient Matching" }; section) {
			slider("DALC Amount", &ibl::Settings::DALCAmount,
				"Blends the IBL brightness toward the game's vanilla ambient (DALC) level.\n0 = no matching (pure IBL brightness), 1 = fully matched to vanilla ambient.");
			const std::array modes{
				dmui::ChoiceOption<std::uint32_t>{ 0, "Luminance Ratio", "luminance-ratio" },
				dmui::ChoiceOption<std::uint32_t>{ 1, "Color Ratio", "color-ratio" },
				dmui::ChoiceOption<std::uint32_t>{ 2, "DALC + Sky", "dalc-sky" },
				dmui::ChoiceOption<std::uint32_t>{ 3, "DALC + Sky (Directional)", "dalc-sky-directional" }
			};
			const auto mode = dmui::DrawChoice<std::uint32_t>(
				"ibl-dalc-mode", _settings.DALCMode, std::span<const dmui::ChoiceOption<std::uint32_t>>{ modes }, "Unavailable", "DALC Mode");
			if (edit.Discrete(mode.changed)) {
				_settings.DALCMode = *mode.selected;
				changed = true;
			}
			Tooltip(
				"How the DALC-to-IBL brightness ratio is computed:\n"
				"Luminance Ratio: Scalar ratio from overall luminance (loses DALC color tint).\n"
				"Color Ratio: Per-channel ratio (preserves DALC color tint).\n"
				"DALC + Sky: Uses vanilla ambient as base, sky IBL on top. Skylighting only affects sky.\n"
				"DALC + Sky (Directional): Same, but Skylighting also dims vanilla ambient per-direction.");
		}
		if (const ui::Section section{ "ibl-fog", "Fog" }; section) {
			slider("Fog Mix", &ibl::Settings::FogAmount,
				"Blends the fog color toward the IBL ambient color.\n0 = vanilla fog, 1 = fog fully tinted by IBL.");
			toggle("Preserve Fog Luminance", _settings.PreserveFogLuminance,
				"When Fog Mix is active, rescales the IBL-tinted fog to keep the original fog brightness.\nPrevents fog from becoming too bright or too dark.");
		}
		if (changed)
			PublishSettings();
	}

	void ImageBasedLighting::RestoreDefaultSettings()
	{
		_settings = {};
		PublishSettings();
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(ImageBasedLighting::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
