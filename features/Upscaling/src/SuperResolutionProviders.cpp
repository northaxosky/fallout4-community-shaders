#include "SuperResolutionProviders.h"

#include "FidelityFX.h"
#include "Streamline.h"
#include "Render/TemporalDevicePolicy.h"

namespace cs::features
{
	namespace
	{
		render::temporal::ProviderResult Success()
		{
			return { .code = render::temporal::ProviderResultCode::kSuccess };
		}

		render::temporal::ProviderResult Failure(std::string a_message)
		{
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.message = std::move(a_message)
			};
		}

	}

	FidelityFXSuperResolution::FidelityFXSuperResolution(
		FidelityFX& a_runtime) noexcept :
		_runtime(a_runtime)
	{}

	const char* FidelityFXSuperResolution::Name() const noexcept
	{
		return "FSR 3 Super Resolution";
	}

	render::temporal::ProviderResult FidelityFXSuperResolution::Initialize(
		const render::temporal::SuperResolutionInitContext& a_context)
	{
		_sizeCache.Clear();
		const auto* device = std::get_if<ID3D11Device*>(&a_context.device);
		if (!device || !*device) {
			return Failure("FSR 3 super resolution requires D3D11 recording.");
		}
		if ((*device)->GetFeatureLevel() <
			render::temporal::kFsrMinimumFeatureLevel) {
			return {
				.code = render::temporal::ProviderResultCode::kUnavailable,
				.message = "FSR 3 requires a D3D11 feature-level 11.1 device."
			};
		}
		return Success();
	}

	render::temporal::SuperResolutionSizeResult
		FidelityFXSuperResolution::QueryRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request)
	{
		if (const auto* cached = _sizeCache.Find(a_request)) {
			return *cached;
		}
		if (!a_request.outputWidth || !a_request.outputHeight ||
			a_request.qualityMode > 4) {
			return {
				.result = Failure(
					"FSR 3 received an invalid output extent or quality mode.")
			};
		}

		render::temporal::SuperResolutionSizeResult result;
		const auto sdkResult = ffxFsr3GetRenderResolutionFromQualityMode(
			&result.renderWidth,
			&result.renderHeight,
			a_request.outputWidth,
			a_request.outputHeight,
			static_cast<FfxFsr3QualityMode>(a_request.qualityMode));
		result.result.sdkResult = sdkResult;
		if (sdkResult != FFX_OK || !result.renderWidth ||
			!result.renderHeight) {
			result.result.code =
				render::temporal::ProviderResultCode::kFailure;
			result.result.message =
				"FSR 3 render-size query failed.";
			return result;
		}
		result.result.code =
			render::temporal::ProviderResultCode::kSuccess;
		_sizeCache.Store(a_request, result);
		return result;
	}

	render::temporal::ProviderResult FidelityFXSuperResolution::Record(
		const render::temporal::SuperResolutionRequest& a_request)
	{
		return _runtime.Upscale(a_request)
			? Success()
			: Failure("FSR 3 super-resolution evaluation failed.");
	}

	void FidelityFXSuperResolution::DestroyAfterDrain() noexcept
	{
		_sizeCache.Clear();
		_runtime.DestroyFSRResources();
	}

	StreamlineSuperResolution::StreamlineSuperResolution(
		Streamline& a_runtime) noexcept :
		_runtime(a_runtime)
	{}

	const char* StreamlineSuperResolution::Name() const noexcept
	{
		return "DLSS Super Resolution";
	}

	render::temporal::ProviderResult StreamlineSuperResolution::Initialize(
		const render::temporal::SuperResolutionInitContext& a_context)
	{
		_sizeCache.Clear();
		const auto* device11 =
			std::get_if<ID3D11Device*>(&a_context.device);
		const auto* device12 =
			std::get_if<ID3D12Device*>(&a_context.device);
		const bool d3d12 = device12 != nullptr;
		const bool deviceValid =
			(d3d12 && *device12) || (!d3d12 && device11 && *device11);
		return _runtime.featureDLSS &&
				_runtime.slDLSSGetOptimalSettings &&
				deviceValid &&
				d3d12 == _runtime.IsD3D12Session()
			? Success()
			: Failure(
				"DLSS super-resolution session is unavailable for the "
				"requested graphics API.");
	}

	render::temporal::SuperResolutionSizeResult
		StreamlineSuperResolution::QueryRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request)
	{
		if (const auto* cached = _sizeCache.Find(a_request)) {
			return *cached;
		}
		auto result = _runtime.QueryDLSSRenderSize(a_request);
		_sizeCache.Store(a_request, result);
		return result;
	}

	render::temporal::ProviderResult StreamlineSuperResolution::Record(
		const render::temporal::SuperResolutionRequest& a_request)
	{
		const bool succeeded = _runtime.IsD3D12Session()
			? _runtime.UpscaleD3D12(a_request)
			: _runtime.Upscale(a_request);
		return succeeded
			? Success()
			: Failure("DLSS super-resolution evaluation failed.");
	}

	void StreamlineSuperResolution::DestroyAfterDrain() noexcept
	{
		_sizeCache.Clear();
		_runtime.DestroyDLSSResources();
	}
}
