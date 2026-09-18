#include "SuperResolutionProviders.h"

#include "FidelityFX.h"
#include "Streamline.h"
#include "Render/TemporalDevicePolicy.h"

namespace cs::features
{
	namespace
	{
		render::temporal::ProviderResult Success(
			render::temporal::ProviderWorkState a_workState =
				render::temporal::ProviderWorkState::kNone,
			bool a_outputDependencyEstablished = false)
		{
			return { .code = render::temporal::ProviderResultCode::kSuccess,
				.workState = a_workState,
				.outputDependencyEstablished = a_outputDependencyEstablished };
		}

		render::temporal::ProviderResult Failure(
			std::string a_message,
			render::temporal::FailureDomain a_domain =
				render::temporal::FailureDomain::kSuperResolution)
		{
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.message = std::move(a_message),
				.failureDomain = a_domain
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
			return Failure("FSR 3 super resolution requires D3D11 recording.",
				render::temporal::FailureDomain::kTransport);
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
			? Success(render::temporal::ProviderWorkState::kOutputReady, true)
			: Failure("FSR 3 super-resolution evaluation failed.");
	}

	render::temporal::ProviderResult
	FidelityFXSuperResolution::DestroyAfterDrain() noexcept
	{
		_sizeCache.Clear();
		_runtime.DestroyFSRResources();
		return Success();
	}

	StreamlineSuperResolution::StreamlineSuperResolution(
		Streamline& a_runtime, Method a_method) noexcept :
		_runtime(a_runtime),
		_method(a_method)
	{}

	const char* StreamlineSuperResolution::Name() const noexcept
	{
		return _method == Method::kDLSS ?
			"DLSS Super Resolution" :
			"FSR 3 Super Resolution (Streamline)";
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
		if (!deviceValid || d3d12 != _runtime.IsD3D12Session()) {
			return Failure(
				"Streamline super-resolution is unavailable for the requested "
				"graphics API.",
				render::temporal::FailureDomain::kStreamline);
		}
		if (_method == Method::kDLSS) {
			return _runtime.featureDLSS &&
					_runtime.slDLSSGetOptimalSettings
				? Success()
				: Failure(
					"DLSS super-resolution is unavailable in this Streamline "
					"session.",
					render::temporal::FailureDomain::kStreamline);
		}
		return d3d12 && _runtime.featureFSR &&
				_runtime.slFSRGetOptimalSettings
			? Success()
			: Failure(
				"Native FSR 3 super-resolution is unavailable.",
				render::temporal::FailureDomain::kStreamline);
	}

	render::temporal::SuperResolutionSizeResult
		StreamlineSuperResolution::QueryRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request)
	{
		if (const auto* cached = _sizeCache.Find(a_request)) {
			return *cached;
		}
		auto result = _method == Method::kDLSS
			? _runtime.QueryDLSSRenderSize(a_request)
			: _runtime.QueryFSRRenderSize(a_request);
		_sizeCache.Store(a_request, result);
		return result;
	}

	render::temporal::ProviderResult StreamlineSuperResolution::Record(
		const render::temporal::SuperResolutionRequest& a_request)
	{
		if (_method == Method::kFSR3) {
			return _runtime.UpscaleFSRD3D12(a_request);
		}
		return _runtime.IsD3D12Session() ?
			_runtime.UpscaleD3D12(a_request) :
			_runtime.Upscale(a_request);
	}

	render::temporal::ProviderResult
	StreamlineSuperResolution::DestroyAfterDrain() noexcept
	{
		_sizeCache.Clear();
		return _method == Method::kDLSS ?
			_runtime.DestroyDLSSResources() :
			_runtime.DestroyFSRResources();
	}
}
