#include "TruePBR.h"

#include "LoadNotices.h"
#include "MaterialLoadHooks.h"
#include "MaterialRegistry.h"
#include "MaterialTagHooks.h"
#include "MaterialTags.h"
#include "Menu/SettingWidgets.h"
#include "PrepassClassifier.h"
#include "Render/FeatureShaderBindings.h"
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
		// Tag hooks first and the parse last, so a partial install tags nothing.
		if (!true_pbr::InstallMaterialTagHooks() || !true_pbr::InstallMaterialLoadHooks()) {
			FailLoad("True PBR material hooks failed to install.");
			return;
		}
		Log()->info("Installed hooks");
		if (!engine::RegisterFeatureShaderBindings(*this) || !true_pbr::RegisterPrepassClassifier(*this)) {
			FailLoad("True PBR prepass registration failed.");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	bool TruePBR::ValidateInjectionPrerequisites(std::string& a_error)
	{
		return RequireSharedData(a_error, "the debug views read b7");
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
			.Field("foreign_material_path", Tags().ForeignPaths());
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
