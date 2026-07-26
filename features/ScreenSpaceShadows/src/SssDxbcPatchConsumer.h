#pragma once

#include "Render/ShaderInjection.h"
#include "SssDxbcPatchResolver.h"

#include <filesystem>
#include <string>

namespace cs::features::sss_dxbc_patch
{
	struct PatchedDrawTelemetry
	{
		bool realMaskBound = false;
		bool whiteFallbackBound = false;
		bool invariantViolated = false;
		bool stockShaderFallback = false;
		bool stockShaderFallbackFailed = false;
	};

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
	void RecordPatchedDraw(PatchedDrawTelemetry a_telemetry) noexcept;
	TelemetrySnapshot GetTelemetry() noexcept;
}
