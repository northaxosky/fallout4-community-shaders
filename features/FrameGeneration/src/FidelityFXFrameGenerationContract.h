#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>
#include <FidelityFX/api/include/ffx_api.hpp>
#include <FidelityFX/framegeneration/include/ffx_framegeneration.hpp>

namespace cs::features::fidelityfx_fg
{
	[[nodiscard]] inline ffx::ConfigureDescFrameGeneration
		BuildDisabledConfiguration(
			IDXGISwapChain4* a_swapChain,
			std::uint64_t a_frameId,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight) noexcept
	{
		ffx::ConfigureDescFrameGeneration config{};
		config.frameGenerationEnabled = false;
		config.frameGenerationCallback = nullptr;
		config.frameGenerationCallbackUserContext = nullptr;
		config.HUDLessColor = FfxApiResource({});
		config.presentCallback = nullptr;
		config.presentCallbackUserContext = nullptr;
		config.frameID = a_frameId;
		config.swapChain = a_swapChain;
		config.onlyPresentGenerated = false;
		config.flags = 0;
		config.allowAsyncWorkloads = false;
		config.generationRect = {
			.left = 0,
			.top = 0,
			.width = static_cast<std::int32_t>(a_outputWidth),
			.height = static_cast<std::int32_t>(a_outputHeight)
		};
		return config;
	}

	struct ProviderVersion
	{
		ffx::ReturnCode result = ffx::ReturnCode::Error;
		bool available = false;
		std::uint64_t id = 0;
		std::string name;
	};

	template <class Query>
	[[nodiscard]] ProviderVersion QueryProviderVersion(
		ffx::Context& a_context,
		Query&& a_query)
	{
		ffx::QueryGetProviderVersion query{};
		const auto result =
			std::forward<Query>(a_query)(a_context, query);
		const bool available =
			result == ffx::ReturnCode::Ok && query.versionId != 0 &&
			query.versionName != nullptr;
		return {
			.result = result,
			.available = available,
			.id = available ? query.versionId : 0,
			.name = available ? query.versionName : ""
		};
	}
}
