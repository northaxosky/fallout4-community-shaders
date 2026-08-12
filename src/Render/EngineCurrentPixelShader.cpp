#include "Render/EngineCurrentPixelShader.h"

#ifndef FO4CS_CURRENT_PIXEL_SHADER_TESTING
#include "PCH.h"
#endif

namespace cs::engine
{
	namespace
	{
#ifndef FO4CS_CURRENT_PIXEL_SHADER_TESTING
		std::optional<EngineCurrentPixelShaderSnapshot>
			ReadPublishedPixelShader(void*) noexcept
		{
			// BeginTechnique publishes the wrapper before setup completes.
			static REL::Relocation<RE::BSGraphics::PixelShader**>
				currentPixelShader{
					REL::ID({ 0, 2713197, 2713197 })
				};
			auto* const shader = currentPixelShader
				? *currentPixelShader
				: nullptr;
			if (!shader)
				return std::nullopt;
			return EngineCurrentPixelShaderSnapshot{
				.psid = EngineLookupPsid{ shader->id },
				.d3dShaderPresent = shader->shader != nullptr
			};
		}
#endif
	}

	std::string_view EngineCurrentPixelShaderStatusName(
		EngineCurrentPixelShaderStatus a_status) noexcept
	{
		switch (a_status) {
		case EngineCurrentPixelShaderStatus::kKnown:
			return "known";
		case EngineCurrentPixelShaderStatus::kUnavailableOnRuntime:
			return "unavailable_on_runtime";
		case EngineCurrentPixelShaderStatus::kNoCurrentPixelShader:
			return "no_current_pixel_shader";
		case EngineCurrentPixelShaderStatus::kNoD3DShader:
			return "no_d3d_shader";
		}
		return "no_current_pixel_shader";
	}

	EngineCurrentPixelShaderObservation ObserveEngineCurrentPixelShader(
		bool a_availableOnRuntime,
		EngineCurrentPixelShaderSnapshotReader a_reader,
		void* a_context) noexcept
	{
		if (!a_availableOnRuntime) {
			return {
				.status =
					EngineCurrentPixelShaderStatus::kUnavailableOnRuntime
			};
		}
		if (!a_reader)
			return {};
		const auto snapshot = a_reader(a_context);
		if (!snapshot)
			return {};
		return {
			.status = snapshot->d3dShaderPresent
				? EngineCurrentPixelShaderStatus::kKnown
				: EngineCurrentPixelShaderStatus::kNoD3DShader,
			.psid = snapshot->psid
		};
	}

#ifndef FO4CS_CURRENT_PIXEL_SHADER_TESTING
	EngineCurrentPixelShaderObservation
		ReadEngineCurrentPixelShader() noexcept
	{
		if (REX::FModule::IsRuntimeOG()) {
			return ObserveEngineCurrentPixelShader(
				false, nullptr);
		}
		return ObserveEngineCurrentPixelShader(
			true, &ReadPublishedPixelShader);
	}
#endif
}
