#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_fsr.h>
#include <sl_fsr_g.h>
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

#include "Render/FrameGenerationOrchestration.h"

namespace cs::features
{
	class Streamline
	{
	public:
		static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\Streamline";

		Streamline() = default;

		bool initialized = false;
		bool triedInitialization = false;
		bool featureDLSS = false;
		bool featureDLSSG = false;
		bool featureFSR = false;
		bool featureFSRG = false;
		bool featurePCL = false;
		bool featureReflex = false;
		bool deviceRegistered = false;

		sl::ViewportHandle viewport{ 0 };
		HMODULE interposer = nullptr;

		PFun_slInit* slInit{};
		PFun_slIsFeatureSupported* slIsFeatureSupported{};
		PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
		PFun_slSetFeatureLoaded* slSetFeatureLoaded{};
		PFun_slEvaluateFeature* slEvaluateFeature{};
		PFun_slFreeResources* slFreeResources{};
		PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
		PFun_slUpgradeInterface* slUpgradeInterface{};
		PFun_slSetConstants* slSetConstants{};
		PFun_slSetTagForFrame* slSetTagForFrame{};
		PFun_slGetFeatureFunction* slGetFeatureFunction{};
		PFun_slGetNewFrameToken* slGetNewFrameToken{};
		PFun_slSetD3DDevice* slSetD3DDevice{};
		PFun_slPCLSetMarker* slPCLSetMarker{};
		PFun_slReflexSleep* slReflexSleep{};
		PFun_slReflexSetOptions* slReflexSetOptions{};

		PFun_slDLSSSetOptions* slDLSSSetOptions{};
		PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
		PFun_slDLSSGSetOptions* slDLSSGSetOptions{};
		PFun_slDLSSGGetState* slDLSSGGetState{};
		PFun_slFSRSetOptions* slFSRSetOptions{};
		PFun_slFSRGetOptimalSettings* slFSRGetOptimalSettings{};
		PFun_slFSRGetState* slFSRGetState{};
		PFun_slFSRGetCapabilities* slFSRGetCapabilities{};
		PFun_slFSRGSetOptions* slFSRGSetOptions{};
		PFun_slFSRGGetState* slFSRGGetState{};
		PFun_slFSRGGetCapabilities* slFSRGGetCapabilities{};
		PFun_slFSRGQuiesce* slFSRGQuiesce{};

		sl::FrameToken* frameToken = nullptr;

		void LoadInterposer(
			std::uint32_t a_logLevel,
			bool a_loadDlss,
			bool a_loadFsr,
			bool a_loadDlssG,
			bool a_loadFsrG);

		bool PrepareD3D12Device(ID3D12Device** a_device);
		bool PrepareDXGIFactory(IDXGIFactory4** a_factory);
		bool SetDevice(ID3D12Device* a_device);
		void NotifyD3D12DeviceChange() noexcept;

		void CheckFeatures(IDXGIAdapter* a_adapter);
		void PostDevice();
		bool EnsureFrameToken(std::uint32_t a_frameIndex);
		bool CheckFrameConstants(
			sl::ViewportHandle p_viewport,
			std::uint32_t a_frameIndex,
			float a_jitterX,
			float a_jitterY,
			bool a_resetHistory,
			const render::temporal::FrameGenerationCamera& a_camera);
		bool SetDLSSOptions(
			sl::ViewportHandle p_viewport,
			const render::temporal::SuperResolutionRequest& a_request);
		[[nodiscard]] render::temporal::SuperResolutionSizeResult
			QueryDLSSRenderSize(
				const render::temporal::SuperResolutionSizeRequest& a_request);
		bool SetFSROptions(
			sl::ViewportHandle p_viewport,
			const render::temporal::SuperResolutionRequest& a_request,
			sl::FSRAlgorithm a_algorithm);
		[[nodiscard]] render::temporal::SuperResolutionSizeResult
			QueryFSRRenderSize(
				const render::temporal::SuperResolutionSizeRequest& a_request,
				sl::FSRAlgorithm a_algorithm);

