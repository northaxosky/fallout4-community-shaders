#include "SuperResolutionProviders.h"

#include "FidelityFX.h"
#include "Streamline.h"

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

		SuperResolutionExecutionContext LegacyContext(
			const render::temporal::SuperResolutionRequest& a_request)
		{
			const auto* recording =
				std::get_if<render::temporal::D3D11RecordingContext>(
					&a_request.recording);
			const auto view = [](const auto& a_resource) {
				return std::get_if<render::temporal::D3D11GpuView>(
					&a_resource);
			};
			const auto* color = view(a_request.colorInput);
			const auto* output = view(a_request.privateOutput);
			const auto* depth = view(a_request.depth);
			const auto* motion = view(a_request.motionVectors);
			const auto* reactive = view(a_request.reactiveMask);
			const auto* transparency =
				view(a_request.transparencyCompositionMask);
			return {
				.commandContext = recording ? recording->context : nullptr,
				.colorInput = color ? color->resource : nullptr,
				.privateOutput = output ? output->resource : nullptr,
				.depth = depth ? depth->resource : nullptr,
				.motionVectors = motion ? motion->resource : nullptr,
				.reactiveMask = reactive ? reactive->resource : nullptr,
				.transparencyCompositionMask =
					transparency ? transparency->resource : nullptr,
				.renderWidth = a_request.renderWidth,
				.renderHeight = a_request.renderHeight,
				.outputWidth = a_request.outputWidth,
				.outputHeight = a_request.outputHeight,
				.qualityMode = a_request.qualityMode,
				.providerPreset = a_request.providerPreset,
				.realFrame = a_request.realFrame,
				.engineFrame = a_request.engineFrame,
				.jitterX = a_request.jitterX,
				.jitterY = a_request.jitterY,
				.sharpness = a_request.sharpness,
				.frameTimeMilliseconds =
					a_request.frameTimeMilliseconds,
				.cameraNear = a_request.cameraNear,
				.cameraFar = a_request.cameraFar,
				.cameraVerticalFov = a_request.cameraVerticalFov,
				.resetHistory = a_request.resetHistory,
				.color = a_request.color,
				.camera = a_request.camera
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
		return device && *device
			? Success()
			: Failure("FSR 3 super resolution requires D3D11 recording.");
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
		return _runtime.Upscale(LegacyContext(a_request))
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
			: _runtime.Upscale(LegacyContext(a_request));
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
