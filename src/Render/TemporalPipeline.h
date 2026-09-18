#pragma once

#include "Render/FrameGenerationCpuTiming.h"
#include "Render/FrameGenerationOrchestration.h"
#include "Render/SwapChainHook.h"
#include "Render/TemporalPipelineState.h"
#include "Render/TemporalProvider.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct IDXGIAdapter;
struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D11UnorderedAccessView;

namespace cs::buffer
{
	class Texture2D;
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
		temporal::FrameGenerationCapabilities capabilities;
		std::uint64_t failures = 0;
		bool cameraValid = false;
		std::int64_t cameraFrameDelta = 0;
		double cameraFovDegrees = 0.0;
		FrameGenerationCpuTimingCollector<>::Snapshot cpuTiming;
		temporal::PresentInputRetirementDiagnostics inputRetirement;
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
		void PostFailure(temporal::FailureDomain a_domain,
			std::string a_message) noexcept;
		void FailSuperResolutionToNative(std::string reason) noexcept;
		void AdvanceEngineResourceGeneration() noexcept;
		void AdvanceDisplayGeneration(std::uint32_t a_width,
			std::uint32_t a_height) noexcept;
		void RequestSuperResolutionReset() noexcept;
		void RequestFrameGenerationReset() noexcept;
		[[nodiscard]] bool SuperResolutionResetPending() const noexcept;
		[[nodiscard]] bool FrameGenerationResetPending() const noexcept;
		[[nodiscard]] bool ArmFrameGenerationReset() noexcept;
		void ConsumeSuperResolutionReset(bool a_completed) noexcept;
		void FreezeFrameConstants(
			std::uint32_t a_slot,
			const temporal::FrameGenerationRequest& a_request) noexcept;
		[[nodiscard]] bool ApplyFrozenFrameConstants(
			temporal::SuperResolutionRequest& a_request) const noexcept;
		bool RecordInputPacket(std::uint64_t a_engineFrame,
			temporal::Extent a_renderExtent,
			temporal::Extent a_outputExtent,
			std::uint32_t a_slot) noexcept;
		bool PreparePresent(std::uint32_t a_slot) noexcept;
		void SetFrameGenerationPrepared(std::uint32_t a_slot,
			bool a_prepared) noexcept;
		void RecordPresentAttempt(std::uint32_t a_slot, UINT a_flags,
			HRESULT a_result) noexcept;
		void RecordGeneratedFrames(std::optional<std::uint32_t> a_count) noexcept;
		void RecordPresentedFrames(std::optional<std::uint32_t> a_count) noexcept;
		[[nodiscard]] FrameGenerationCpuTimingCollector<>::Scope
		MeasureFrameGenerationCpuPhase(FrameGenerationCpuPhase a_phase) noexcept;
		void RecordFrameGenerationFrameTimeInput(float a_milliseconds) noexcept;

		[[nodiscard]] TemporalPipelineStatus GetStatus() const;
		TemporalRenderer& Renderer() noexcept;
		[[nodiscard]] temporal::EffectiveConfiguration
		GetEffectiveConfiguration() const;
		[[nodiscard]] bool IsFrameGenerationProxyActive() const noexcept;
		[[nodiscard]] bool
		IsFrameGenerationEnabledForFrame(bool a_inExcludedMenu) const noexcept;
		[[nodiscard]] bool AllowFrameGenerationInMenus() const noexcept;
		[[nodiscard]] FrameGenerationDiagnostics
		GetFrameGenerationDiagnostics() const noexcept;
		[[nodiscard]] temporal::FidelityFXCapabilities
		GetFidelityFXCapabilities() const noexcept;
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
		[[nodiscard]] temporal::ProviderResult
		EvaluateSuperResolution(temporal::SuperResolutionMethod a_method,
			const temporal::SuperResolutionRequest& a_request);
		[[nodiscard]] temporal::SuperResolutionSizeResult
		QuerySuperResolutionRenderSize(
			temporal::SuperResolutionMethod a_method,
			const temporal::SuperResolutionSizeRequest& a_request);
		[[nodiscard]] bool IsSuperResolutionRuntimeReady(
			temporal::SuperResolutionMethod a_method) const noexcept;
		[[nodiscard]] std::unique_ptr<cs::buffer::Texture2D>
		CreateSuperResolutionTexture(
			const D3D11_TEXTURE2D_DESC& a_desc,
			std::string_view a_name);
		[[nodiscard]] temporal::ProviderResult DestroySuperResolutionResources(
			temporal::SuperResolutionMethod a_method) noexcept;
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
		void OnPostCreateDeviceAndSwapChain(IDXGIAdapter* a_adapter,
			ID3D11Device** a_device,
			IDXGISwapChain** a_swapChain);
		bool InstallLatencyHooks() noexcept;
		void ApplyPendingConfiguration();

		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
}  // namespace cs::render