		[[nodiscard]] render::temporal::ProviderResult UpscaleD3D12(
			const render::temporal::SuperResolutionRequest& a_request);
		[[nodiscard]] render::temporal::ProviderResult UpscaleFSRD3D12(
			const render::temporal::SuperResolutionRequest& a_request,
			sl::FSRAlgorithm a_algorithm);
		[[nodiscard]] render::temporal::ProviderResult
			ValidateFSRAlgorithm(sl::FSRAlgorithm a_algorithm) const;
		[[nodiscard]] render::temporal::ProviderResult
			ValidateFSRGAlgorithm(sl::FSRGAlgorithm a_algorithm) const;
		[[nodiscard]] render::temporal::FidelityFXCapabilities
			GetFidelityFXCapabilities() const noexcept;
		bool Sleep(std::uint32_t a_frameIndex);
		bool SetLatencyMarker(
			sl::PCLMarker a_marker,
			std::uint32_t a_frameIndex);
		bool ConfigureDLSSG(
			bool a_enabled,
			const render::temporal::FrameGenerationConfiguration&
				a_configuration,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight,
			std::uint32_t a_backBufferCount,
			bool a_retainResources);
		bool TagDLSSGFrame(
			const render::temporal::FrameGenerationRequest& a_request);
		bool ClearFrameGenerationTags(
			std::uint32_t a_frameIndex,
			ID3D12GraphicsCommandList* a_commandList = nullptr) noexcept;
		bool ClearCurrentFrameGenerationTags() noexcept;
		bool PollDLSSGState() noexcept;
		void NotifyDLSSGDisplayChange(
			std::uint32_t a_width,
			std::uint32_t a_height) noexcept;
		void ObserveDLSSGPresent(std::uint32_t a_syncInterval) noexcept;
		[[nodiscard]] render::temporal::ProviderResult
			ValidateDLSSGConfiguration(
				const render::temporal::FrameGenerationConfiguration&
					a_configuration) const;
		[[nodiscard]] render::temporal::FrameGenerationCapabilities
			GetDLSSGCapabilities() const noexcept;
		[[nodiscard]] bool LastDLSSGStateSucceeded() const noexcept
		{
			return _dlssGLastStateQuerySucceeded;
		}
		[[nodiscard]] std::uint32_t
			ConsumeDLSSGGeneratedFrameCount() noexcept;
		[[nodiscard]] std::uint32_t
			ConsumeDLSSGPresentedFrameCount() noexcept;
		[[nodiscard]] std::optional<render::temporal::GpuCompletionDependency>
			ConsumeDLSSGInputCompletionDependency() noexcept;
		[[nodiscard]] render::temporal::ProviderResult
			DestroyDLSSGResources() noexcept;
		[[nodiscard]] render::temporal::ProviderResult
			SetDLSSGPresentationActive(bool a_active) noexcept;
		bool ConfigureFSRG(
			bool a_enabled,
			const render::temporal::FrameGenerationRequest& a_request,
			sl::FSRGAlgorithm a_algorithm);
		bool TagFSRGFrame(
			const render::temporal::FrameGenerationRequest& a_request);
		bool CheckFSRGCompletionCapability(
			sl::FSRGAlgorithm a_algorithm) noexcept;
		bool PollFSRGState(
			sl::FSRGAlgorithm a_algorithm) noexcept;
		[[nodiscard]] std::optional<render::temporal::GpuCompletionDependency>
			ConsumeFSRGInputCompletionDependency() noexcept;
		[[nodiscard]] render::temporal::ProviderResult
			DestroyFSRGResources() noexcept;
		[[nodiscard]] render::temporal::ProviderResult
			SetFSRGPresentationActive(bool a_active) noexcept;
		[[nodiscard]] bool HasDLSSGResources() const noexcept
		{
			return _dlssGResourcesConfigured;
		}
		[[nodiscard]] render::temporal::ProviderResult
			DestroyDLSSResources() noexcept;
		[[nodiscard]] bool HasDLSSResources() const noexcept
		{
			return _dlssResourcesConfigured;
		}
		[[nodiscard]] render::temporal::ProviderResult
			DestroyFSRResources(
				sl::FSRAlgorithm a_algorithm =
					sl::FSRAlgorithm::eFSR3) noexcept;
		[[nodiscard]] bool HasFSRResources() const noexcept
		{
			return _fsrResourcesConfigured;
		}
	private:
		[[nodiscard]] render::temporal::ProviderResult
			UpscaleD3D12Feature(
				const render::temporal::SuperResolutionRequest& a_request,
				std::optional<sl::FSRAlgorithm> a_fsrAlgorithm);
		bool TagFrameGenerationFrame(
			const render::temporal::FrameGenerationRequest& a_request,
			sl::Feature a_feature,
			bool a_evaluate);
		[[nodiscard]] render::temporal::ProviderResult
			SetPresentationFeatureActive(
				sl::Feature a_feature,
				bool a_active,
				const char* a_name) noexcept;
		[[nodiscard]] sl::Result ClearFrameGenerationTagsChecked(
			std::uint32_t a_frameIndex,
			ID3D12GraphicsCommandList* a_commandList = nullptr) noexcept;
		[[nodiscard]] sl::Result
			ClearCurrentFrameGenerationTagsChecked() noexcept;
		bool PollFSRGState(
			sl::FSRGAlgorithm a_algorithm,
			bool a_requireSubmittedDependency) noexcept;
		std::uint32_t _lastFrameToken = UINT32_MAX;
		std::optional<std::uint32_t> _constantsFrame;
		std::uint32_t _constantsViewport = 0;
		bool _constantsReset = false;
		float _constantsJitterX = 0.0f;
		float _constantsJitterY = 0.0f;
		render::temporal::FrameGenerationCamera _constantsCamera{};
		bool _latencyFeaturesRequested = false;
		render::temporal::PresentedFrameAccumulator _dlssGPresentedFrames;
		render::temporal::PresentedFrameAccumulator _dlssGGeneratedFrames;
		std::optional<render::temporal::GpuCompletionDependency>
			_dlssGInputCompletionDependency;
		std::optional<render::temporal::GpuCompletionDependency>
			_fsrGInputCompletionDependency;
		sl::DLSSGStatus _dlssGStatus = sl::DLSSGStatus::eOk;
		bool _dlssGLastStateQuerySucceeded = false;
		std::atomic<render::temporal::CapabilityAvailability>
			_dlssGAvailability{
				render::temporal::CapabilityAvailability::kUnknown
			};
		std::atomic_bool _dlssGConfigurationKnown{ false };
		std::atomic_bool _dlssGConfigurationQueryFailed{ false };
		std::atomic_uint32_t _dlssGMaxGeneratedFrames{ 0 };
		std::atomic_bool _dlssGDynamicSupported{ false };
		std::atomic_bool _dlssGVsyncSupportAvailable{ false };
		std::atomic_bool _dlssGVsyncEnabled{ false };
		std::atomic_bool _dlssGGenerationEnabled{ false };
		std::atomic_bool _dlssGHardwareSchedulingRequired{ false };
		std::atomic_uint32_t _dlssGDetectedDriverMajor{ 0 };
		std::atomic_uint32_t _dlssGDetectedDriverMinor{ 0 };
		std::atomic_uint32_t _dlssGDetectedDriverBuild{ 0 };
		std::atomic_uint32_t _dlssGRequiredDriverMajor{ 0 };
		std::atomic_uint32_t _dlssGRequiredDriverMinor{ 0 };
		std::atomic_uint32_t _dlssGRequiredDriverBuild{ 0 };
		std::atomic_uint64_t _dlssGDeviceGeneration{ 0 };
		std::atomic_uint64_t _dlssGDisplayGeneration{ 0 };
		std::atomic_uint64_t _dlssGSampledDeviceGeneration{ 0 };
		std::atomic_uint64_t _dlssGSampledDisplayGeneration{ 0 };
		std::atomic_uint32_t _dlssGProviderStatus{ 0 };
		std::uint32_t _dlssGDisplayWidth = 0;
		std::uint32_t _dlssGDisplayHeight = 0;
		bool _dlssResourcesConfigured = false;
		bool _dlssGResourcesConfigured = false;
		bool _fsrResourcesConfigured = false;
		bool _fsrFeatureRequested = false;
		bool _fsrGResourcesConfigured = false;
		bool _fsrGFeatureRequested = false;
		std::atomic<render::temporal::CapabilityAvailability>
			_fsr4SrAvailability{
				render::temporal::CapabilityAvailability::kUnknown
			};
		std::atomic_uint32_t _fsr4SrUnavailableReason{ 0 };
		std::atomic_uint32_t _fsr4SrVersionMajor{ 0 };
		std::atomic_uint32_t _fsr4SrVersionMinor{ 0 };
		std::atomic_uint32_t _fsr4SrVersionPatch{ 0 };
		struct FidelityFXRuntimeDiagnosticsCache
		{
			std::atomic_bool windows11OrGreater{ false };
			std::atomic_uint32_t shaderModelMajor{ 0 };
			std::atomic_uint32_t shaderModelMinor{ 0 };
			std::atomic_uint32_t d3d12RuntimeSource{ 0 };
			std::atomic_uint32_t d3d12CoreVersionMajor{ 0 };
			std::atomic_uint32_t d3d12CoreVersionMinor{ 0 };
			std::atomic_uint32_t d3d12CoreVersionPatch{ 0 };
			std::atomic_uint32_t d3d12CoreVersionRevision{ 0 };
			std::atomic_uint32_t requestedD3D12SDKVersion{ 0 };

