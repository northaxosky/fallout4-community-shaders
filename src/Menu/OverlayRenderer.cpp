#include "Menu/OverlayRenderer.h"

#include "Feature.h"
#include "Menu/ImGuiRecovery.h"
#include "Menu/Menu.h"

#include <exception>

#include <imgui.h>

namespace cs
{
	void OverlayRenderer::RenderOverlay()
	{
		RenderFeatureOverlays();
	}

	void OverlayRenderer::RenderFeatureOverlays()
	{
		auto& featureManager = FeatureManager::Get();
		for (Feature* feature : featureManager.GetAll()) {
			if (!feature || !featureManager.PrepareRuntimeCallback(*feature, "Menu::DrawOverlay"))
				continue;

			CS_FEATURE_ZONE(feature, "DrawOverlay");
			auto recovery = ImGuiRecoverySnapshot::Capture();
			if (!recovery) {
				featureManager.QuarantineRuntimeCallback(*feature, "Menu::DrawOverlay", "ImGui recovery snapshot unavailable");
				continue;
			}
			try {
				feature->DrawOverlay();
			} catch (const std::exception& e) {
				recovery->Recover();
				featureManager.QuarantineRuntimeCallback(*feature, "Menu::DrawOverlay", e.what());
			} catch (...) {
				recovery->Recover();
				featureManager.QuarantineRuntimeCallback(*feature, "Menu::DrawOverlay", "non-standard exception");
			}
		}
		featureManager.FinishRuntimeCallbackPass();
	}
}
