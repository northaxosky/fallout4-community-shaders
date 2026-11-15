#include "TruePBR.h"

#include "LoadNotices.h"
#include "MaterialLoadHooks.h"
#include "MaterialRegistry.h"
#include "MaterialTagHooks.h"
#include "MaterialTags.h"
#include "Menu/Section.h"
#include "Menu/SettingWidgets.h"
#include "Menu/SettingsEdit.h"
#include "PrepassClassifier.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/GBufferViews.h"
#include "Render/RenderTargetFormats.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedDataLayout.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

#include <DearModdingUI/Client.h>

#include <array>
#include <format>

namespace cs::features
{
	TruePBR* TruePBR::GetSingleton()
	{
		static TruePBR instance;
		return &instance;
	}

	namespace
	{
		// SV_Target7 and t67 in the prepass and lighting shaders.
		constexpr std::uint32_t kF0TargetSlot = 7;
		constexpr std::uint32_t kF0PublishSlot = 67;

		constexpr std::array kDebugViews{
			FeatureDebugView{ "classified", "Classified draws", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "record", "Material record (color per id)", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "base_color", "Base color (after metal)", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "roughness", "Roughness", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "metallic_f0", "F0 (metals carry their color)", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "ambient_occlusion", "Material ambient occlusion", FeatureDebugViewKind::kFullscreen }
		};
	}

	bool TruePBR::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(true_pbr::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(true_pbr::kSchema, _settings);
		return true;
	}

	bool TruePBR::SaveSettings()
	{
		return settings::SaveDelta(true_pbr::kSchema, GetConfigKey(), _settings, *Log());
	}

	void TruePBR::RestoreDefaultSettings()
	{
		_settings = {};
		SaveSettings();
	}

	void TruePBR::OnD3D11Ready(IDXGIAdapter*, ID3D11Device*)
	{
		_records.Create();
	}

	bool TruePBR::EnabledThisFrame() noexcept
	{
		const auto* graphics = engine::GetGraphicsState();
		const auto frame = graphics ? graphics->frameCount : 0u;
		if (frame != _latchedFrame) {
			_latchedFrame = frame;
			// Last frame's views: a lit pixel is only tagged once both targets exist.
			_latchedEnabled = IsInjectionOperational() && _records.SRV() && _f0Target &&
			                  engine::GetPrepassExtraTargetSRV(*_f0Target) &&
			                  engine::GetPrepassAuxSRV(engine::PrepassAuxChannel::kMaterialAO);
		}
		return _latchedEnabled;
	}

	void TruePBR::WriteSharedData(render::SharedFeatureDataCB& a_shared, render::FO4SharedDataCB& a_fo4)
	{
		a_shared.truePBRSettings.VertexAOStrength = _settings.VertexAOStrength;
		a_shared.truePBRSettings.EnableMicroShadows = _settings.EnableMicroShadows ? 1u : 0u;
		a_shared.truePBRSettings.MicroShadowStrength = _settings.MicroShadowStrength;
		a_fo4.EnabledTruePBR = EnabledThisFrame() ? 1u : 0u;
	}

	void TruePBR::BindRecordTable(ID3D11DeviceContext* a_context)
	{
		if (auto* table = _records.SRV())
			engine::BindInjectionShaderResources(a_context, true_pbr::kRecordTableSlot, 1, &table);
	}

	bool TruePBR::RegisterMaterialTargets()
	{
		const auto active = [this] { return IsEnabled() && IsInjectionOperational(); };
		constexpr std::uint8_t color = 0x7;  // RGB; the target has no alpha
		_f0Target = engine::RegisterPrepassExtraTarget(*this, { .name = "TruePBR/F0",
																  .rtvSlot = kF0TargetSlot,
																  .format = DXGI_FORMAT_R11G11B10_FLOAT,
																  .clear = { 0.04f, 0.04f, 0.04f, 0.0f },
																  .publishSlot = kF0PublishSlot,
																  .publishStages = engine::ShaderStageBit(engine::ShaderStage::kPixel) | engine::ShaderStageBit(engine::ShaderStage::kCompute),
																  .active = active,
																  // Terrain fades neutral F0 into a PBR object like color.
																  .terrain = { engine::PrepassExtraWrite{ .blend = true, .mask = color }, engine::PrepassExtraWrite{}, engine::PrepassExtraWrite{ .mask = color } },
																  .materialMask = color });
		return _f0Target && engine::RegisterPrepassAuxChannel(*this, engine::PrepassAuxChannel::kMaterialAO, active);
	}

