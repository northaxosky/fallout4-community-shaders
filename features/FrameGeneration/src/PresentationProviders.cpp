#include "PresentationProviders.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <utility>

#include <DirectXMath.h>
#include <winrt/base.h>

#include "FidelityFX.h"
#include "Streamline.h"

namespace cs::features
{
	namespace
	{
		using render::temporal::ProviderResult;
		using render::temporal::ProviderResultCode;

		ProviderResult Success()
		{
			return { .code = ProviderResultCode::kSuccess };
		}

		ProviderResult Failure(std::string a_message, std::int64_t a_result = 0)
		{
			return {
				.code = ProviderResultCode::kFailure,
				.sdkResult = a_result,
				.message = std::move(a_message)
			};
		}
	}

	FidelityFXPresentation::FidelityFXPresentation(
		FidelityFX& a_runtime) noexcept :
		_runtime(a_runtime)
	{}

	const char* FidelityFXPresentation::Name() const noexcept
	{
		return "FSR3-FG";
	}

	ProviderResult FidelityFXPresentation::PrepareDevice(ID3D12Device** a_device)
	{
		return a_device && *a_device
			? Success()
			: Failure("FidelityFX received no D3D12 device.");
	}

	ProviderResult FidelityFXPresentation::PrepareFactory(IDXGIFactory4** a_factory)
	{
		return a_factory && *a_factory
			? Success()
			: Failure("FidelityFX received no DXGI factory.");
	}

	ProviderResult FidelityFXPresentation::CreatePresentation(
		const render::temporal::PresentationCreateContext& a_context,
		IDXGISwapChain4** a_swapChain)
	{
		if (!a_context.device || !a_context.queue || !a_context.factory ||
			!a_context.window || !a_context.description || !a_swapChain) {
			return Failure("FidelityFX presentation creation received an incomplete context.");
		}
		_device = a_context.device;
		const HRESULT result = _runtime.CreateSwapChainContext(
			a_context.device,
			a_context.queue,
			a_context.factory,
			a_context.window,
			*a_context.description,
			a_swapChain);
		if (FAILED(result)) {
			return Failure("FidelityFX swap-chain creation failed.", result);
		}
		_swapChain = *a_swapChain;
		return Success();
	}

	ProviderResult FidelityFXPresentation::CreateDisplayResources(
		std::uint32_t a_width,
		std::uint32_t a_height,
		DXGI_FORMAT a_format,
		std::uint32_t)
	{
		return _runtime.CreateFrameGenerationContext(
			_device, a_width, a_height, a_format)
			? Success()
			: Failure("FidelityFX frame-generation context creation failed.");
	}

