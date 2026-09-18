#include "SuperResolutionProviders.h"

#include "Streamline.h"

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
		const auto* device12 =
			std::get_if<ID3D12Device*>(&a_context.device);
		if (!device12 || !*device12) {
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
		return _runtime.featureFSR &&
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
		return _runtime.UpscaleD3D12(a_request);
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
