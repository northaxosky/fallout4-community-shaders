#pragma once

#include "Render/FrameGenerationCpuTiming.h"
#include "Render/TemporalPipelineState.h"
#include "Render/TemporalProvider.h"
#include "Render/SwapChainHook.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

struct IDXGIAdapter;
struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D11UnorderedAccessView;

namespace cs::features
{
	struct SuperResolutionExecutionContext;
}

namespace cs::render
{
	class TemporalRenderer;

	enum class TemporalCreationState : std::uint8_t
	{
		kUnregistered,
		kRegistered,
		kFrozen,
		kCreating,
		kNative,
		kProxy,
		kFailed
	};

	enum class TemporalTraceEvent : std::uint8_t
	{
		kInputPacket,
		kPresentPrepared,
		kPresentTest,
		kPresentRetry,
		kPresentOccluded,
		kPresentAccepted,
		kPresentFailed
	};

	struct TemporalTraceEntry
	{
		std::uint64_t sequence = 0;
		std::uint64_t realFrame = 0;
		std::uint64_t engineFrame = 0;
		std::uint32_t slot = 0;
		TemporalTraceEvent event = TemporalTraceEvent::kInputPacket;
		HRESULT result = S_OK;
	};

	struct TemporalPipelineStatus
	{
		temporal::RequestedTopology requested;
		temporal::EffectiveConfiguration effective;
		temporal::PendingRestart pending;
		temporal::SessionTopology session;
		temporal::DisplayState display;
		temporal::FailureDomain failureDomain = temporal::FailureDomain::kNone;
		TemporalCreationState creationState = TemporalCreationState::kUnregistered;
		bool requestFrozen = false;
		bool d3d11Ready = false;
		bool latencyHooksInstalled = false;
		bool latencySdkActive = false;
		temporal::LatencyPhase latencyPhase = temporal::LatencyPhase::kIdle;
		temporal::FramePhase framePhase = temporal::FramePhase::kIdle;
		std::uint64_t realFrame = 0;
		std::uint64_t engineFrame = 0;
		std::uint32_t frameSlot = 0;
		std::uint32_t presentAttempts = 0;
		std::uint64_t superResolutionResetRequested = 0;
		std::uint64_t superResolutionResetConsumed = 0;
		std::uint64_t frameGenerationResetRequested = 0;
		std::uint64_t frameGenerationResetConsumed = 0;
		std::uint64_t traceSequence = 0;
		std::uint32_t traceEntryCount = 0;
		std::string failure;
	};

	enum class FrameGenerationDebugResource : std::uint8_t
	{
		kHudless,
		kFinal,
		kDepth,
		kMotion
	};

