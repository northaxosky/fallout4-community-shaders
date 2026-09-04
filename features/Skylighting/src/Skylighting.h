#pragma once

#include "Feature.h"
#include "FeatureCategories.h"

#include <DirectXMath.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>

#include <winrt/base.h>

struct ID3D11DepthStencilView;
struct ID3D11Buffer;
struct ID3D11ComputeShader;
struct ID3D11Device;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D11UnorderedAccessView;

namespace cs::features
{
	class Skylighting : public Feature
	{
	public:
		struct Settings
		{
			bool enabled = true;
			bool forceSceneTraversal = true;
			float occlusionExtent = 4096.0f;
			float strength = 1.0f;
			float minDiffuseVisibility = 0.1f;
			float minSpecularVisibility = 0.1f;
		};

		struct OcclusionData
		{
			DirectX::XMFLOAT4X4 transform{};
			DirectX::XMFLOAT4 direction{};
			float extent = 4096.0f;
			bool valid = false;
		};

		static Skylighting* GetSingleton();

		std::string_view GetName() const override { return "Skylighting"; }
		std::string_view GetDisplayName() const override { return "Skylighting"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Darkens sky-occluded ambient lighting from a player-centred world depth map.";
		}
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;

		OcclusionData GetOcclusionData() const;

	private:
		struct BSLightingShaderProperty_GetOcclusionPasses;
		struct Precipitation_RenderOcclusionMap;

		enum class FailureState : std::uint8_t
		{
			kNone,
			kResourcesUnavailable,
			kSkyUnavailable,
			kPrecipitationUnavailable,
			kRendererUnavailable,
			kGlobalsUnavailable,
			kProjectionInvalid,
			kProducerException
		};

		enum class ResourceState : std::uint8_t
		{
			kNotAttempted,
			kWaitingForNativeTarget,
			kFailed,
			kReady
		};

		enum class ResourceWaitReason : std::uint8_t
		{
			kNone,
			kRendererData,
			kNativeTexture,
			kNativeDepthStencilView,
			kNativeTextureAndDepthStencilView
		};

		enum class ResourceFailureReason : std::uint8_t
		{
			kNone,
			kDeviceUnavailable,
			kUnexpectedExtent,
			kUnsupportedShaderResourceFormat,
			kTextureCreationFailed,
			kShaderResourceViewCreationFailed,
			kDepthStencilViewCreationFailed,
			kUnexpectedException
		};

		Skylighting() = default;

		void SaveSettings();
		void PublishSettings() noexcept;
		void PublishConsumerData() noexcept;
		void ResolveOcclusionGlobals() noexcept;
		bool EnsureResources() noexcept;
		bool EnsureNormalizedDebugResources(ID3D11Device* a_device) noexcept;
		bool DispatchNormalizedDebugView() noexcept;
		void RenderProducer() noexcept;
		FeatureDebugTexture GetOcclusionDebugTexture() const;
		FeatureDebugTexture GetNormalizedOcclusionDebugTexture() const;
		void ObserveRouteDiagnostics() const noexcept;
		void SetValidationDetail(std::string a_detail) const;
		std::string GetValidationDetail() const;
		static const char* FailureStateName(FailureState a_state) noexcept;
		static const char* ResourceStateName(ResourceState a_state) noexcept;
		static const char* ResourceWaitReasonName(
			ResourceWaitReason a_reason) noexcept;
		static const char* ResourceFailureReasonName(
			ResourceFailureReason a_reason) noexcept;

