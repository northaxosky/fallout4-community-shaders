#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace cs::render::temporal
{
	enum class ProviderResultCode : std::uint8_t
	{
		kSuccess,
		kUnavailable,
		kSkipped,
		kFailure
	};

	struct ProviderResult
	{
		ProviderResultCode code = ProviderResultCode::kFailure;
		std::int64_t sdkResult = 0;
		std::string message;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return code == ProviderResultCode::kSuccess;
		}
	};

	enum class ColorRange : std::uint8_t
	{
		kUnknown,
		kFull
	};

	enum class TransferFunction : std::uint8_t
	{
		kUnknown,
		kLinear,
		kGamma22,
		kSRGB
	};

	enum class ColorPrimaries : std::uint8_t
	{
		kUnspecified
	};

	enum class ColorStage : std::uint8_t
	{
		kUnknown,
		kPostTonemapLut
	};

	enum class AlphaMode : std::uint8_t
	{
		kUnknown,
		kIgnored,
		kStraight,
		kPremultiplied
	};

	enum class ExposureMode : std::uint8_t
	{
		kUnknown,
		kAutomatic,
		kExplicit
	};

	struct ColorContract
	{
		DXGI_FORMAT resourceFormat = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
		ColorRange range = ColorRange::kUnknown;
		TransferFunction transfer = TransferFunction::kUnknown;
		ColorPrimaries primaries = ColorPrimaries::kUnspecified;
		ColorStage stage = ColorStage::kUnknown;
		AlphaMode alpha = AlphaMode::kUnknown;
		ExposureMode exposure = ExposureMode::kUnknown;
		float exposureValue = 1.0f;
		float preExposure = 1.0f;
	};

	[[nodiscard]] inline bool IsFo4PostTonemapSdr(
		const ColorContract& a_color) noexcept
	{
		return a_color.resourceFormat == DXGI_FORMAT_R8G8B8A8_UNORM &&
			a_color.range == ColorRange::kFull &&
			a_color.transfer == TransferFunction::kGamma22 &&
			a_color.primaries == ColorPrimaries::kUnspecified &&
			a_color.stage == ColorStage::kPostTonemapLut &&
			a_color.alpha == AlphaMode::kIgnored &&
			a_color.exposure == ExposureMode::kAutomatic;
	}

	struct D3D11GpuView
	{
		ID3D11Resource* resource = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		ID3D11UnorderedAccessView* uav = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;
	};

	struct D3D12GpuView
	{
		ID3D12Resource* resource = nullptr;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
	};

	using GpuView = std::variant<std::monostate, D3D11GpuView, D3D12GpuView>;

	struct D3D11RecordingContext
	{
		ID3D11DeviceContext* context = nullptr;
	};

	struct D3D12RecordingContext
	{
		ID3D12GraphicsCommandList* commandList = nullptr;
		ID3D12CommandQueue* queue = nullptr;
		std::uint32_t slot = 0;
	};

	using RecordingContext =
		std::variant<D3D11RecordingContext, D3D12RecordingContext>;

	struct FrameGenerationCamera
	{
		float currentWorldToClip[16]{};
		float previousWorldToClip[16]{};
		float viewToWorld[16]{};
		float position[3]{};
		float previousPosition[3]{};
		float right[3]{};
		float up[3]{};
		float forward[3]{};
		float nearPlane = 0.0f;
		float farPlane = 1.0f;
		float verticalFov = 0.0f;
		float aspectRatio = 0.0f;
		std::uint32_t engineFrame = 0;
		bool valid = false;
	};

	struct SuperResolutionInitContext
	{
		std::variant<ID3D11Device*, ID3D12Device*> device;
		std::uint32_t maxRenderWidth = 0;
		std::uint32_t maxRenderHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
	};

	struct SuperResolutionSizeRequest
	{
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		std::uint32_t qualityMode = 0;

		[[nodiscard]] bool operator==(
			const SuperResolutionSizeRequest&) const noexcept = default;
	};

	struct SuperResolutionSizeResult
	{
		ProviderResult result;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return result.Succeeded() && renderWidth > 0 && renderHeight > 0;
		}
	};

	class SuperResolutionSizeCache
	{
	public:
		[[nodiscard]] const SuperResolutionSizeResult* Find(
			const SuperResolutionSizeRequest& a_request) const noexcept
		{
			return _valid && _request == a_request ? &_result : nullptr;
		}

		void Store(
			const SuperResolutionSizeRequest& a_request,
			const SuperResolutionSizeResult& a_result)
		{
			if (!a_result.Succeeded()) {
				return;
			}
			_request = a_request;
			_result = a_result;
			_valid = true;
		}

		void Clear() noexcept
		{
			_valid = false;
			_request = {};
			_result = {};
		}

	private:
		SuperResolutionSizeRequest _request;
		SuperResolutionSizeResult _result;
		bool _valid = false;
	};

	struct SuperResolutionRequest
	{
		RecordingContext recording;
		GpuView colorInput;
		GpuView privateOutput;
		GpuView depth;
		GpuView motionVectors;
		GpuView reactiveMask;
		GpuView transparencyCompositionMask;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		std::uint32_t qualityMode = 0;
		std::uint32_t providerPreset = 0;
		std::uint64_t realFrame = 0;
		std::uint32_t engineFrame = 0;
		float jitterX = 0.0f;
		float jitterY = 0.0f;
		float sharpness = 0.0f;
		float frameTimeMilliseconds = 0.0f;
		float cameraNear = 0.0f;
		float cameraFar = 1.0f;
		float cameraVerticalFov = 0.0f;
		bool resetHistory = false;
		ColorContract color;
		FrameGenerationCamera camera;
	};

	class ISuperResolutionProvider
	{
	public:
		virtual ~ISuperResolutionProvider() = default;
		[[nodiscard]] virtual const char* Name() const noexcept = 0;
		[[nodiscard]] virtual ProviderResult Initialize(
			const SuperResolutionInitContext& a_context) = 0;
		[[nodiscard]] virtual SuperResolutionSizeResult QueryRenderSize(
			const SuperResolutionSizeRequest& a_request) = 0;
		[[nodiscard]] virtual ProviderResult Record(
			const SuperResolutionRequest& a_request) = 0;
		virtual void DestroyAfterDrain() noexcept = 0;
	};

	enum class UiCompositionMode : std::uint8_t
	{
		kFinalColorOnly,
		kHudlessAndFinal,
		kHudlessAndPremultipliedLayer
	};

	enum class LatencyMarker : std::uint8_t
	{
		kInputSample,
		kSimulationStart,
		kSimulationEnd,
		kRenderSubmitStart,
		kRenderSubmitEnd,
		kPresentStart,
		kPresentEnd
	};

	struct PresentationCreateContext
	{
		IDXGIAdapter* adapter = nullptr;
		ID3D12Device* device = nullptr;
		ID3D12CommandQueue* queue = nullptr;
		IDXGIFactory4* factory = nullptr;
		HWND window = nullptr;
		DXGI_SWAP_CHAIN_DESC1* description = nullptr;
	};

	struct FrameGenerationRequest
	{
		D3D12RecordingContext recording;
		D3D12GpuView depth;
		D3D12GpuView motionVectors;
		D3D12GpuView hudlessColor;
		D3D12GpuView finalColor;
		D3D12GpuView uiColorAndAlpha;
		std::uint64_t realFrame = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		float jitterX = 0.0f;
		float jitterY = 0.0f;
		float frameTimeMilliseconds = 0.0f;
		bool enabled = false;
		bool resetHistory = false;
		UiCompositionMode uiMode = UiCompositionMode::kFinalColorOnly;
		ColorContract color;
		FrameGenerationCamera camera;
	};

	class IFrameGenerationProvider
	{
	public:
		virtual ~IFrameGenerationProvider() = default;
		[[nodiscard]] virtual const char* Name() const noexcept = 0;
		[[nodiscard]] virtual ProviderResult PrepareDevice(
			ID3D12Device** a_device) = 0;
		[[nodiscard]] virtual ProviderResult PrepareFactory(
			IDXGIFactory4** a_factory) = 0;
		[[nodiscard]] virtual ProviderResult CreatePresentation(
			const PresentationCreateContext& a_context,
			IDXGISwapChain4** a_swapChain) = 0;
		[[nodiscard]] virtual ProviderResult CreateDisplayResources(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_format,
			std::uint32_t a_bufferCount) = 0;
		[[nodiscard]] virtual ProviderResult PrepareFrame(
			const FrameGenerationRequest& a_request) = 0;
		[[nodiscard]] virtual ProviderResult CancelFrame(
			const FrameGenerationRequest& a_request) = 0;
		[[nodiscard]] virtual ProviderResult SetGenerationEnabled(bool a_enabled) = 0;
		[[nodiscard]] virtual ProviderResult AcquirePresentInputs() = 0;
		[[nodiscard]] virtual ProviderResult CollectPresentStatus(
			UINT a_presentFlags,
			HRESULT a_presentResult) = 0;
		[[nodiscard]] virtual std::optional<std::uint32_t>
			ConsumeGeneratedFrameCount() noexcept
		{
			return std::nullopt;
		}
		[[nodiscard]] virtual std::optional<std::uint32_t>
			ConsumePresentedFrameCount() noexcept
		{
			return std::nullopt;
		}
		[[nodiscard]] virtual ProviderResult Sleep(std::uint32_t a_frame) = 0;
		[[nodiscard]] virtual ProviderResult SetLatencyMarker(
			LatencyMarker a_marker,
			std::uint32_t a_frame) = 0;
		[[nodiscard]] virtual ProviderResult Quiesce() = 0;
		[[nodiscard]] virtual ProviderResult ReleaseDisplayResources() noexcept = 0;
		[[nodiscard]] virtual ProviderResult DestroyAfterDrain() noexcept = 0;
		[[nodiscard]] virtual bool IsReady() const noexcept = 0;
	};
}
