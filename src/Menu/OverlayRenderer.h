#pragma once

namespace cs
{
	// Draws the always-on overlay layer: per-feature overlays and transient notices.
	class OverlayRenderer
	{
	public:
		static void RenderOverlay();

	private:
		static void RenderFeatureOverlays();
	};
}