			template <class Capability>
			void Store(const Capability& a_capability) noexcept
			{
				windows11OrGreater.store(
					a_capability.windows11OrGreater ==
						sl::Boolean::eTrue,
					std::memory_order_relaxed);
				shaderModelMajor.store(
					a_capability.shaderModelMajor,
					std::memory_order_relaxed);
				shaderModelMinor.store(
					a_capability.shaderModelMinor,
					std::memory_order_relaxed);
				d3d12RuntimeSource.store(
					static_cast<std::uint32_t>(
						a_capability.d3d12RuntimeSource),
					std::memory_order_relaxed);
				d3d12CoreVersionMajor.store(
					a_capability.d3d12CoreVersionMajor,
					std::memory_order_relaxed);
				d3d12CoreVersionMinor.store(
					a_capability.d3d12CoreVersionMinor,
					std::memory_order_relaxed);
				d3d12CoreVersionPatch.store(
					a_capability.d3d12CoreVersionPatch,
					std::memory_order_relaxed);
				d3d12CoreVersionRevision.store(
					a_capability.d3d12CoreVersionRevision,
					std::memory_order_relaxed);
				requestedD3D12SDKVersion.store(
					a_capability.requestedD3D12SDKVersion,
					std::memory_order_relaxed);
			}

			void Reset() noexcept
			{
				windows11OrGreater.store(
					false, std::memory_order_relaxed);
				shaderModelMajor.store(
					0, std::memory_order_relaxed);
				shaderModelMinor.store(
					0, std::memory_order_relaxed);
				d3d12RuntimeSource.store(
					0, std::memory_order_relaxed);
				d3d12CoreVersionMajor.store(
					0, std::memory_order_relaxed);
				d3d12CoreVersionMinor.store(
					0, std::memory_order_relaxed);
				d3d12CoreVersionPatch.store(
					0, std::memory_order_relaxed);
				d3d12CoreVersionRevision.store(
					0, std::memory_order_relaxed);
				requestedD3D12SDKVersion.store(
					0, std::memory_order_relaxed);
			}

