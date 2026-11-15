#include "TruePBR.h"

#include "LoadNotices.h"
#include "MaterialLoadHooks.h"
#include "MaterialRegistry.h"
#include "MaterialTagHooks.h"
#include "MaterialTags.h"
#include "Menu/SettingWidgets.h"
#include "PrepassClassifier.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/RenderTargetFormats.h"
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
		constexpr std::array kDebugViews{
			FeatureDebugView{ "classified", "Classified draws", FeatureDebugViewKind::kFullscreen },
			FeatureDebugView{ "record", "Material record (color per id)", FeatureDebugViewKind::kFullscreen }
		};
	}

	void TruePBR::Load()
	{
		if (!engine::RegisterFeatureShaderBindings(*this) || !true_pbr::RegisterPrepassClassifier(*this)) {
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
		_debugMode = a_view == "classified" ? TruePBRDebugMode::Classified :
		             a_view == "record"     ? TruePBRDebugMode::Record :
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