	struct FrameGenerationDiagnostics
	{
		bool ready = false;
		bool active = false;
		bool inputsCaptured = false;
		bool hudlessPending = false;
		bool alphaConditioned = false;
		std::uint64_t conditionedCaptures = 0;
		std::uint64_t rawCaptures = 0;
		std::uint64_t dispatches = 0;
		std::uint64_t generatedFrames = 0;
		bool generatedFrameCountAvailable = false;
		std::uint64_t providerPresentedFrames = 0;
		bool providerPresentedFrameCountAvailable = false;
		bool fidelityFxProviderVersionAvailable = false;
		std::uint64_t fidelityFxProviderVersionId = 0;
		std::string fidelityFxProviderVersionName;
		std::uint32_t fidelityFxProviderVersionQueryResult = 0;
		std::uint64_t failures = 0;
		bool cameraValid = false;
		std::int64_t cameraFrameDelta = 0;
		double cameraFovDegrees = 0.0;
		bool cpuPhaseTimingsAvailable = false;
		std::array<CpuPhaseTimingStats,
			FrameGenerationCpuTimingCollector<>::kPhaseCount>
			cpuPhaseTimings{};
		bool frameTimeInputAvailable = false;
		double lastFrameTimeInputMilliseconds = 0.0;
		std::uint64_t inputRetirementAcquisitions = 0;
		std::uint64_t inputRetirementImmediateAcquisitions = 0;
		std::uint64_t inputRetirementGpuWaits = 0;
		std::uint64_t inputRetirementProviderDrains = 0;
		std::uint64_t inputRetirementGlobalDrainAttempts = 0;
		std::uint64_t inputRetirementGlobalDrainFailures = 0;
		std::uint64_t inputRetirementWaitFailures = 0;
		std::uint64_t inputRetirementSignals = 0;
		std::uint64_t inputRetirementSignalFailures = 0;
		std::uint64_t inputRetirementViolations = 0;
		std::uint64_t inputRetirementStartupDrains = 0;
		std::uint64_t inputRetirementDisableDrains = 0;
		std::uint64_t inputRetirementResizeDrains = 0;
		std::uint64_t inputRetirementTeardownDrains = 0;
		std::uint64_t inputRetirementSteadyDrains = 0;
		std::uint64_t inputRetirementLastRealFrame = 0;
		std::uint64_t inputRetirementLastResourceGeneration = 0;
		std::uint64_t inputRetirementLastRequiredFence = 0;
		std::uint64_t inputRetirementLastCompletedFence = 0;
		std::uint64_t inputRetirementWaitCpuMicroseconds = 0;
		std::uint32_t inputRetirementLastSlot = 0;
		bool inputRetirementLastAcquireQueuedGpuWait = false;
	};

	struct FrameGenerationDebugTexture
	{
		ID3D11ShaderResourceView* srv = nullptr;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	class TemporalPipeline
	{
	public:
		struct FrameGenerationCaptureResources
		{
			bool ready = false;
			struct Texture
			{
				ID3D11Texture2D* resource = nullptr;
				ID3D11ShaderResourceView* srv = nullptr;
				ID3D11UnorderedAccessView* uav = nullptr;
			};
			Texture motion;
			Texture depth;
			Texture hudlessColor;
			std::uint32_t width = 0;
			std::uint32_t height = 0;
			std::uint32_t frameSlot = 0;
		};

		static TemporalPipeline& Get();

		void RegisterCreationRouter();
		void FreezeRequest();
		void OnDataLoaded();
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device);
		void SubmitLiveConfiguration();
		void SetDetailedTracing(bool a_enabled) noexcept;
		[[nodiscard]] bool DetailedTracingEnabled() const noexcept;
		void BeginMainLoopFrame() noexcept;
		void BeginSimulation() noexcept;
		void EndSimulationAndBeginRenderSubmit() noexcept;
		void BeginPresentAttempt(UINT a_flags) noexcept;
		void EndPresentAttempt(UINT a_flags, HRESULT a_result) noexcept;
		[[nodiscard]] std::uint64_t CurrentRealFrame() const noexcept;
		void PostFailure(temporal::FailureDomain a_domain, std::string a_message) noexcept;
		void FailSuperResolutionToNative(std::string reason) noexcept;
		void AdvanceEngineResourceGeneration() noexcept;
		void AdvanceDisplayGeneration(std::uint32_t a_width, std::uint32_t a_height) noexcept;
		void RequestSuperResolutionReset() noexcept;
		void RequestFrameGenerationReset() noexcept;
		[[nodiscard]] bool SuperResolutionResetPending() const noexcept;
		[[nodiscard]] bool ArmFrameGenerationReset() noexcept;
		void ConsumeSuperResolutionReset(bool a_completed) noexcept;
		bool RecordInputPacket(
			std::uint64_t a_engineFrame,
			temporal::Extent a_renderExtent,
			temporal::Extent a_outputExtent,
			std::uint32_t a_slot,
			bool a_externalPublished) noexcept;
		bool PreparePresent(std::uint32_t a_slot, bool a_frameGenerationPrepared) noexcept;
		void RecordPresentAttempt(
			std::uint32_t a_slot,
			UINT a_flags,
			HRESULT a_result) noexcept;
		void RecordGeneratedFrames(std::optional<std::uint32_t> a_count) noexcept;
		void RecordPresentedFrames(std::optional<std::uint32_t> a_count) noexcept;
		[[nodiscard]] FrameGenerationCpuTimingCollector<>::Scope
			MeasureFrameGenerationCpuPhase(
				FrameGenerationCpuPhase a_phase) noexcept;
		void RecordFrameGenerationFrameTimeInput(
			float a_milliseconds) noexcept;

