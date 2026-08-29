#pragma once

#include "Host/IntegrationState.h"

#include <DearModdingUI/API.h>

namespace cs::host
{
	enum class SwapChainHandoffAction : std::uint8_t
	{
		kAccepted,
		kRetry,
		kFallback,
		kRejectAfterReady
	};

	struct SwapChainHandoffAttempt
	{
		DMUI_Result result{ DMUI_RESULT_INVALID_ARGUMENT };
		SwapChainHandoffAction action{ SwapChainHandoffAction::kFallback };
	};

	SwapChainHandoffAttempt AttemptSwapChainHandoff(
		const DMUI_HostAPI* a_api,
		DMUI_ClientHandle a_client,
		void* a_swapChain,
		IntegrationState a_state) noexcept;
}