		std::atomic_bool _enabled{ true };
		std::atomic_bool _forceSceneTraversal{ true };
		std::atomic<float> _occlusionExtent{ 4096.0f };
		std::atomic<float> _effectiveExtent{ 4096.0f };
		std::atomic<float> _strength{ 1.0f };
		std::atomic<float> _minDiffuseVisibility{ 0.1f };
		std::atomic<float> _minSpecularVisibility{ 0.1f };
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::atomic_bool _consumerSamplerReady{ false };
		mutable std::atomic_bool _routeSubstitutionMismatch{ false };
		std::atomic_bool _visibilityDebugEnabled{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic<ResourceState> _resourceState{
			ResourceState::kNotAttempted
		};
		std::atomic<ResourceWaitReason> _resourceWaitReason{
			ResourceWaitReason::kNone
		};
		std::atomic<ResourceFailureReason> _resourceFailureReason{
			ResourceFailureReason::kNone
		};
		std::atomic_bool _nativeTextureAvailable{ false };
		std::atomic_bool _nativeSRVAvailable{ false };
		std::atomic_bool _nativeDSVAvailable{ false };
		std::atomic_flag _depthStencilTargetsReported = ATOMIC_FLAG_INIT;
		std::atomic<std::int32_t> _first512DepthStencilSlot{ -1 };
		std::atomic<std::int32_t> _precipitationOcclusionPlatformID{ -1 };
		std::atomic_bool _synthesizedSRVDescriptor{ false };
		std::atomic_bool _outerHookInstalled{ false };
		std::atomic_bool _vfuncHookInstalled{ false };
		std::atomic_bool _matrixGlobalResolved{ false };
		std::atomic_bool _extentGlobalResolved{ false };
		std::atomic_bool _directionGlobalsResolved{ false };
		std::atomic_bool _inOcclusionRender{ false };
		std::atomic_bool _producerRanThisFrame{ false };
		std::atomic_bool _projectionValid{ false };
		std::atomic_bool _debugPreviewEnabled{ false };
		std::atomic_bool _normalizedDebugPreviewEnabled{ false };
		std::atomic_bool _normalizedResourcesAttempted{ false };
		std::atomic_bool _normalizedResourcesAllocated{ false };
		std::atomic_bool _normalizedViewDispatchedLastFrame{ false };
		std::atomic_flag _emptyGeometryReported = ATOMIC_FLAG_INIT;
		std::atomic_bool _shadowSceneNodeGlobalResolved{ false };
		std::atomic_bool _shadowSceneNodePresent{ false };
		std::atomic_bool _preCulledObjectsPresent{ false };
		std::atomic_bool _sceneRootPresent{ false };
		std::atomic_int64_t _sceneRootChildCount{ -1 };
		std::atomic_bool _cullingProcessPresent{ false };
		std::atomic_bool _accumulatorPresent{ false };
		std::atomic_int64_t _accumulatorRenderMode{ -1 };
		std::atomic_bool _occlusionCameraPresent{ false };
		std::atomic<float> _occlusionFrustumNear{
			std::numeric_limits<float>::lowest()
		};
		std::atomic<float> _occlusionFrustumFar{
			std::numeric_limits<float>::lowest()
		};
		std::atomic<float> _occlusionFrustumLeft{
			std::numeric_limits<float>::lowest()
		};
		std::atomic<float> _occlusionFrustumRight{
			std::numeric_limits<float>::lowest()
		};
		std::atomic<float> _occlusionFrustumTop{
			std::numeric_limits<float>::lowest()
		};
		std::atomic<float> _occlusionFrustumBottom{
			std::numeric_limits<float>::lowest()
		};
		std::atomic_int64_t _occlusionFrustumOrthographic{ -1 };
		std::atomic_bool _inInteriorResolved{ false };
		std::atomic_bool _inInterior{ false };
		std::atomic_int64_t _currentWorldspaceFormID{ -1 };
		std::atomic_int64_t _currentCellFormID{ -1 };
		std::atomic_bool _preCulledQEnabledResolved{ false };
		std::atomic_bool _preCulledQEnabled{ false };
		std::atomic_bool _preCulledWantEnabledResolved{ false };
		std::atomic_bool _preCulledWantEnabled{ false };
		std::atomic_bool _preCulledDisplaySettingResolved{ false };
		std::atomic_bool _preCulledDisplaySetting{ false };
		std::atomic_bool _preCulledTempDisabledResolved{ false };
		std::atomic_bool _preCulledTempDisabled{ false };
		std::atomic_bool _sceneTraversalOverrideResolved{ false };
		std::atomic_bool _sceneTraversalOverrideAppliedLastRender{ false };
		std::atomic_uint64_t _renderCount{ 0 };
		std::atomic_uint64_t _vfuncCallsTotal{ 0 };
		std::atomic_uint64_t _workingVfuncCallsDuringRender{ 0 };
		std::atomic_uint64_t _lastVfuncCallsDuringRender{ 0 };
		std::atomic_int64_t _workingVfuncLastRenderModeDuringRender{ -1 };
		std::atomic_int64_t _vfuncLastRenderModeDuringRender{ -1 };
		std::atomic_uint64_t _workingGeometryCount{ 0 };
		std::atomic_uint64_t _workingPassCount{ 0 };
		std::atomic_uint64_t _lastGeometryCount{ 0 };
		std::atomic_uint64_t _lastPassCount{ 0 };
		std::atomic_uint64_t _resourceAttemptCount{ 0 };
		std::atomic<FailureState> _failureState{ FailureState::kNone };

		Settings _settings;

		std::atomic<ID3D11Device*> _device{ nullptr };
		DirectX::XMFLOAT4* _matrixGlobal = nullptr;
		float* _extentGlobal = nullptr;
		float* _directionXGlobal = nullptr;
		float* _directionYGlobal = nullptr;
		float* _directionZGlobal = nullptr;
		std::uint8_t* _preCulledTempDisabledGlobal = nullptr;

		winrt::com_ptr<ID3D11Texture2D> _occlusionTexture;
		winrt::com_ptr<ID3D11ShaderResourceView> _occlusionSRV;
		winrt::com_ptr<ID3D11DepthStencilView> _occlusionDSV;
		winrt::com_ptr<ID3D11SamplerState> _comparisonSampler;
		winrt::com_ptr<ID3D11Texture2D> _normalizedOcclusionTexture;
		winrt::com_ptr<ID3D11ShaderResourceView> _normalizedOcclusionSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> _normalizedOcclusionUAV;
		winrt::com_ptr<ID3D11Buffer> _normalizedRangeBuffer;
		winrt::com_ptr<ID3D11ShaderResourceView> _normalizedRangeSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> _normalizedRangeUAV;
		winrt::com_ptr<ID3D11ComputeShader> _normalizedReduceCS;
		winrt::com_ptr<ID3D11ComputeShader> _normalizedWriteCS;

		mutable std::mutex _occlusionDataMutex;
		OcclusionData _occlusionData;
		mutable std::mutex _validationMutex;
		mutable std::string _validationDetail;
	};
}