		[[nodiscard]] TemporalPipelineStatus GetStatus() const;
		TemporalRenderer& Renderer() noexcept;
		[[nodiscard]] temporal::EffectiveConfiguration GetEffectiveConfiguration() const;
		[[nodiscard]] bool IsFrameGenerationProxyActive() const noexcept;
		[[nodiscard]] bool IsFrameGenerationEnabledForFrame(bool a_inExcludedMenu) const noexcept;
		[[nodiscard]] bool AllowFrameGenerationInMenus() const noexcept;
		[[nodiscard]] FrameGenerationDiagnostics
			GetFrameGenerationDiagnostics() const noexcept;
		[[nodiscard]] FrameGenerationCaptureResources
			GetFrameGenerationCaptureResources() const noexcept;
		[[nodiscard]] bool AcquireFrameGenerationInputWrite() noexcept;
		void SetFrameGenerationInputsReady(bool a_ready) noexcept;
		void FailFrameGenerationFrame(const char* a_reason) noexcept;
		void ResetFrameGenerationCaptureDiagnostics() noexcept;
		void RecordFrameGenerationCapture(bool a_alphaConditioned) noexcept;
		void SetHudlessCapturePending(bool a_pending) noexcept;
		void RecordFrameGenerationDispatch() noexcept;
		void RecordFrameGenerationFailure() noexcept;
		[[nodiscard]] bool EvaluateD3D12DLSS(
			const features::SuperResolutionExecutionContext& a_context);
		[[nodiscard]] temporal::ProviderResult EvaluateD3D11SuperResolution(
			temporal::SuperResolutionMethod a_method,
			const temporal::SuperResolutionRequest& a_request);
		[[nodiscard]] temporal::SuperResolutionSizeResult
			QuerySuperResolutionRenderSize(
				temporal::SuperResolutionMethod a_method,
				const temporal::SuperResolutionSizeRequest& a_request);
		[[nodiscard]] bool IsSuperResolutionRuntimeReady(
			temporal::SuperResolutionMethod a_method) const noexcept;
		[[nodiscard]] bool UsesD3D12SuperResolution(
			temporal::SuperResolutionMethod a_method) const noexcept;
		[[nodiscard]] bool CreateFsrSuperResolutionResources(
			ID3D11Device* a_device,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight);
		void DestroySuperResolutionResources(
			temporal::SuperResolutionMethod a_method) noexcept;
		void ResetFsrFrameGenerationCamera() noexcept;
		void RequestFsrFrameGenerationReset() noexcept;
		[[nodiscard]] FrameGenerationDebugTexture GetFrameGenerationDebugTexture(
			FrameGenerationDebugResource a_resource) const noexcept;

	private:
		TemporalPipeline();
		~TemporalPipeline();
		TemporalPipeline(const TemporalPipeline&) = delete;
		TemporalPipeline& operator=(const TemporalPipeline&) = delete;

		void OnPreCreateDeviceAndSwapChain(
			DXGI_SWAP_CHAIN_DESC* a_desc,
			std::vector<D3D_FEATURE_LEVEL>& a_featureLevels);
		std::optional<HRESULT> OnReplacementCreateDeviceAndSwapChain(
			CreateDeviceAndSwapChainContext& a_context);
		void OnPostCreateDeviceAndSwapChain(
			IDXGIAdapter* a_adapter,
			ID3D11Device** a_device,
			IDXGISwapChain** a_swapChain);
		bool InstallLatencyHooks() noexcept;

		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
}