	void TruePBR::Load()
	{
		const bool registered = engine::RegisterFeatureShaderBindings(*this, [this](engine::ShaderReplacementRegistration& a_registration) {
			// The prepass reads the record table; the lighting stages read the G-buffer.
			if (a_registration.targetId == engine::ShaderInjectionTarget::kDeferredPrepass)
				a_registration.bind = [this](ID3D11DeviceContext* a_context) { BindRecordTable(a_context); };
			else
				a_registration.bind = [](ID3D11DeviceContext* a_context) { engine::BindGBufferViews(a_context); };
		});
		if (!registered || !true_pbr::RegisterPrepassClassifier(*this) || !RegisterMaterialTargets()) {
			FailLoad("True PBR prepass registration failed.");
			return;
		}
		// Tag hooks first and the parse last, so a partial install tags nothing.
		if (!true_pbr::InstallMaterialTagHooks() || !true_pbr::InstallMaterialLoadHooks()) {
			FailLoad("True PBR material hooks failed to install.");
			return;
		}
		Log()->info("Installed hooks");
		// Last: the request cannot be undone and needs the shader clamp.
		const bool floatEmissive = engine::RequestRenderTargetFormat(RE::BSGraphics::RenderTargetID::kGBufferEmissive, DXGI_FORMAT_R16G16B16A16_FLOAT);
		_floatEmissiveRequested.store(floatEmissive, std::memory_order_relaxed);
		if (!floatEmissive)
			Log()->warn("The emissive target stays 8-bit; PBR emission clips at 1.");
		_registrationsReady.store(true, std::memory_order_release);
	}

	bool TruePBR::ValidateInjectionPrerequisites(std::string& a_error)
	{
		return RequireSharedData(a_error, "the debug views read b7");
	}

	void TruePBR::Prepass()
	{
		_records.Upload(engine::GetImmediateContext());
		if (_emissiveFormat.load(std::memory_order_relaxed) != 0)
			return;
		if (auto* texture = engine::GetRenderTargetTexture(RE::BSGraphics::RenderTargetID::kGBufferEmissive)) {
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			_emissiveFormat.store(desc.Format, std::memory_order_relaxed);
		}
	}

	std::span<const FeatureDebugView> TruePBR::GetDebugViews() const noexcept { return kDebugViews; }

	void TruePBR::SetDebugView(std::string_view a_view) noexcept
	{
		_debugMode = a_view == "classified"        ? TruePBRDebugMode::Classified :
		             a_view == "record"            ? TruePBRDebugMode::Record :
		             a_view == "base_color"        ? TruePBRDebugMode::BaseColor :
		             a_view == "roughness"         ? TruePBRDebugMode::Roughness :
		             a_view == "metallic_f0"       ? TruePBRDebugMode::MetallicF0 :
		             a_view == "ambient_occlusion" ? TruePBRDebugMode::AmbientOcclusion :
		                                             0u;
	}

	FullscreenDebugData TruePBR::GetFullscreenDebugData() const noexcept
	{
		return { .owner = FullscreenDebugOwner::TruePBR, .mode = _debugMode.load() };
	}

	void TruePBR::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		using namespace true_pbr;
		a_sink
			.Field("records", Records().Count())
			.Field("rejected", Notices().RejectedCount())
			.Field("live_tags", Tags().Count())
			.Field("retag_conflicts", Tags().Conflicts())
			.Field("foreign_material_path", Tags().ForeignPaths())
			.Field("float_emissive_requested", _floatEmissiveRequested.load(std::memory_order_relaxed))
			.Field("float_emissive", _emissiveFormat.load(std::memory_order_relaxed) == DXGI_FORMAT_R16G16B16A16_FLOAT)
			.Field("emissive_format", _emissiveFormat.load(std::memory_order_relaxed));
	}

	void TruePBR::DrawDebug()
	{
		using namespace true_pbr;
		dmui::ui::Text("Records: %zu  Live tagged materials: %zu", Records().Count(), Tags().Count());
		dmui::ui::Text("Rejected files: %zu", Notices().RejectedCount());
		if (const auto last = Notices().LastRejection(); !last.empty())
			dmui::ui::Text("Last rejection: %s", last.c_str());
		for (const auto& [id, live] : Tags().LiveByRecord()) {
			if (const auto* record = Records().Get(id))
				dmui::ui::Text("id %u  live %zu  %s", static_cast<unsigned>(id), live, record->name.c_str());
		}
	}

	void TruePBR::DrawSettings()
	{
		using namespace true_pbr;
		const auto records = Records().Count();
		ui::DrawStatusText(Log(), std::format("{} PBR material files loaded this session.", records), dmui::TextTone::kInherit);
		if (!IsEnabled() && records != 0)
			ui::DrawStatusText(Log(), "True PBR is switched off, but loaded PBR materials keep their PBR textures and render through vanilla shading until restart.", dmui::TextTone::kStatusWarning);
		settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(kSchema, _settings);
		if (const ui::Section section{ "true-pbr-global", "Global Settings" }; section) {
			field(&Settings::VertexAOStrength);
			field(&Settings::EnableMicroShadows);
			if (_settings.EnableMicroShadows)
				field(&Settings::MicroShadowStrength);
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(TruePBR::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
