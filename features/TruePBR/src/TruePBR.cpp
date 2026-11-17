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
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/GBufferViews.h"
#include "Render/PrepassLane.h"
#include "Render/RenderHooks.h"
#include "Render/RenderTargetFormats.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedDataLayout.h"
#include "Render/SubstrateSlots.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"

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
		// SV_Target7 of the prepass shader.
		constexpr std::uint32_t kF0TargetSlot = 7;
		// RGB only: the F0 target has no alpha; the emissive alpha is kept.
		constexpr std::uint8_t kRgbMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;

		constexpr std::array kDebugViews{
			FeatureDebugView{ "classified", "Classified draws", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "record", "Material record (color per id)", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "base_color", "Base color (after metal)", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "roughness", "Roughness", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "metallic_f0", "F0 (metals carry their color)", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "ambient_occlusion", "Material ambient occlusion", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "parallax_offset", "Parallax offset (uv x10 in RG, shadow in B)", FeatureDebugViewKind::kFullscreen }
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

	void TruePBR::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		_records.Create();
		BuildEmissiveClamp(a_device);
	}

	void TruePBR::BuildEmissiveClamp(ID3D11Device* a_device)
	{
		try {
			_clampPass.Create(a_device);
			D3D11_BLEND_DESC blend{};
			auto& target = blend.RenderTarget[0];
			target.BlendEnable = TRUE;
			target.SrcBlend = target.SrcBlendAlpha = D3D11_BLEND_ONE;
			target.DestBlend = target.DestBlendAlpha = D3D11_BLEND_ONE;
			target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_MIN;
			// The alpha carries the vertex occlusion.
			target.RenderTargetWriteMask = kRgbMask;
			DX::ThrowIfFailed(a_device->CreateBlendState(&blend, _clampBlend.put()));
			render::annotation::SetName(_clampBlend.get(), "TruePBR/EmissiveClamp.Blend");
			_clampShader = util::CompilePixelShader(L"Data\\Shaders\\FO4\\TruePBR\\EmissiveClampPS.hlsl");
		} catch (const std::exception& e) {
			Log()->warn("The emissive clamp pass is unavailable: {}", e.what());
		}
		if (!_clampShader)
			Log()->warn("The emissive clamp pass is unavailable; fallback prepass draws can exceed 1.");
	}

	void TruePBR::ClampFallbackEmission()
	{
		const auto outcome = engine::GetShaderInjectionOutcomeSnapshot(engine::ShaderInjectionTarget::kDeferredPrepass);
		if (outcome.nativeDraws == 0 || !_floatEmissiveRequested.load(std::memory_order_relaxed) || !_clampShader || !_clampPass.Ready())
			return;
		auto* context = engine::GetImmediateContext();
		auto* target = engine::GetRenderTargetRTV(RE::BSGraphics::RenderTargetID::kGBufferEmissive);
		auto* texture = engine::GetRenderTargetTexture(RE::BSGraphics::RenderTargetID::kGBufferEmissive);
		auto* material = engine::GetRenderTargetSRV(RE::BSGraphics::RenderTargetID::kGBufferAmbient);
		if (!context || !target || !texture || !material)
			return;
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		const render::FullscreenPass::Scope pass(_clampPass, context, target, desc.Width, desc.Height);
		if (!pass.IsActive())
			return;
		render::annotation::ScopedEvent timing("TruePBR/EmissiveClamp");
		constexpr float kBlendFactor[4]{};
		context->OMSetBlendState(_clampBlend.get(), kBlendFactor, 0xFFFFFFFFu);
		context->PSSetShaderResources(0, 1, &material);
		pass.Draw(_clampShader.get());
		_clampFrames.fetch_add(1, std::memory_order_relaxed);
	}

	bool TruePBR::EnabledThisFrame() noexcept
	{
		const auto* graphics = engine::GetGraphicsState();
		const auto frame = graphics ? graphics->frameCount : 0u;
		if (frame != _latchedFrame) {
			_latchedFrame = frame;
			// Last frame's views: tagging needs both targets to exist.
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
			engine::BindInjectionShaderResources(a_context, render::kTruePBRRecordTableSlot, 1, &table);
	}

	bool TruePBR::RegisterMaterialTargets()
	{
		const auto active = [this] { return IsEnabled() && IsInjectionOperational(); };
		_f0Target = engine::RegisterPrepassExtraTarget(*this, { .name = "TruePBR/F0",
																  .rtvSlot = kF0TargetSlot,
																  .format = DXGI_FORMAT_R11G11B10_FLOAT,
																  .clear = { 0.04f, 0.04f, 0.04f, 0.0f },
																  .publishSlot = render::kTruePBRF0Slot,
																  .publishStages = engine::ShaderStageBit(engine::ShaderStage::kPixel) | engine::ShaderStageBit(engine::ShaderStage::kCompute),
																  .active = active,
																  // Terrain fades neutral F0 into a PBR object like color.
																  .terrain = { engine::PrepassExtraWrite{ .blend = true, .mask = kRgbMask }, engine::PrepassExtraWrite{}, engine::PrepassExtraWrite{ .mask = kRgbMask } },
																  .materialMask = kRgbMask });
		return _f0Target && engine::RegisterPrepassAuxChannel(*this, engine::PrepassAuxChannel::kMaterialAO, active);
	}

	void TruePBR::Load()
	{
		const bool registered = engine::RegisterFeatureShaderBindings(*this, [this](engine::ShaderReplacementRegistration& a_registration) {
			// The prepass binds the record table, lighting the G-buffer.
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
		if (!engine::RegisterPostDeferredPrePass(*this, [this] { ClampFallbackEmission(); })) {
			FailLoad("True PBR emissive clamp registration failed.");
			return;
		}
		// Last: the request cannot be undone.
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
		             a_view == "parallax_offset"   ? TruePBRDebugMode::ParallaxOffset :
		                                             0u;
	}

	FullscreenDebugData TruePBR::GetFullscreenDebugData() const noexcept
	{
		return { .owner = FullscreenDebugOwner::TruePBR, .mode = _debugMode.load() };
	}

	void TruePBR::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		using namespace true_pbr;
		const auto lanes = engine::GetPrepassLaneStats();
		a_sink
			.Field("records", Records().Count())
			.Field("rejected", Notices().RejectedCount())
			.Field("texture_warnings", Notices().WarningCount())
			.Field("textured_draws", lanes.textured)
			.Field("bakes_kept_immediate", lanes.keptImmediate)
			.Field("live_tags", Tags().Count())
			.Field("retag_conflicts", Tags().Conflicts())
			.Field("foreign_material_path", Tags().ForeignPaths())
			.Field("float_emissive_requested", _floatEmissiveRequested.load(std::memory_order_relaxed))
			.Field("float_emissive", _emissiveFormat.load(std::memory_order_relaxed) == DXGI_FORMAT_R16G16B16A16_FLOAT)
			.Field("emissive_clamp_frames", _clampFrames.load(std::memory_order_relaxed))
			.Field("emissive_format", _emissiveFormat.load(std::memory_order_relaxed));
	}

	void TruePBR::DrawDebug()
	{
		using namespace true_pbr;
		dmui::ui::Text("Records: %zu  Live tagged materials: %zu", Records().Count(), Tags().Count());
		dmui::ui::Text("Rejected files: %zu  Texture warnings: %zu", Notices().RejectedCount(), Notices().WarningCount());
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
		const auto* extendedMaterials = FeatureManager::Get().Find("ExtendedMaterials");
		if (!extendedMaterials || !extendedMaterials->IsLoaded() || !extendedMaterials->IsEnabled())
			ui::DrawStatusText(Log(), "PBR parallax needs Extended Materials, which is not active; PBR surfaces render flat.", dmui::TextTone::kStatusWarning);
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
