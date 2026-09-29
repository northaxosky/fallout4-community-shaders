#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "Render/Engine.h"
#include "Render/FrameBuffer.h"
#include "Render/PixelShaderResourceSnapshot.h"
#include "ScreenSpaceGIHistory.h"
#include "ScreenSpaceGISettings.h"
#include "Utils/CSBuffer.h"

#include <DirectXMath.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <winrt/base.h>

namespace cs::features
{
	class ScreenSpaceGI :
		public Feature,
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static ScreenSpaceGI* GetSingleton();

		std::string_view GetName() const override { return "ScreenSpaceGI"; }
		std::string_view GetDisplayName() const override { return "Screen Space GI"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override { return "Screen-space ambient occlusion and indirect diffuse lighting."; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnDataLoaded() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;

		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent& a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		cs::ScreenSpaceGIFeatureData GetCommonBufferData();

		using Settings = ssgi_settings::Settings;

	private:
		// Must match Shaders/XeGTAO/common.hlsli.
		struct alignas(16) XeGTAOCB
		{
			float         NDCToViewMul[4];
			float         NDCToViewAdd[4];
			float         TexDim[2];
			float         RcpTexDim[2];
			float         FrameDim[2];
			float         RcpFrameDim[2];
			float         PrevFrameDim[2];
			float         RcpPrevFrameDim[2];
			std::uint32_t FrameIndex;
			std::uint32_t NumSlices;
			std::uint32_t NumSteps;
			float         MinScreenRadius;
			float         AORadius;
			float         EffectRadius;
			float         Thickness;
			float         GIRadius;
			float         DepthFadeRange[2];
			float         DepthFadeScaleConst;
			float         BlurRadius;
			float         DistanceNormalisation;
			float         NormalDisocclusion;
			float         DepthDisocclusion;
			std::uint32_t MaxAccumFrames;
			std::uint32_t TemporalFlags;
			float         GISaturation;
			float         GIDistanceCompensation;
			float         GICompensationMaxDist;
			float         _pad0[2];
			float         AOPower;
			float         GIStrength;
			float         PrevNDCToViewMul[2];
			float         PrevNDCToViewAdd[2];
			float         ViewToWorld[12];
			float         PrevViewToWorld[12];
			float         CameraOrigin[4];
			float         PrevCameraOrigin[4];
			float         FarReprojZ[4];
			float         FarReprojW[4];
			float         NearReprojZ[4];
			float         NearReprojW[4];
		};
		static_assert(sizeof(XeGTAOCB) == 384);
		static_assert(offsetof(XeGTAOCB, PrevFrameDim) == 64);
		static_assert(offsetof(XeGTAOCB, FrameIndex) == 80);
		static_assert(offsetof(XeGTAOCB, DepthDisocclusion) == 136);
		static_assert(offsetof(XeGTAOCB, MaxAccumFrames) == 140);
		static_assert(offsetof(XeGTAOCB, TemporalFlags) == 144);
		static_assert(offsetof(XeGTAOCB, AOPower) == 168);
		static_assert(offsetof(XeGTAOCB, PrevNDCToViewMul) == 176);
		static_assert(offsetof(XeGTAOCB, ViewToWorld) == 192);
		static_assert(offsetof(XeGTAOCB, PrevViewToWorld) == 240);
		static_assert(offsetof(XeGTAOCB, CameraOrigin) == 288);
		static_assert(offsetof(XeGTAOCB, PrevCameraOrigin) == 304);
		static_assert(offsetof(XeGTAOCB, FarReprojZ) == 320);

		// Variants compiled for one resolution mode; pairs index the temporal denoiser.
		struct ResolutionShaders
		{
			winrt::com_ptr<ID3D11ComputeShader> prefilterDepth;
			winrt::com_ptr<ID3D11ComputeShader> prefilterRadiance;
			winrt::com_ptr<ID3D11ComputeShader> prefilterNormal;
			winrt::com_ptr<ID3D11ComputeShader> ao;
			std::array<winrt::com_ptr<ID3D11ComputeShader>, 2> radianceDisocc;
			std::array<winrt::com_ptr<ID3D11ComputeShader>, 2> gi;
			std::array<winrt::com_ptr<ID3D11ComputeShader>, 2> blur;
			winrt::com_ptr<ID3D11ComputeShader> upsample;
		};
		static constexpr std::size_t kResolutionModes = 3;

		// Rotation rows and projection terms retained for the next temporal frame.
		struct CameraTransform
		{
			float rows[12]{};
			float ndcToViewMul[2]{};
			float ndcToViewAdd[2]{};
		};

		ScreenSpaceGI() = default;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(ssgi_settings::kSchema); }
		void OnPostDeferredLights();
		void ApplyVanillaSSAO();
		void SaveCompositionBindings();
		void RestoreCompositionBindings();
		void BindComposition(ID3D11DeviceContext* a_context);
		bool IsGeneratorReady() const noexcept;
		bool IsTemporalReady() const noexcept;
		const ResolutionShaders& ActiveShaders() const noexcept;
		bool EnsureResources();
		void ClearOcclusionOutputs(ID3D11DeviceContext* a_context);
		void ClearBounceOutputs(ID3D11DeviceContext* a_context);
		void ClearTemporalHistory(ID3D11DeviceContext* a_context);
		void ResetHistory(ssgi::HistoryResetReason a_reason);
		FeatureDebugTexture GetOcclusionDebugTexture() const;

		// Slots: occlusion, SH luma, CoCg, g-buffer normal.
		static constexpr std::uint32_t kCompositionPSSlot = 26;
		static constexpr std::uint32_t kCompositionPSSlotCount = 4;
		static constexpr auto kRadianceSourceA = cs::engine::RenderTarget::kDiffuseBufferA;
		static constexpr auto kRadianceSourceB = cs::engine::RenderTarget::kDiffuseBufferB;
		// Full-resolution R16G16_FLOAT motion written by the deferred prepass.
		static constexpr auto kMotionSource = cs::engine::RenderTarget::kMotionVectors;
		static constexpr std::uint32_t kMipCount = 5;

		// Bounded ambient-IBL fold-in already carried by the direct radiance source.
		static constexpr std::int64_t kContaminatedLightClasses = 16;
		static constexpr std::int64_t kContaminatedRoutes = 24;

		Settings _settings;
		std::atomic_bool _started{ false };
		std::atomic_bool _injectionRegistered{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _resourceInitFailed{ false };
		std::atomic_bool _aoProducedLastFrame{ false };
		std::atomic_bool _bounceProducedLastFrame{ false };
		std::atomic_bool _bounceDenoisedLastFrame{ false };
		std::atomic_bool _radianceAvailableLastFrame{ false };
		std::atomic_bool _normalBoundLastFrame{ false };
		std::atomic_bool _historyValidLastFrame{ false };
		std::atomic_bool _motionAvailableLastFrame{ false };
		std::atomic_bool _tiledLightingActive{ false };
		std::atomic_bool _tiledBAvailable{ false };
		std::atomic_bool _debugPreviewEnabled{ false };
		std::atomic_bool _queuedHistoryReset{ false };
		std::atomic_bool _vanillaSSAOAppliedLastFrame{ false };
		std::atomic_uint32_t _compositionBindsLastFrame{ 0 };
		std::atomic_uint32_t _temporalDispatchesLastFrame{ 0 };
		std::atomic_uint32_t _radianceSourceCount{ 0 };
		std::atomic_uint32_t _repeatCallbacks{ 0 };
		std::atomic_bool _cameraReadyLastFrame{ false };
		std::atomic<float> _cameraOriginXLastFrame{ 0.0f };
		std::atomic<float> _cameraOriginYLastFrame{ 0.0f };
		std::atomic<float> _cameraOriginZLastFrame{ 0.0f };
		std::atomic<float> _cameraPreviousOriginXLastFrame{ 0.0f };
		std::atomic<float> _cameraPreviousOriginYLastFrame{ 0.0f };
		std::atomic<float> _cameraPreviousOriginZLastFrame{ 0.0f };
		std::atomic_uint32_t _historyResetCount{ 0 };
		std::atomic_uint32_t _lastResetReason{
			static_cast<std::uint32_t>(ssgi::HistoryResetReason::kFirstFrame)
		};

		// Render-thread state.
		bool _occlusionOutputsDirty = false;
		bool _bounceOutputsDirty = false;
		bool _lastTemporalEnabled = false;
		int _lastResolutionMode = 0;
		bool _upsampledLastFrame = false;
		// The engine's startup bSAOEnable snapshot, restored when vanilla SSAO is re-enabled.
		std::optional<bool> _vanillaSSAOSnapshot;
		bool _lastCallbackFrameValid = false;
		std::uint32_t _lastCallbackFrame = 0;
		std::uint32_t _prevFrameW = 0;
		std::uint32_t _prevFrameH = 0;
		CameraTransform _prevCamera{};
		ssgi::HistoryState _history;
		cs::render::PixelShaderResourceSnapshot<kCompositionPSSlotCount>
			_compositionBindingSnapshot;

		std::unique_ptr<cs::buffer::Texture2D> _workingDepthTex;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> _workingDepthMipUAVs;
		std::unique_ptr<cs::buffer::Texture2D> _normalTex;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> _normalMipUAVs;
		std::unique_ptr<cs::buffer::Texture2D> _radianceTempTex;
		std::unique_ptr<cs::buffer::Texture2D> _radianceTex;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> _radianceMipUAVs;
		std::unique_ptr<cs::buffer::Texture2D> _aoTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceSHRawTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceCoCgRawTex;
		std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> _bounceSHTex;
		std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> _bounceCoCgTex;
		std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> _accumTex;
		std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> _prevGeoTex;
		std::unique_ptr<cs::buffer::Texture2D> _accumBlurTex;
		// Full-resolution upsample targets; history stays at the internal resolution.
		std::unique_ptr<cs::buffer::Texture2D> _aoUpsampledTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceSHUpsampledTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceCoCgUpsampledTex;
		winrt::com_ptr<ID3D11Texture2D> _noiseTex;
		winrt::com_ptr<ID3D11ShaderResourceView> _noiseSRV;
		winrt::com_ptr<ID3D11SamplerState> _pointClampSampler;
		winrt::com_ptr<ID3D11SamplerState> _linearClampSampler;
		std::unique_ptr<cs::buffer::ConstantBuffer> _xegtaoCB;
		std::array<ResolutionShaders, kResolutionModes> _shaders;
		std::uint32_t _allocW = 0;
		std::uint32_t _allocH = 0;
		std::uint32_t _generation = 0;
	};
}