			void Load(
				render::temporal::FidelityFXAlgorithmCapability&
					a_capability) const noexcept
			{
				a_capability.windows11OrGreater =
					windows11OrGreater.load(
						std::memory_order_relaxed);
				a_capability.shaderModelMajor =
					shaderModelMajor.load(
						std::memory_order_relaxed);
				a_capability.shaderModelMinor =
					shaderModelMinor.load(
						std::memory_order_relaxed);
				a_capability.d3d12RuntimeSource =
					d3d12RuntimeSource.load(
						std::memory_order_relaxed);
				a_capability.d3d12CoreVersionMajor =
					d3d12CoreVersionMajor.load(
						std::memory_order_relaxed);
				a_capability.d3d12CoreVersionMinor =
					d3d12CoreVersionMinor.load(
						std::memory_order_relaxed);
				a_capability.d3d12CoreVersionPatch =
					d3d12CoreVersionPatch.load(
						std::memory_order_relaxed);
				a_capability.d3d12CoreVersionRevision =
					d3d12CoreVersionRevision.load(
						std::memory_order_relaxed);
				a_capability.requestedD3D12SDKVersion =
					requestedD3D12SDKVersion.load(
						std::memory_order_relaxed);
			}
		};
		FidelityFXRuntimeDiagnosticsCache _fsr4SrRuntimeDiagnostics;
		std::atomic<render::temporal::CapabilityAvailability>
			_fsr4FgAvailability{
				render::temporal::CapabilityAvailability::kUnknown
			};
		std::atomic_uint32_t _fsr4FgUnavailableReason{ 0 };
		std::atomic_uint32_t _fsr4FgVersionMajor{ 0 };
		std::atomic_uint32_t _fsr4FgVersionMinor{ 0 };
		std::atomic_uint32_t _fsr4FgVersionPatch{ 0 };
		std::atomic_uint32_t _fsr4SwapchainVersionMajor{ 0 };
		std::atomic_uint32_t _fsr4SwapchainVersionMinor{ 0 };
		std::atomic_uint32_t _fsr4SwapchainVersionPatch{ 0 };
		FidelityFXRuntimeDiagnosticsCache _fsr4FgRuntimeDiagnostics;
	};
}
