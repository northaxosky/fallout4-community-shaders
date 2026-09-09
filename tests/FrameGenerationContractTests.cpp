#include "Render/FrameGenerationOrchestration.h"

#pragma warning(push)
#pragma warning(disable: 4068 4100)
#include "FidelityFXFrameGenerationContract.h"
#pragma warning(pop)

#include "StreamlineFrameGenerationContract.h"
#include "XeSSFrameGenerationContract.h"

#pragma warning(push)
#pragma warning(disable: 4068 4100)
#include <FidelityFX/api/include/ffx_api.hpp>
#pragma warning(pop)

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	int failures = 0;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	class RecordingProvider final :
		public cs::render::temporal::IFrameGenerationProvider
	{
	public:
		const char* Name() const noexcept override { return "recording"; }
		cs::render::temporal::ProviderResult PrepareDevice(ID3D12Device**) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult PrepareFactory(IDXGIFactory4**) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult CreatePresentation(
			const cs::render::temporal::PresentationCreateContext&,
			IDXGISwapChain4**) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult CreateDisplayResources(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT,
			std::uint32_t) override
		{
			events.emplace_back(
				"create:" + std::to_string(a_width) + "x" +
				std::to_string(a_height));
			return createSucceeds ? Success() : Failure("create");
		}
		cs::render::temporal::ProviderResult PrepareFrame(
			const cs::render::temporal::FrameGenerationRequest&) override
		{
			events.emplace_back("prepare");
			return prepareSucceeds ? Success() : Failure("prepare");
		}
		cs::render::temporal::ProviderResult CancelFrame(
			const cs::render::temporal::FrameGenerationRequest&) override
		{
			events.emplace_back("cancel");
			return cancelSucceeds ? Success() : Failure("cancel");
		}
		cs::render::temporal::ProviderResult SetGenerationEnabled(bool a_enabled) override
		{
			events.emplace_back(a_enabled ? "enable" : "disable");
			return Success();
		}
		cs::render::temporal::ProviderResult AcquirePresentInputs() override
		{
			events.emplace_back("acquire");
			return acquireSucceeds ? Success() : Failure("acquire");
		}
		cs::render::temporal::ProviderResult CollectPresentStatus(
			UINT a_flags,
			HRESULT a_result) override
		{
			++statusCalls;
			lastStatusFlags = a_flags;
			lastPresentResult = a_result;
			return Success();
		}
		std::optional<std::uint32_t>
		ConsumeGeneratedFrameCount() noexcept override
		{
			return generatedCount;
		}
		std::optional<std::uint32_t>
		ConsumePresentedFrameCount() noexcept override
		{
			return presentedCount;
		}
		cs::render::temporal::ProviderResult Sleep(std::uint32_t) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult SetLatencyMarker(
			cs::render::temporal::LatencyMarker,
			std::uint32_t) override
		{
			return Success();
		}
		cs::render::temporal::ProviderResult Quiesce() override
		{
			events.emplace_back("quiesce");
			return quiesceSucceeds ? Success() : Failure("quiesce");
		}
		cs::render::temporal::ProviderResult ReleaseDisplayResources() noexcept override
		{
			events.emplace_back("release");
			return releaseSucceeds ? Success() : Failure("release");
		}
		cs::render::temporal::ProviderResult DestroyAfterDrain() noexcept override
		{
			events.emplace_back("destroy");
			return Success();
		}
		bool IsReady() const noexcept override { return true; }

		static cs::render::temporal::ProviderResult Success()
		{
			return {
				.code =
					cs::render::temporal::ProviderResultCode::kSuccess
			};
		}
		static cs::render::temporal::ProviderResult Failure(std::string a_message)
		{
			return {
				.code =
					cs::render::temporal::ProviderResultCode::kFailure,
				.message = std::move(a_message)
			};
		}

		std::vector<std::string> events;
		bool prepareSucceeds = true;
		bool cancelSucceeds = true;
		bool acquireSucceeds = true;
		bool quiesceSucceeds = true;
		bool releaseSucceeds = true;
		bool createSucceeds = true;
		std::uint32_t statusCalls = 0;
		UINT lastStatusFlags = 0;
		HRESULT lastPresentResult = E_FAIL;
		std::optional<std::uint32_t> generatedCount;
		std::optional<std::uint32_t> presentedCount;
	};

	void TestLifecycleOrderAndFailures()
	{
		RecordingProvider provider;
		const auto result = cs::render::temporal::QuiesceDrainAndRelease(
			provider,
			[&]() {
				provider.events.emplace_back("drain");
				return true;
			});
		Check(result.Succeeded(), "successful lifecycle completes");
		Check(
			provider.events ==
				std::vector<std::string>{ "quiesce", "drain", "release" },
			"provider disable/quiesce precedes CPU drain and release");

		provider.events.clear();
		const auto drainFailure =
			cs::render::temporal::QuiesceDrainAndRelease(
				provider,
				[&]() {
					provider.events.emplace_back("drain");
					return false;
				});
		Check(!drainFailure.Succeeded(), "drain failure is visible");
		Check(
			provider.events ==
				std::vector<std::string>{ "quiesce", "drain" },
			"drain failure never frees provider resources");

		provider.events.clear();
		provider.quiesceSucceeds = false;
		const auto quiesceFailure =
			cs::render::temporal::QuiesceDrainAndRelease(
				provider,
				[&]() {
					provider.events.emplace_back("drain");
					return true;
				});
		Check(!quiesceFailure.Succeeded(), "quiesce failure is visible");
		Check(
			provider.events == std::vector<std::string>{ "quiesce" },
			"quiesce failure performs neither drain nor release");
	}

	void TestResizeRestoration()
	{
		using cs::render::temporal::ProviderDisplayDescription;
		RecordingProvider provider;
		const ProviderDisplayDescription oldDescription{
			.width = 1280,
			.height = 720,
			.format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.bufferCount = 2
		};
		const ProviderDisplayDescription changedDescription{
			.width = 1920,
			.height = 1080,
			.format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.bufferCount = 2
		};

		auto result = cs::render::temporal::RestoreProviderAfterResize(
			provider, S_OK, oldDescription, oldDescription);
		Check(result.Succeeded(), "same-size resize restores provider readiness");
		Check(
			provider.events.back() == "create:1280x720",
			"same-size resize recreates provider display resources");

		result = cs::render::temporal::RestoreProviderAfterResize(
			provider, S_OK, oldDescription, changedDescription);
		Check(result.Succeeded(), "changed-size resize restores provider readiness");
		Check(
			provider.events.back() == "create:1920x1080",
			"changed-size resize uses the actual new description");

		const HRESULT rejected = DXGI_ERROR_INVALID_CALL;
		const auto restoration =
			cs::render::temporal::RestoreProviderAndPreserveResizeResult(
			provider, rejected, oldDescription, std::nullopt);
		Check(
			restoration.providerResult.Succeeded(),
			"rejected resize attempts old-provider restoration");
		Check(
			provider.events.back() == "create:1280x720",
			"rejected resize restores from the retained description");
		Check(
			restoration.nativeResizeResult == rejected,
			"provider restoration preserves the rejected native resize HRESULT");

		provider.createSucceeds = false;
		result = cs::render::temporal::RestoreProviderAfterResize(
			provider, S_OK, oldDescription, oldDescription);
		Check(!result.Succeeded(), "provider recreation failure remains visible for quarantine");
		Check(
			cs::render::temporal::CompleteNativeResize(
				S_OK, S_FALSE) == S_OK,
			"provider-only recreation failure preserves a successful native resize");
		Check(
			cs::render::temporal::CompleteNativeResize(
				S_OK, E_OUTOFMEMORY) == E_OUTOFMEMORY,
			"bridge-resource failure publishes an explicit invalid resize result");
	}

	void TestResizeCommitProtocol()
	{
		DXGI_SWAP_CHAIN_DESC proxy{};
		proxy.BufferDesc.Width = 1280;
		proxy.BufferDesc.Height = 720;
		proxy.BufferCount = 2;
		DXGI_SWAP_CHAIN_DESC1 inner{};
		inner.Width = 1280;
		inner.Height = 720;
		inner.BufferCount = 2;
		std::array<std::uint64_t, 2> fenceValues{ 7, 9 };
		cs::render::temporal::PresentInputReuseGate gate;
		gate.MarkSubmitted(1, true);
		std::uint32_t frameSlot = 1;
		bool presentPrepared = true;
		bool preparedGeneration = true;
		bool vendorConsumption = true;
		bool preparedTransaction = true;
		const DXGI_SWAP_CHAIN_DESC1 resized{
			.Width = 1920,
			.Height = 1080,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.SampleDesc = { 1, 0 },
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = 2,
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_IGNORE,
			.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
		};
		cs::render::temporal::CommitResizeBridgeState(
			resized,
			proxy,
			inner,
			fenceValues,
			gate,
			frameSlot,
			presentPrepared,
			preparedGeneration,
			vendorConsumption,
			preparedTransaction);
		Check(
			proxy.BufferDesc.Width == 1920 &&
				proxy.BufferDesc.Height == 1080 &&
				inner.Width == 1920 &&
				inner.Height == 1080,
			"successful native resize commits the actual bridge descriptor");
		Check(
			fenceValues == std::array<std::uint64_t, 2>{ 0, 0 } &&
				frameSlot == 0 && !gate.IsPending(1) &&
				!presentPrepared && !preparedGeneration &&
				!vendorConsumption && !preparedTransaction,
			"resize commit invalidates prepared and retry protocol state");
		Check(
			cs::render::temporal::CompleteNativeResize(
				S_OK, S_FALSE) == S_OK &&
				proxy.BufferDesc.Width == 1920 &&
				inner.Width == 1920,
			"provider quarantine after commit cannot roll published metadata back to the old native size");
	}

	void TestSafePreparation()
	{
		RecordingProvider provider;
		cs::render::temporal::FrameGenerationRequest request;
		provider.prepareSucceeds = false;
		const auto safe =
			cs::render::temporal::PrepareFrameSafely(provider, request);
		Check(!safe.prepared && safe.safeToPresent, "failed preparation is safe only after cancellation");
		provider.events.emplace_back("present");
		Check(
			provider.events ==
				std::vector<std::string>{ "prepare", "cancel", "present" },
			"SDK disable/cancellation precedes the same underlying Present");

		provider.events.clear();
		provider.cancelSucceeds = false;
		const auto unsafe =
			cs::render::temporal::PrepareFrameSafely(provider, request);
		if (unsafe.safeToPresent) {
			provider.events.emplace_back("present");
		}
		Check(!unsafe.safeToPresent, "failed cancellation blocks unsafe interpolation submission");
		Check(
			provider.events ==
				std::vector<std::string>{ "prepare", "cancel" },
			"unsafe cancellation path does not call Present");
	}

	void TestInputReuseGate()
	{
		cs::render::temporal::PresentInputReuseGate gate;
		gate.MarkSubmitted(0, true);
		std::uint32_t acquisitions = 0;
		bool complete = false;
		Check(
			!gate.Acquire(0, [&]() {
				++acquisitions;
				return complete;
			}),
			"delayed provider completion blocks borrowed-input reuse");
		Check(gate.IsPending(0), "failed acquisition keeps the slot pending");

		complete = true;
		Check(
			gate.Acquire(0, [&]() {
				++acquisitions;
				return complete;
			}),
			"completed provider use permits the first shared write");
		Check(
			gate.Acquire(0, [&]() {
				++acquisitions;
				return complete;
			}),
			"alpha, raw, and HUD-less writes share one acquisition");
		Check(acquisitions == 2, "successful acquisition runs once for the producer frame");
	}

	void TestXeSSCopyContract()
	{
		cs::render::temporal::FrameGenerationRequest request;
		request.realFrame = 42;
		request.renderWidth = 1280;
		request.renderHeight = 720;
		request.outputWidth = 1920;
		request.outputHeight = 1080;
		request.recording.commandList =
			reinterpret_cast<ID3D12GraphicsCommandList*>(0x1234);
		request.depth.resource = reinterpret_cast<ID3D12Resource*>(0x10);
		request.motionVectors.resource = reinterpret_cast<ID3D12Resource*>(0x20);
		request.hudlessColor.resource = reinterpret_cast<ID3D12Resource*>(0x30);
		request.finalColor.resource = reinterpret_cast<ID3D12Resource*>(0x40);

		struct Call
		{
			xefg_swapchain_resource_type_t type;
			xefg_swapchain_resource_validity_t validity;
			ID3D12GraphicsCommandList* commandList;
			std::uint32_t frame;
		};
		std::vector<Call> calls;
		const auto result = cs::features::xess_fg::TagFrameResources(
			reinterpret_cast<xefg_swapchain_handle_t>(0x1),
			request,
			[&](xefg_swapchain_handle_t,
				ID3D12GraphicsCommandList* a_commandList,
				std::uint32_t a_frame,
				const xefg_swapchain_d3d12_resource_data_t* a_resource) {
				calls.push_back({
					a_resource->type,
					a_resource->validity,
					a_commandList,
					a_frame
				});
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			});
		Check(result == XEFG_SWAPCHAIN_RESULT_SUCCESS, "XeSS tags record successfully");
		Check(calls.size() == 4, "XeSS records all required resource tags");
		for (std::size_t index = 0; index < 3; ++index) {
			Check(
				calls[index].validity == XEFG_SWAPCHAIN_RV_ONLY_NOW &&
					calls[index].commandList == request.recording.commandList &&
					calls[index].frame == 42,
				"XeSS borrowed inputs copy on the same application command list");
		}
		Check(
			calls[3].type == XEFG_SWAPCHAIN_RES_BACKBUFFER &&
				calls[3].commandList == nullptr,
			"XeSS backbuffer tag remains region-only");

		for (std::size_t failureIndex = 0;
			 failureIndex < 4;
			 ++failureIndex) {
			calls.clear();
			const auto failed = cs::features::xess_fg::TagFrameResources(
				reinterpret_cast<xefg_swapchain_handle_t>(0x1),
				request,
				[&](xefg_swapchain_handle_t,
					ID3D12GraphicsCommandList* a_commandList,
					std::uint32_t a_frame,
					const xefg_swapchain_d3d12_resource_data_t*
						a_resource) {
					calls.push_back({
						a_resource->type,
						a_resource->validity,
						a_commandList,
						a_frame
					});
					return calls.size() - 1 == failureIndex
						? XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT
						: XEFG_SWAPCHAIN_RESULT_SUCCESS;
				});
			Check(
				failed ==
						XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT &&
					calls.size() == failureIndex + 1,
				"XeSS tagging stops at the first failed SDK call");
		}

		calls.clear();
		request.recording.commandList = nullptr;
		const auto missingCommandList =
			cs::features::xess_fg::TagFrameResources(
				reinterpret_cast<xefg_swapchain_handle_t>(0x1),
				request,
				[&](xefg_swapchain_handle_t,
					ID3D12GraphicsCommandList*,
					std::uint32_t,
					const xefg_swapchain_d3d12_resource_data_t*) {
					calls.push_back({});
					return XEFG_SWAPCHAIN_RESULT_SUCCESS;
				});
		Check(
			missingCommandList ==
					XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT &&
				calls.empty(),
			"XeSS rejects ONLY_NOW tags before any SDK call when the app command list is missing");
	}

	void TestStatusAndAccountingPolicies()
	{
		using cs::features::xess_fg::ClassifyPresentStatus;
		using cs::features::xess_fg::PresentStatusClass;
		Check(
			ClassifyPresentStatus(
				XEFG_SWAPCHAIN_RESULT_SUCCESS,
				XEFG_SWAPCHAIN_RESULT_SUCCESS) ==
				PresentStatusClass::kSuccess,
			"XeSS successful status is accepted");
		Check(
			ClassifyPresentStatus(
				XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS,
				XEFG_SWAPCHAIN_RESULT_SUCCESS) ==
				PresentStatusClass::kUnavailable,
			"XeSS warmup status is unavailable rather than fatal");
		Check(
			ClassifyPresentStatus(
				XEFG_SWAPCHAIN_RESULT_SUCCESS,
				XEFG_SWAPCHAIN_RESULT_WARNING_TOO_FEW_FRAMES) ==
				PresentStatusClass::kWarning,
			"XeSS history warmup warnings do not quarantine");
		Check(
			ClassifyPresentStatus(
				XEFG_SWAPCHAIN_RESULT_SUCCESS,
				XEFG_SWAPCHAIN_RESULT_ERROR_MISMATCH_INPUT_RESOURCES) ==
				PresentStatusClass::kError,
			"XeSS interpolation errors are fatal");

		cs::render::temporal::PresentedFrameAccumulator counts;
		counts.Add(0);
		counts.Add(1);
		counts.Add(4);
		Check(
			counts.Consume() == 5,
			"read-reset SDK counts retain zero, one, and multiple presented frames");
		Check(counts.Consume() == 0, "presented-frame accumulator is read-reset");

		Check(
			static_cast<std::uint32_t>(
				ffx::ReturnCode::ErrorProviderNoSupportNewDesctype) == 7,
			"FidelityFX return code 7 remains named and numerically intact");
		ffx::QueryGetProviderVersion version;
		Check(
			version.header.type ==
				FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION,
			"FidelityFX provider-version query uses the compiled descriptor type");
	}

	void TestXeSSFrameAndStatusOrchestration()
	{
		std::vector<std::string> events;
		const auto disabled = cs::features::xess_fg::BeginFrame(
			reinterpret_cast<xefg_swapchain_handle_t>(0x1),
			73,
			false,
			[&](xefg_swapchain_handle_t, std::uint32_t a_id) {
				events.emplace_back("id:" + std::to_string(a_id));
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			},
			[&](xefg_swapchain_handle_t, std::uint32_t a_enabled) {
				events.emplace_back(
					"enabled:" + std::to_string(a_enabled));
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			});
		Check(
			disabled.result == XEFG_SWAPCHAIN_RESULT_SUCCESS &&
				disabled.enablementApplied &&
				events == std::vector<std::string>{
					"id:73", "enabled:0" },
			"XeSS applies the real Present ID before disabling an inactive frame");

		events.clear();
		const auto enabled = cs::features::xess_fg::BeginFrame(
			reinterpret_cast<xefg_swapchain_handle_t>(0x1),
			74,
			true,
			[&](xefg_swapchain_handle_t, std::uint32_t a_id) {
				events.emplace_back("id:" + std::to_string(a_id));
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			},
			[&](xefg_swapchain_handle_t, std::uint32_t) {
				events.emplace_back("unexpected-enable");
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			});
		Check(
			enabled.result == XEFG_SWAPCHAIN_RESULT_SUCCESS &&
				!enabled.enablementApplied &&
				events == std::vector<std::string>{ "id:74" },
			"XeSS enabled-frame setup records the ID without premature enablement");

		events.clear();
		const auto idFailure = cs::features::xess_fg::BeginFrame(
			reinterpret_cast<xefg_swapchain_handle_t>(0x1),
			75,
			false,
			[&](xefg_swapchain_handle_t, std::uint32_t) {
				events.emplace_back("id");
				return XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT;
			},
			[&](xefg_swapchain_handle_t, std::uint32_t) {
				events.emplace_back("enabled");
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			});
		Check(
			idFailure.result ==
					XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT &&
				events == std::vector<std::string>{ "id" },
			"XeSS disabled-frame setup short-circuits when the Present ID is rejected");

		Check(
			cs::render::temporal::ShouldObservePresentStatus(0, S_OK),
			"accepted real Present observes provider status");
		Check(
			!cs::render::temporal::ShouldObservePresentStatus(
				DXGI_PRESENT_TEST, S_OK),
			"TEST Present never observes provider status");
		Check(
			!cs::render::temporal::ShouldObservePresentStatus(
				0, DXGI_ERROR_WAS_STILL_DRAWING),
			"retryable Present does not replay the previous status snapshot");
		Check(
			!cs::render::temporal::ShouldObservePresentStatus(
				0, DXGI_ERROR_DEVICE_REMOVED),
			"failed native Present does not publish stale provider status");

		RecordingProvider provider;
		provider.generatedCount = 2;
		provider.presentedCount = 3;
		auto collected =
			cs::render::temporal::CollectAcceptedPresentStatus(
				provider, 0, S_OK);
		Check(
			collected.observed && provider.statusCalls == 1 &&
				collected.generatedFrames == 2u &&
				collected.presentedFrames == 3u &&
				provider.lastStatusFlags == 0 &&
				provider.lastPresentResult == S_OK,
			"production status orchestration polls and retains counts after one accepted real Present");
		collected =
			cs::render::temporal::CollectAcceptedPresentStatus(
				provider, DXGI_PRESENT_TEST, S_OK);
		Check(
			!collected.observed && provider.statusCalls == 1,
			"production status orchestration does not poll TEST Presents");
		collected =
			cs::render::temporal::CollectAcceptedPresentStatus(
				provider, 0, DXGI_ERROR_WAS_STILL_DRAWING);
		Check(
			!collected.observed && provider.statusCalls == 1,
			"production status orchestration does not poll retry attempts");

		std::uint32_t statusCalls = 0;
		auto observation = cs::features::xess_fg::ObservePresentStatus(
			reinterpret_cast<xefg_swapchain_handle_t>(0x1),
			true,
			[&](xefg_swapchain_handle_t,
				xefg_swapchain_present_status_t*) {
				++statusCalls;
				return XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS;
			});
		Check(
			statusCalls == 1 && !observation.countAvailable &&
				observation.classification ==
					cs::features::xess_fg::PresentStatusClass::kUnavailable,
			"missing XeSS status is explicitly unavailable, not a zero-count sample");

		observation = cs::features::xess_fg::ObservePresentStatus(
			reinterpret_cast<xefg_swapchain_handle_t>(0x1),
			true,
			[&](xefg_swapchain_handle_t,
				xefg_swapchain_present_status_t* a_status) {
				++statusCalls;
				a_status->frameGenResult =
					XEFG_SWAPCHAIN_RESULT_SUCCESS;
				a_status->framesPresented = 3;
				a_status->isFrameGenEnabled = 1;
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			});
		Check(
			observation.countAvailable &&
				observation.presentedFrames == 3 &&
				observation.generatedFrames == 2 &&
				observation.enablementMatches,
			"accepted XeSS status publishes the SDK presentation counts");

		observation = cs::features::xess_fg::ObservePresentStatus(
			reinterpret_cast<xefg_swapchain_handle_t>(0x1),
			true,
			[&](xefg_swapchain_handle_t,
				xefg_swapchain_present_status_t* a_status) {
				++statusCalls;
				a_status->frameGenResult =
					XEFG_SWAPCHAIN_RESULT_WARNING_TOO_FEW_FRAMES;
				a_status->framesPresented = 1;
				a_status->isFrameGenEnabled = 0;
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			});
		Check(
			observation.classification ==
					cs::features::xess_fg::PresentStatusClass::kWarning &&
				observation.countAvailable &&
				observation.enablementMatches,
			"XeSS warmup warning remains nonfatal even before enabled state is reported");

		observation = cs::features::xess_fg::ObservePresentStatus(
			reinterpret_cast<xefg_swapchain_handle_t>(0x1),
			false,
			[&](xefg_swapchain_handle_t,
				xefg_swapchain_present_status_t* a_status) {
				++statusCalls;
				a_status->frameGenResult =
					XEFG_SWAPCHAIN_RESULT_SUCCESS;
				a_status->framesPresented = 1;
				a_status->isFrameGenEnabled = 0;
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			});
		Check(
			observation.countAvailable &&
				observation.presentedFrames == 1 &&
				observation.generatedFrames == 0 &&
				observation.enablementMatches,
			"disabled XeSS Present retains its aligned real-frame status");

		observation = cs::features::xess_fg::ObservePresentStatus(
			reinterpret_cast<xefg_swapchain_handle_t>(0x1),
			true,
			[&](xefg_swapchain_handle_t,
				xefg_swapchain_present_status_t* a_status) {
				++statusCalls;
				a_status->frameGenResult =
					XEFG_SWAPCHAIN_RESULT_ERROR_MISMATCH_INPUT_RESOURCES;
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			});
		Check(
			!observation.countAvailable &&
				observation.classification ==
					cs::features::xess_fg::PresentStatusClass::kError,
			"XeSS interpolation error does not publish success-shaped counts");

		auto fg = reinterpret_cast<xefg_swapchain_handle_t>(0x10);
		auto latency = reinterpret_cast<xell_context_handle_t>(0x20);
		std::vector<std::string> destructionEvents;
		auto destruction = cs::features::xess_fg::DestroyContexts(
			fg,
			latency,
			[&](xefg_swapchain_handle_t) {
				destructionEvents.emplace_back("fg");
				return XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_CONTEXT;
			},
			[&](xell_context_handle_t) {
				destructionEvents.emplace_back("latency");
				return XELL_RESULT_SUCCESS;
			});
		Check(
			!destruction.succeeded && fg != nullptr &&
				latency != nullptr &&
				destructionEvents ==
					std::vector<std::string>{ "fg" },
			"failed XeSS-FG destruction preserves both handles and skips XeLL teardown");

		destructionEvents.clear();
		destruction = cs::features::xess_fg::DestroyContexts(
			fg,
			latency,
			[&](xefg_swapchain_handle_t) {
				destructionEvents.emplace_back("fg");
				return XEFG_SWAPCHAIN_RESULT_SUCCESS;
			},
			[&](xell_context_handle_t) {
				destructionEvents.emplace_back("latency");
				return XELL_RESULT_ERROR_DEVICE;
			});
		Check(
			!destruction.succeeded && fg == nullptr &&
				latency != nullptr &&
				destructionEvents ==
					std::vector<std::string>{ "fg", "latency" },
			"failed XeLL destruction preserves its live handle and prevents module unload");
	}

	void TestStreamlineBackendContracts()
	{
		cs::render::temporal::PresentedFrameAccumulator counts;
		sl::DLSSGStatus status = sl::DLSSGStatus::eOk;
		std::uint32_t calls = 0;
		for (const std::uint32_t count : { 0u, 1u, 4u }) {
			const auto result = cs::features::streamline_fg::PollState(
				sl::ViewportHandle{ 1 },
				counts,
				status,
				[&](sl::ViewportHandle,
					sl::DLSSGState& a_state,
					const sl::DLSSGOptions*) {
					++calls;
					a_state.numFramesActuallyPresented = count;
					a_state.status = sl::DLSSGStatus::eOk;
					return sl::Result::eOk;
				});
			Check(result == sl::Result::eOk, "DLSS-G state query succeeds");
		}
		Check(
			calls == 3 && counts.Consume() == 5,
			"the one DLSS-G state owner retains 0, 1, and multiple presentations");

		std::vector<std::string> cleanupEvents;
		sl::DLSSGOptions recordedOptions{};
		const auto cleanup = cs::features::streamline_fg::DestroyResources(
			true,
			sl::ViewportHandle{ 9 },
			[&]() {
				cleanupEvents.emplace_back("clear");
				return sl::Result::eOk;
			},
			[&](sl::ViewportHandle,
				const sl::DLSSGOptions& a_options) {
				cleanupEvents.emplace_back("options");
				recordedOptions = a_options;
				return sl::Result::eOk;
			},
			[&](sl::Feature a_feature, sl::ViewportHandle) {
				cleanupEvents.emplace_back("free");
				Check(
					a_feature == sl::kFeatureDLSS_G,
					"DLSS-G cleanup frees only the DLSS-G allocation");
				return sl::Result::eOk;
			});
		Check(
			cleanup.succeeded &&
				cleanupEvents == std::vector<std::string>{
					"clear", "options", "free" } &&
				recordedOptions.mode == sl::DLSSGMode::eOff,
			"DLSS-G cleanup checks clear, disable, and free in order");

		cleanupEvents.clear();
		const auto idempotent =
			cs::features::streamline_fg::DestroyResources(
				false,
				sl::ViewportHandle{ 9 },
				[&]() {
					cleanupEvents.emplace_back("clear");
					return sl::Result::eOk;
				},
				[&](sl::ViewportHandle,
					const sl::DLSSGOptions&) {
					cleanupEvents.emplace_back("options");
					return sl::Result::eOk;
				},
				[&](sl::Feature, sl::ViewportHandle) {
					cleanupEvents.emplace_back("free");
					return sl::Result::eOk;
				});
		Check(
			idempotent.succeeded && cleanupEvents.empty(),
			"unallocated DLSS-G cleanup is idempotent and makes no SDK calls");

		cleanupEvents.clear();
		const auto clearFailure =
			cs::features::streamline_fg::DestroyResources(
				true,
				sl::ViewportHandle{ 9 },
				[&]() {
					cleanupEvents.emplace_back("clear");
					return sl::Result::eErrorInvalidState;
				},
				[&](sl::ViewportHandle,
					const sl::DLSSGOptions&) {
					cleanupEvents.emplace_back("options");
					return sl::Result::eOk;
				},
				[&](sl::Feature, sl::ViewportHandle) {
					cleanupEvents.emplace_back("free");
					return sl::Result::eOk;
				});
		Check(
			!clearFailure.succeeded &&
				cleanupEvents == std::vector<std::string>{ "clear" },
			"DLSS-G cleanup failure is propagated before later release calls");

		cleanupEvents.clear();
		const auto optionsFailure =
			cs::features::streamline_fg::DestroyResources(
				true,
				sl::ViewportHandle{ 9 },
				[&]() {
					cleanupEvents.emplace_back("clear");
					return sl::Result::eOk;
				},
				[&](sl::ViewportHandle,
					const sl::DLSSGOptions&) {
					cleanupEvents.emplace_back("options");
					return sl::Result::eErrorInvalidState;
				},
				[&](sl::Feature, sl::ViewportHandle) {
					cleanupEvents.emplace_back("free");
					return sl::Result::eOk;
				});
		Check(
			!optionsFailure.succeeded &&
				cleanupEvents == std::vector<std::string>{
					"clear", "options" },
			"DLSS-G disable failure prevents resource free");

		cleanupEvents.clear();
		const auto freeFailure =
			cs::features::streamline_fg::DestroyResources(
				true,
				sl::ViewportHandle{ 9 },
				[&]() {
					cleanupEvents.emplace_back("clear");
					return sl::Result::eOk;
				},
				[&](sl::ViewportHandle,
					const sl::DLSSGOptions&) {
					cleanupEvents.emplace_back("options");
					return sl::Result::eOk;
				},
				[&](sl::Feature, sl::ViewportHandle) {
					cleanupEvents.emplace_back("free");
					return sl::Result::eErrorInvalidState;
				});
		Check(
			!freeFailure.succeeded &&
				cleanupEvents == std::vector<std::string>{
					"clear", "options", "free" },
			"DLSS-G free failure remains visible and does not report safe cleanup");
	}

	void TestFidelityFXBackendContracts()
	{
		auto* swapChain = reinterpret_cast<IDXGISwapChain4*>(0x1234);
		const auto config =
			cs::features::fidelityfx_fg::BuildDisabledConfiguration(
				swapChain, 88, 1920, 1080);
		Check(
			!config.frameGenerationEnabled &&
				config.frameGenerationCallback == nullptr &&
				config.frameGenerationCallbackUserContext == nullptr &&
				config.HUDLessColor.resource == nullptr &&
				config.presentCallback == nullptr &&
				config.presentCallbackUserContext == nullptr &&
				config.swapChain == swapChain &&
				config.frameID == 88 &&
				config.flags == 0 &&
				config.generationRect.width == 1920 &&
				config.generationRect.height == 1080,
			"FSR deconfiguration detaches callbacks, user context, and HUD-less input before destruction");

		ffx::Context context{};
		const auto unsupported =
			cs::features::fidelityfx_fg::QueryProviderVersion(
				context,
				[](ffx::Context&,
					ffx::QueryGetProviderVersion&) {
					return ffx::ReturnCode::
						ErrorProviderNoSupportNewDesctype;
				});
		Check(
			!unsupported.available && unsupported.id == 0 &&
				unsupported.name.empty() &&
				unsupported.result ==
					ffx::ReturnCode::
						ErrorProviderNoSupportNewDesctype,
			"unsupported FSR provider-version query is explicit and non-fatal");
	}

	void TestProductionInputWriteOrdering()
	{
		cs::render::temporal::PresentInputReuseGate gate;
		gate.MarkSubmitted(1, true);
		bool providerComplete = false;
		std::vector<std::string> events;
		auto write = [&](std::string a_name) {
			return cs::render::temporal::WriteFrameGenerationInput(
				[&]() {
					return gate.Acquire(1, [&]() {
						events.emplace_back("provider-wait");
						return providerComplete;
					});
				},
				[&]() {
					events.emplace_back(std::move(a_name));
				});
		};
		Check(
			!write("alpha") &&
				events == std::vector<std::string>{ "provider-wait" },
			"delayed completion prevents the first shared write");
		providerComplete = true;
		Check(write("alpha"), "completed slot permits alpha input write");
		Check(write("raw"), "same slot permits raw input write");
		Check(write("hudless"), "same slot permits HUD-less input write");
		Check(
			events == std::vector<std::string>{
				"provider-wait", "provider-wait", "alpha", "raw",
				"hudless" },
			"production write wrapper waits before the first write and only once after completion across alpha, raw, and HUD-less inputs");
	}

	void TestDisableDrainDestroyOrder()
	{
		std::vector<std::string> events;
		const auto result = cs::render::temporal::DisableDrainAndDestroy(
			[&]() {
				events.emplace_back("disable-null-callbacks");
				return true;
			},
			[&]() {
				events.emplace_back("drain");
				return true;
			},
			[&]() {
				events.emplace_back("destroy-context");
				return true;
			});
		Check(
			result ==
				cs::render::temporal::DestructionResult::kSuccess,
			"callback deconfiguration lifecycle succeeds");
		Check(
			events == std::vector<std::string>{
				"disable-null-callbacks", "drain", "destroy-context" },
			"callbacks are detached before drain and context destruction");

		events.clear();
		const auto disableFailure =
			cs::render::temporal::DisableDrainAndDestroy(
				[&]() {
					events.emplace_back("disable-null-callbacks");
					return false;
				},
				[&]() {
					events.emplace_back("drain");
					return true;
				},
				[&]() {
					events.emplace_back("destroy-context");
					return true;
				});
		Check(
			disableFailure ==
				cs::render::temporal::DestructionResult::kDisableFailed &&
				events ==
					std::vector<std::string>{ "disable-null-callbacks" },
			"failed callback deconfiguration prevents drain and destruction");
	}
}

int main()
{
	TestLifecycleOrderAndFailures();
	TestResizeRestoration();
	TestResizeCommitProtocol();
	TestSafePreparation();
	TestInputReuseGate();
	TestXeSSCopyContract();
	TestStatusAndAccountingPolicies();
	TestXeSSFrameAndStatusOrchestration();
	TestStreamlineBackendContracts();
	TestFidelityFXBackendContracts();
	TestProductionInputWriteOrdering();
	TestDisableDrainDestroyOrder();
	if (failures) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "Frame-generation contract tests passed\n";
	return 0;
}