	ProviderResult FidelityFXPresentation::PrepareFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		FidelityFX::FrameGenerationCameraSnapshot camera{};
		std::copy_n(a_request.camera.right, 3, camera.right);
		std::copy_n(a_request.camera.up, 3, camera.up);
		std::copy_n(a_request.camera.forward, 3, camera.forward);
		std::copy_n(a_request.camera.position, 3, camera.position);
		camera.nearPlane = a_request.camera.nearPlane;
		camera.farPlane = a_request.camera.farPlane;
		camera.verticalFov = a_request.camera.verticalFov;
		camera.frameTimeDelta = a_request.frameTimeMilliseconds;
		camera.frameCount = a_request.camera.engineFrame;
		camera.valid = a_request.camera.valid;
		if (a_request.enabled &&
			!_runtime.SetFrameGenerationCameraData(camera)) {
			return Failure("FidelityFX rejected the frame camera.");
		}
		const bool prepared = _runtime.PresentFrameGeneration(
			a_request.recording.commandList,
			_swapChain,
			a_request.hudlessColor.resource,
			a_request.depth.resource,
			a_request.motionVectors.resource,
			a_request.enabled,
			a_request.renderWidth,
			a_request.renderHeight,
			a_request.outputWidth,
			a_request.outputHeight,
			a_request.jitterX,
			a_request.jitterY,
			a_request.color);
		return prepared
			? Success()
			: Failure("FidelityFX frame preparation failed.");
	}

	ProviderResult FidelityFXPresentation::SetGenerationEnabled(bool a_enabled)
	{
		_enabled = a_enabled;
		return Success();
	}

	ProviderResult FidelityFXPresentation::Quiesce()
	{
		_enabled = false;
		return _runtime.WaitForPresents()
			? Success()
			: Failure("FidelityFX did not drain pending presents.");
	}

	ProviderResult FidelityFXPresentation::WaitForPresentInputs()
	{
		return _runtime.WaitForPresents()
			? Success()
			: Failure("FidelityFX did not retire present inputs.");
	}

	ProviderResult FidelityFXPresentation::Sleep(std::uint32_t)
	{
		return Success();
	}

	ProviderResult FidelityFXPresentation::SetLatencyMarker(
		render::temporal::LatencyMarker,
		std::uint32_t)
	{
		return Success();
	}

	void FidelityFXPresentation::ReleaseDisplayResources() noexcept
	{
		_runtime.DestroyFrameGenerationContext();
	}

	void FidelityFXPresentation::DestroyAfterDrain() noexcept
	{
		(void)Quiesce();
		_runtime.DestroyFrameGenerationContext();
		_runtime.DestroySwapChainContext();
		_swapChain = nullptr;
		_device = nullptr;
	}

	bool FidelityFXPresentation::IsReady() const noexcept
	{
		return _runtime.IsFrameGenerationContextReady();
	}

	StreamlinePresentation::StreamlinePresentation(
		Streamline& a_runtime) noexcept :
		_runtime(a_runtime)
	{}

	const char* StreamlinePresentation::Name() const noexcept
	{
		return "DLSS-G";
	}

	ProviderResult StreamlinePresentation::PrepareDevice(ID3D12Device** a_device)
	{
		return _runtime.PrepareD3D12Device(a_device)
			? Success()
			: Failure("Streamline could not upgrade and bind the D3D12 device.");
	}

	ProviderResult StreamlinePresentation::PrepareFactory(IDXGIFactory4** a_factory)
	{
		return _runtime.PrepareDXGIFactory(a_factory)
			? Success()
			: Failure("Streamline could not upgrade the DXGI factory.");
	}

	ProviderResult StreamlinePresentation::CreatePresentation(
		const render::temporal::PresentationCreateContext& a_context,
		IDXGISwapChain4** a_swapChain)
	{
		if (!a_context.device || !a_context.queue || !a_context.factory ||
			!a_context.window || !a_context.description || !a_swapChain ||
			!_runtime.IsD3D12Session()) {
			return Failure("DLSS-G presentation creation received an incompatible context.");
		}
		_runtime.CheckFeatures(a_context.adapter);
		_runtime.PostDevice();
		if (!_runtime.featureDLSSG || !_runtime.featurePCL ||
			!_runtime.featureReflex) {
			return Failure("DLSS-G, Reflex, or PCL is unavailable.");
		}
		winrt::com_ptr<IDXGISwapChain1> swapChain;
		const HRESULT result = a_context.factory->CreateSwapChainForHwnd(
			a_context.queue,
			a_context.window,
			a_context.description,
			nullptr,
			nullptr,
			swapChain.put());
		if (FAILED(result)) {
			return Failure("Streamline swap-chain creation failed.", result);
		}
		const HRESULT queryResult =
			swapChain->QueryInterface(IID_PPV_ARGS(a_swapChain));
		if (FAILED(queryResult)) {
			return Failure(
				"Streamline swap-chain does not expose IDXGISwapChain4.",
				queryResult);
		}
		_queue = a_context.queue;
		return Success();
	}

	ProviderResult StreamlinePresentation::CreateDisplayResources(
		std::uint32_t a_width,
		std::uint32_t a_height,
		DXGI_FORMAT a_format,
		std::uint32_t a_bufferCount)
	{
		if (a_format != DXGI_FORMAT_R8G8B8A8_UNORM) {
			return Failure("DLSS-G requires the supported SDR swap-chain format.");
		}
		_width = a_width;
		_height = a_height;
		_bufferCount = a_bufferCount;
		_ready = _runtime.ConfigureDLSSG(
			false, a_width, a_height, a_width, a_height, a_bufferCount, true);
		return _ready
			? Success()
			: Failure("DLSS-G display-resource preflight failed.");
	}

	ProviderResult StreamlinePresentation::PrepareFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		if (!_ready) {
			return Failure("DLSS-G presentation is not ready.");
		}
		if (!_runtime.ConfigureDLSSG(
			a_request.enabled,
			a_request.renderWidth,
			a_request.renderHeight,
			a_request.outputWidth,
			a_request.outputHeight,
			_bufferCount,
			true)) {
			return Failure("DLSS-G options were rejected.");
		}
		_enabled = a_request.enabled;
		if (!a_request.enabled) {
			return Success();
		}
		return _runtime.TagDLSSGFrame(a_request)
			? Success()
			: Failure("DLSS-G input tagging failed.");
	}

	ProviderResult StreamlinePresentation::SetGenerationEnabled(bool a_enabled)
	{
		if (!_ready) {
			return Failure("DLSS-G presentation is not ready.");
		}
		_enabled = a_enabled;
		return _runtime.ConfigureDLSSG(
			a_enabled,
			_width,
			_height,
			_width,
			_height,
			_bufferCount,
			true)
			? Success()
			: Failure("DLSS-G enablement change failed.");
	}

	ProviderResult StreamlinePresentation::Quiesce()
	{
		_enabled = false;
		if (_ready &&
			!_runtime.ConfigureDLSSG(
				false,
				_width,
				_height,
				_width,
				_height,
				_bufferCount,
				true)) {
			return Failure("DLSS-G could not be disabled.");
		}

		return _runtime.WaitForDLSSGInputs(_queue)
			? Success()
			: Failure("DLSS-G inputs did not reach a safe retirement point.");
	}

	ProviderResult StreamlinePresentation::WaitForPresentInputs()
	{
		return _runtime.WaitForDLSSGInputs(_queue)
			? Success()
			: Failure("DLSS-G did not retire present inputs.");
	}

	std::optional<std::uint32_t>
	StreamlinePresentation::ConsumeGeneratedFrameCount() noexcept
	{
		return _runtime.ConsumeDLSSGGeneratedFrameCount();
	}

	ProviderResult StreamlinePresentation::Sleep(std::uint32_t a_frame)
	{
		return _runtime.Sleep(a_frame)
			? Success()
			: Failure("Reflex sleep failed.");
	}

	ProviderResult StreamlinePresentation::SetLatencyMarker(
		render::temporal::LatencyMarker a_marker,
		std::uint32_t a_frame)
	{
		sl::PCLMarker marker = sl::PCLMarker::eSimulationStart;
		switch (a_marker) {
		case render::temporal::LatencyMarker::kInputSample:
			marker = sl::PCLMarker::eControllerInputSample;
			break;
		case render::temporal::LatencyMarker::kSimulationStart:
			marker = sl::PCLMarker::eSimulationStart;
			break;
		case render::temporal::LatencyMarker::kSimulationEnd:
			marker = sl::PCLMarker::eSimulationEnd;
			break;
		case render::temporal::LatencyMarker::kRenderSubmitStart:
			marker = sl::PCLMarker::eRenderSubmitStart;
			break;
		case render::temporal::LatencyMarker::kRenderSubmitEnd:
			marker = sl::PCLMarker::eRenderSubmitEnd;
			break;
		case render::temporal::LatencyMarker::kPresentStart:
			marker = sl::PCLMarker::ePresentStart;
			break;
		case render::temporal::LatencyMarker::kPresentEnd:
			marker = sl::PCLMarker::ePresentEnd;
			break;
		}
		return _runtime.SetLatencyMarker(marker, a_frame)
			? Success()
			: Failure("PCL marker failed.");
	}

	void StreamlinePresentation::ReleaseDisplayResources() noexcept
	{
		(void)SetGenerationEnabled(false);
		_runtime.DestroyDLSSGResources();
		_ready = false;
	}

	void StreamlinePresentation::DestroyAfterDrain() noexcept
	{
		(void)Quiesce();
		_runtime.DestroyDLSSGResources();
		_queue = nullptr;
		_ready = false;
	}

	bool StreamlinePresentation::IsReady() const noexcept
	{
		return _ready && _runtime.featureDLSSG &&
			_runtime.featurePCL && _runtime.featureReflex;
	}

	XeSSPresentation::~XeSSPresentation()
	{
		DestroyAfterDrain();
	}

	const char* XeSSPresentation::Name() const noexcept
	{
		return "XeSS-FG";
	}

	bool XeSSPresentation::LoadRuntimes()
	{
		if (_frameGenerationModule && _latencyModule) {
			return true;
		}
		const auto directory =
			std::filesystem::path(L"Data\\Shaders\\Upscaling\\XeSS");
		_frameGenerationModule =
			LoadLibraryW((directory / L"libxess_fg.dll").c_str());
		_latencyModule =
			LoadLibraryW((directory / L"libxell.dll").c_str());
		if (!_frameGenerationModule || !_latencyModule) {
			return false;
		}
		_createContext = Load<decltype(_createContext)>(
			_frameGenerationModule, "xefgSwapChainD3D12CreateContext");
		_initialize = Load<decltype(_initialize)>(
			_frameGenerationModule, "xefgSwapChainD3D12InitFromSwapChainDesc");
		_getSwapChain = Load<decltype(_getSwapChain)>(
			_frameGenerationModule, "xefgSwapChainD3D12GetSwapChainPtr");
		_tagResource = Load<decltype(_tagResource)>(
			_frameGenerationModule, "xefgSwapChainD3D12TagFrameResource");
		_tagConstants = Load<decltype(_tagConstants)>(
			_frameGenerationModule, "xefgSwapChainTagFrameConstants");
		_setPresentId = Load<decltype(_setPresentId)>(
			_frameGenerationModule, "xefgSwapChainSetPresentId");
		_setEnabled = Load<decltype(_setEnabled)>(
			_frameGenerationModule, "xefgSwapChainSetEnabled");
		_getPresentStatus = Load<decltype(_getPresentStatus)>(
			_frameGenerationModule, "xefgSwapChainGetLastPresentStatus");
		_setLatencyReduction = Load<decltype(_setLatencyReduction)>(
			_frameGenerationModule, "xefgSwapChainSetLatencyReduction");
		_destroyContext = Load<decltype(_destroyContext)>(
			_frameGenerationModule, "xefgSwapChainDestroy");
		_createLatency = Load<decltype(_createLatency)>(
			_latencyModule, "xellD3D12CreateContext");
		_setSleepMode = Load<decltype(_setSleepMode)>(
			_latencyModule, "xellSetSleepMode");
		_sleep = Load<decltype(_sleep)>(_latencyModule, "xellSleep");
		_addMarker = Load<decltype(_addMarker)>(
			_latencyModule, "xellAddMarkerData");
		_destroyLatency = Load<decltype(_destroyLatency)>(
			_latencyModule, "xellDestroyContext");
		return _createContext && _initialize && _getSwapChain &&
			_tagResource && _tagConstants && _setPresentId &&
			_setEnabled && _getPresentStatus && _setLatencyReduction &&
			_destroyContext && _createLatency && _setSleepMode &&
			_sleep && _addMarker && _destroyLatency;
	}

	ProviderResult XeSSPresentation::PrepareDevice(ID3D12Device** a_device)
	{
		if (!a_device || !*a_device || !LoadRuntimes()) {
			return Failure("XeSS-FG or XeLL runtime loading failed.");
		}
		auto result = _createLatency(*a_device, &_latency);
		if (result != XELL_RESULT_SUCCESS) {
			return Failure("XeLL context creation failed.", result);
		}
		const xell_sleep_params_t sleep{
			.minimumIntervalUs = 0,
			.bLowLatencyMode = 1,
			.bLowLatencyBoost = 0,
			.reserved = 0
		};
		result = _setSleepMode(_latency, &sleep);
		if (result != XELL_RESULT_SUCCESS) {
			return Failure("XeLL low-latency mode setup failed.", result);
		}
		const auto fgResult = _createContext(*a_device, &_context);
		if (fgResult != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			return Failure("XeSS-FG context creation failed.", fgResult);
		}
		const auto latencyResult =
			_setLatencyReduction(_context, _latency);
		return latencyResult == XEFG_SWAPCHAIN_RESULT_SUCCESS
			? Success()
			: Failure(
				"XeSS-FG rejected the XeLL context.", latencyResult);
	}

	ProviderResult XeSSPresentation::PrepareFactory(
		IDXGIFactory4** a_factory)
	{
		return a_factory && *a_factory
			? Success()
			: Failure("XeSS-FG received no DXGI factory.");
	}

	ProviderResult XeSSPresentation::CreatePresentation(
		const render::temporal::PresentationCreateContext& a_context,
		IDXGISwapChain4** a_swapChain)
	{
		if (!_context || !a_context.queue || !a_context.factory ||
			!a_context.window || !a_context.description ||
			!a_swapChain) {
			return Failure("XeSS-FG presentation creation received an incomplete context.");
		}
		const xefg_swapchain_d3d12_init_params_t init{
			.pApplicationSwapChain = nullptr,
			.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE,
			.maxInterpolatedFrames = 1,
			.creationNodeMask = 0,
			.visibleNodeMask = 0,
			.uiMode = XEFG_SWAPCHAIN_UI_MODE_BACKBUFFER_HUDLESS
		};
		const auto result = _initialize(
			_context,
			a_context.window,
			a_context.description,
			nullptr,
			a_context.queue,
			a_context.factory,
			&init);
		if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			return Failure("XeSS-FG swap-chain initialization failed.", result);
		}
		const auto pointerResult = _getSwapChain(
			_context, IID_PPV_ARGS(a_swapChain));
		if (pointerResult != XEFG_SWAPCHAIN_RESULT_SUCCESS ||
			!*a_swapChain) {
			return Failure("XeSS-FG did not publish its swap chain.", pointerResult);
		}
		_queue = a_context.queue;
		_ready = true;
		return Success();
	}

	ProviderResult XeSSPresentation::CreateDisplayResources(
		std::uint32_t,
		std::uint32_t,
		DXGI_FORMAT a_format,
		std::uint32_t)
	{
		return _ready && a_format == DXGI_FORMAT_R8G8B8A8_UNORM
			? Success()
			: Failure("XeSS-FG display format is unsupported.");
	}

	ProviderResult XeSSPresentation::PrepareFrame(
		const render::temporal::FrameGenerationRequest& a_request)
	{
		if (!_ready) {
			return Failure("XeSS-FG presentation is not ready.");
		}
		if (!a_request.enabled) {
			return SetGenerationEnabled(false);
		}
		if (!a_request.camera.valid ||
			!a_request.depth.resource ||
			!a_request.motionVectors.resource ||
			!a_request.hudlessColor.resource ||
			!a_request.finalColor.resource) {
			return Failure("XeSS-FG frame inputs are incomplete.");
		}
		const auto presentId =
			static_cast<std::uint32_t>(a_request.realFrame);
		const auto tag = [&](xefg_swapchain_resource_type_t a_type,
							 const render::temporal::D3D12GpuView& a_view,
							 std::uint32_t a_width,
							 std::uint32_t a_height) {
			const xefg_swapchain_d3d12_resource_data_t resource{
				.type = a_type,
				.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT,
				.resourceBase = { 0, 0 },
				.resourceSize = { a_width, a_height },
				.pResource = a_view.resource,
				.incomingState = a_view.state
			};
			return _tagResource(
				_context, nullptr, presentId, &resource);
		};
		if (tag(
				XEFG_SWAPCHAIN_RES_DEPTH,
				a_request.depth,
				a_request.renderWidth,
				a_request.renderHeight) !=
				XEFG_SWAPCHAIN_RESULT_SUCCESS ||
			tag(
				XEFG_SWAPCHAIN_RES_MOTION_VECTOR,
				a_request.motionVectors,
				a_request.renderWidth,
				a_request.renderHeight) !=
				XEFG_SWAPCHAIN_RESULT_SUCCESS ||
			tag(
				XEFG_SWAPCHAIN_RES_HUDLESS_COLOR,
				a_request.hudlessColor,
				a_request.outputWidth,
				a_request.outputHeight) !=
				XEFG_SWAPCHAIN_RESULT_SUCCESS ||
			tag(
				XEFG_SWAPCHAIN_RES_BACKBUFFER,
				a_request.finalColor,
				a_request.outputWidth,
				a_request.outputHeight) !=
				XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			return Failure("XeSS-FG resource tagging failed.");
		}

		using namespace DirectX;
		const auto position = XMVectorSet(
			a_request.camera.position[0],
			a_request.camera.position[1],
			a_request.camera.position[2],
			1.0f);
		const auto forward = XMVectorSet(
			a_request.camera.forward[0],
			a_request.camera.forward[1],
			a_request.camera.forward[2],
			0.0f);
		const auto up = XMVectorSet(
			a_request.camera.up[0],
			a_request.camera.up[1],
			a_request.camera.up[2],
			0.0f);
		const auto view = XMMatrixLookToLH(position, forward, up);
		const auto projection = XMMatrixPerspectiveFovLH(
			a_request.camera.verticalFov,
			static_cast<float>(a_request.outputWidth) /
				static_cast<float>(a_request.outputHeight),
			a_request.camera.nearPlane,
			a_request.camera.farPlane);
		XMFLOAT4X4 viewStorage{};
		XMFLOAT4X4 projectionStorage{};
		XMStoreFloat4x4(&viewStorage, view);
		XMStoreFloat4x4(&projectionStorage, projection);
		xefg_swapchain_frame_constant_data_t constants{};
		std::memcpy(constants.viewMatrix, &viewStorage, sizeof(viewStorage));
		std::memcpy(
			constants.projectionMatrix,
			&projectionStorage,
			sizeof(projectionStorage));
		constants.jitterOffsetX = -a_request.jitterX;
		constants.jitterOffsetY = -a_request.jitterY;
		constants.motionVectorScaleX =
			static_cast<float>(a_request.renderWidth);
		constants.motionVectorScaleY =
			static_cast<float>(a_request.renderHeight);
		constants.resetHistory = a_request.resetHistory ? 1u : 0u;
		constants.frameRenderTime = a_request.frameTimeMilliseconds;
		if (_tagConstants(_context, presentId, &constants) !=
				XEFG_SWAPCHAIN_RESULT_SUCCESS ||
			_setPresentId(_context, presentId) !=
				XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			return Failure("XeSS-FG frame constants were rejected.");
		}
		return SetGenerationEnabled(a_request.enabled);
	}

	ProviderResult XeSSPresentation::SetGenerationEnabled(bool a_enabled)
	{
		if (!_ready || !_setEnabled) {
			return Failure("XeSS-FG presentation is not ready.");
		}
		const auto result = _setEnabled(_context, a_enabled ? 1u : 0u);
		if (result == XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			_enabled = a_enabled;
			return Success();
		}
		return Failure("XeSS-FG enablement change failed.", result);
	}

	ProviderResult XeSSPresentation::WaitForPresentInputs()
	{
		if (!_ready) {
			return Success();
		}
		xefg_swapchain_present_status_t status{};
		const auto result = _getPresentStatus(_context, &status);
		if (result == XEFG_SWAPCHAIN_RESULT_SUCCESS &&
			status.framesPresented > 1) {
			_generatedFrames += status.framesPresented - 1;
		}
		return result == XEFG_SWAPCHAIN_RESULT_SUCCESS ||
				result == XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS
			? Success()
			: Failure("XeSS-FG present inputs were not retired.", result);
	}

	std::optional<std::uint32_t> XeSSPresentation::ConsumeGeneratedFrameCount() noexcept
	{
		return std::exchange(_generatedFrames, 0);
	}

	ProviderResult XeSSPresentation::Sleep(std::uint32_t a_frame)
	{
		if (!_latency || !_sleep) {
			return Failure("XeLL is not ready.");
		}
		const auto result = _sleep(_latency, a_frame);
		return result == XELL_RESULT_SUCCESS
			? Success()
			: Failure("XeLL sleep failed.", result);
	}

	ProviderResult XeSSPresentation::SetLatencyMarker(
		render::temporal::LatencyMarker a_marker,
		std::uint32_t a_frame)
	{
		if (!_latency || !_addMarker) {
			return Failure("XeLL is not ready.");
		}
		xell_latency_marker_type_t marker = XELL_SIMULATION_START;
		switch (a_marker) {
		case render::temporal::LatencyMarker::kInputSample:
			marker = XELL_INPUT_SAMPLE;
			break;
		case render::temporal::LatencyMarker::kSimulationStart:
			marker = XELL_SIMULATION_START;
			break;
		case render::temporal::LatencyMarker::kSimulationEnd:
			marker = XELL_SIMULATION_END;
			break;
		case render::temporal::LatencyMarker::kRenderSubmitStart:
			marker = XELL_RENDERSUBMIT_START;
			break;
		case render::temporal::LatencyMarker::kRenderSubmitEnd:
			marker = XELL_RENDERSUBMIT_END;
			break;
		case render::temporal::LatencyMarker::kPresentStart:
			marker = XELL_PRESENT_START;
			break;
		case render::temporal::LatencyMarker::kPresentEnd:
			marker = XELL_PRESENT_END;
			break;
		}
		const auto result = _addMarker(_latency, a_frame, marker);
		return result == XELL_RESULT_SUCCESS
			? Success()
			: Failure("XeLL marker failed.", result);
	}

	ProviderResult XeSSPresentation::Quiesce()
	{
		return _ready ? SetGenerationEnabled(false) : Success();
	}

	void XeSSPresentation::ReleaseDisplayResources() noexcept
	{
		(void)Quiesce();
	}

	void XeSSPresentation::DestroyAfterDrain() noexcept
	{
		(void)Quiesce();
		if (_context && _destroyContext) {
			(void)_destroyContext(_context);
		}
		_context = nullptr;
		if (_latency && _destroyLatency) {
			(void)_destroyLatency(_latency);
		}
		_latency = nullptr;
		if (_frameGenerationModule) {
			FreeLibrary(_frameGenerationModule);
		}
		if (_latencyModule) {
			FreeLibrary(_latencyModule);
		}
		_frameGenerationModule = nullptr;
		_latencyModule = nullptr;
		_queue = nullptr;
		_ready = false;
		_enabled = false;
		_generatedFrames = 0;
	}

	bool XeSSPresentation::IsReady() const noexcept
	{
		return _ready && _context && _latency;
	}
}
