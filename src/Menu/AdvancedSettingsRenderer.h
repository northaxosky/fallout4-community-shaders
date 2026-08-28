#pragma once

#include <functional>

namespace cs
{
	// Advanced Settings page: developer tools, disable-at-boot, logging and diagnostics.
	class AdvancedSettingsRenderer
	{
	public:
		static void RenderAdvancedSettings(const std::function<void()>& a_drawDisableAtBootSettings);

	private:
		static void RenderDeveloperSection();
		static void RenderDisableAtBootSection(const std::function<void()>& a_drawDisableAtBootSettings);
		static void RenderLoggingSection();
		static void RenderDiagnosticsSection();
	};
}
