#include "Host/SwapChainHandoff.h"

namespace cs::host
{
	SwapChainHandoffAttempt AttemptSwapChainHandoff(
		const DMUI_HostAPI* a_api,
		DMUI_ClientHandle a_client,
		void* a_swapChain,
		IntegrationState a_state) noexcept
	{
		DMUI_Result result = DMUI_RESULT_INVALID_ARGUMENT;
		if (a_api && a_client != DMUI_INVALID_CLIENT_HANDLE && a_swapChain) {
			if (a_api->structSize < DMUI_HOST_API_ATTACH_SWAP_CHAIN_SIZE)
				result = DMUI_RESULT_STRUCT_TOO_SMALL;
			else if (!a_api->attachSwapChain)
				result = DMUI_RESULT_INVALID_DESCRIPTOR;
			else
				result = a_api->attachSwapChain(a_client, a_swapChain);
		}

		if (result == DMUI_RESULT_OK)
			return { result, SwapChainHandoffAction::kAccepted };
		if (result == DMUI_RESULT_RENDERER_BUSY)
			return { result, SwapChainHandoffAction::kRetry };
		return {
			result,
			a_state == IntegrationState::kHostedReady ?
				SwapChainHandoffAction::kRejectAfterReady :
				SwapChainHandoffAction::kFallback
		};
	}
}
