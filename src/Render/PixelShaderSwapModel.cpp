#include "Render/PixelShaderSwapBroker.h"

namespace cs::engine
{
	PixelShaderSwapObserverInvocation BeginPixelShaderSwapObserver(
		PixelShaderSwapObserver a_observer,
		const void* a_bytecode,
		std::size_t a_bytecodeLength) noexcept
	{
		PixelShaderSwapObserverInvocation invocation;
		invocation.observer = a_observer;
		if (a_observer.beginAdmission) {
			invocation.admitted = a_observer.beginAdmission();
			if (!invocation.admitted)
				return invocation;
		}
		invocation.active = true;
		if (a_observer.prepare) {
			invocation.token =
				a_observer.prepare(a_bytecode, a_bytecodeLength);
		}
		return invocation;
	}

	void CompletePixelShaderSwapObserver(
		PixelShaderSwapObserverInvocation& a_invocation,
		const PixelShaderSwapCompletion& a_completion) noexcept
	{
		if (a_invocation.active && a_invocation.observer.complete) {
			a_invocation.observer.complete(
				a_invocation.token, a_completion);
		}
		if (a_invocation.admitted
			&& a_invocation.observer.endAdmission) {
			a_invocation.observer.endAdmission();
		}
		a_invocation = {};
	}

	PixelShaderSwapCompletion ClassifyPixelShaderSwapCompletion(
		std::int32_t a_originalResult,
		bool a_outputRequested,
		ID3D11PixelShader* a_stockOutput,
		bool a_resolverInvoked,
		bool a_resolverReportedReplacement,
		ID3D11PixelShader* a_finalOutput) noexcept
	{
		PixelShaderSwapCompletion result;
		result.originalResult = a_originalResult;
		result.outputRequested = a_outputRequested;
		result.stockOutput = a_stockOutput;
		result.resolverInvoked = a_resolverInvoked;
		result.resolverReportedReplacement = a_resolverReportedReplacement;
		result.finalOutput = a_finalOutput;
		const bool originalSucceeded = a_originalResult >= 0;
		const bool usableFinal = originalSucceeded
			&& a_outputRequested
			&& a_finalOutput != nullptr;
		result.finalIsNull = a_outputRequested && a_finalOutput == nullptr;
		result.finalIsStock = usableFinal
			&& a_finalOutput == a_stockOutput;
		result.finalIsReplacement = usableFinal
			&& a_stockOutput != nullptr
			&& a_finalOutput != a_stockOutput;
		return result;
	}
}
