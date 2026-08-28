#pragma once

namespace cs::BackgroundBlur
{
	bool Initialize();

	// Call after ImGui::Render() and before ImGui_ImplDX11_RenderDrawData().
	void RenderBackgroundBlur();

	void Cleanup();
	void SetEnabled(bool a_enable);
	bool IsEnabled();
}
