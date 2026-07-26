#pragma once

#include "Render/ShaderInjection.h"
#include "SssDxbcPatchResolver.h"

#include <filesystem>
#include <string>

namespace cs::features::sss_dxbc_patch
{
	bool Prepare(
		const std::filesystem::path& a_artifactPath,
		std::string& a_error);
	bool ActivateResolver();
	void SetBindingReady(bool a_ready) noexcept;
	void BreakBindingInvariant() noexcept;
	bool BindingInvariantBroken() noexcept;
	bool MatchesPatchedShader(
		engine::ShaderInjectionTarget a_target,
		ID3D11PixelShader* a_shader) noexcept;
	bool RestoreStockShader(
		ID3D11DeviceContext* a_context) noexcept;
	void RecordPatchedDrawMatched() noexcept;
	TelemetrySnapshot GetTelemetry() noexcept;
}
