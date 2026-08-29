#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "Render/Engine.h"
#include "Render/PixelShaderResourceSnapshot.h"
#include "ScreenSpaceGIHistory.h"
#include "Utils/CSBuffer.h"

#include <DirectXMath.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
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
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

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

		struct Settings
		{
			bool  denoiseEnabled = true;
			float denoiseRadius = 2.0f;
			float aoRadius = 256.0f;
			float giRadius = 256.0f;
			float aoPower = 1.0f;
			float depthFadeStart = 40000.0f;
			float depthFadeEnd = 50000.0f;
			float bounceStrength = 1.0f;
			int   numSlices = 4;
			int   numSteps = 8;
			bool  enabled = true;
			bool  enableTemporalDenoiser = true;
			float depthDisocclusion = 0.1f;
			int   maxAccumFrames = 16;
		};

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
			float         CenterBeta;
			float         DepthDisocclusion;
			std::uint32_t MaxAccumFrames;
			std::uint32_t TemporalFlags;
			float         _temporalPad[3];
			float         RadianceScale[2];
			float         _bouncePad[2];
			float         PrevNDCToViewMul[2];
			float         PrevNDCToViewAdd[2];
			float         ViewToWorld[12];
			float         PrevViewToWorld[12];
		};
		static_assert(sizeof(XeGTAOCB) == 288);
		static_assert(offsetof(XeGTAOCB, PrevFrameDim) == 64);
		static_assert(offsetof(XeGTAOCB, FrameIndex) == 80);
		static_assert(offsetof(XeGTAOCB, DepthDisocclusion) == 136);
		static_assert(offsetof(XeGTAOCB, MaxAccumFrames) == 140);
		static_assert(offsetof(XeGTAOCB, TemporalFlags) == 144);
		static_assert(offsetof(XeGTAOCB, RadianceScale) == 160);
		static_assert(offsetof(XeGTAOCB, PrevNDCToViewMul) == 176);
		static_assert(offsetof(XeGTAOCB, ViewToWorld) == 192);
		static_assert(offsetof(XeGTAOCB, PrevViewToWorld) == 240);

		// Must match Shaders/XeGTAO/decode.cs.hlsl.
		struct alignas(16) DecodeCB
		{
			DirectX::XMFLOAT4X4 InvProj;  // Row-major and untransposed.
			float               RcpFrameDim[2];
			float               FrameDim[2];
		};
		static_assert(sizeof(DecodeCB) % 16 == 0);

		// Ni camera rows with the adjusted camera origin in w.
		struct CameraTransform
		{
			float rows[12]{};
			float ndcToViewMul[2]{};
			float ndcToViewAdd[2]{};
		};

		// Identity of the engine resources a dispatch consumed.
		struct InputIdentity
		{
			winrt::com_ptr<ID3D11Resource> depth;
			winrt::com_ptr<ID3D11Resource> normal;
			winrt::com_ptr<ID3D11Resource> motion;
			winrt::com_ptr<ID3D11Resource> sourceA;
			winrt::com_ptr<ID3D11Resource> sourceB;

			[[nodiscard]] bool operator==(const InputIdentity& a_rhs) const noexcept
			{
				return depth.get() == a_rhs.depth.get() &&
					normal.get() == a_rhs.normal.get() &&
					motion.get() == a_rhs.motion.get() &&
					sourceA.get() == a_rhs.sourceA.get() &&
					sourceB.get() == a_rhs.sourceB.get();
			}
		};

		ScreenSpaceGI() = default;

		void SaveSettings();
		void OnPostDeferredLights();
		void SaveCompositionBindings();
		void RestoreCompositionBindings();
		void BindComposition(ID3D11DeviceContext* a_context);
		bool IsGeneratorReady() const noexcept;
		bool IsTemporalReady() const noexcept;
		bool EnsureResources();
		void ClearOcclusionOutputs(ID3D11DeviceContext* a_context);
		void ClearBounceOutputs(ID3D11DeviceContext* a_context);
		void ClearTemporalHistory(ID3D11DeviceContext* a_context);
		void ResetHistory(ssgi::HistoryResetReason a_reason);
		FeatureDebugTexture GetOcclusionDebugTexture() const;

		// Contiguous plugin slots: occlusion, SH luma, CoCg, albedo.
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
		std::atomic_bool _aoDenoisedLastFrame{ false };
		std::atomic_bool _bounceProducedLastFrame{ false };
		std::atomic_bool _bounceDenoisedLastFrame{ false };
		std::atomic_bool _radianceAvailableLastFrame{ false };
		std::atomic_bool _albedoBoundLastFrame{ false };
		std::atomic_bool _historyValidLastFrame{ false };
		std::atomic_bool _motionAvailableLastFrame{ false };
		std::atomic_bool _tiledPredicateAvailable{ false };
		std::atomic_bool _tiledLightingActive{ false };
		std::atomic_bool _tiledBAvailable{ false };
		std::atomic_bool _debugPreviewEnabled{ false };
		std::atomic_bool _queuedHistoryReset{ false };
		std::atomic_uint32_t _compositionBindsLastFrame{ 0 };
		std::atomic_uint32_t _temporalDispatchesLastFrame{ 0 };
		std::atomic_uint32_t _radianceSourceCount{ 0 };
		std::atomic_uint32_t _repeatCallbacks{ 0 };
		std::atomic_uint32_t _historyResetCount{ 0 };
		std::atomic_uint32_t _lastResetReason{
			static_cast<std::uint32_t>(ssgi::HistoryResetReason::kFirstFrame)
		};

		// Render-thread state.
		bool _occlusionOutputsDirty = false;
		bool _bounceOutputsDirty = false;
		bool _lastEnabled = false;
		bool _lastTemporalEnabled = false;
		bool _lastCallbackFrameValid = false;
		bool _prevCameraValid = false;
		std::uint8_t _lastSourceMode = 0;
		std::uint32_t _lastCallbackFrame = 0;
		std::uint32_t _prevFrameW = 0;
		std::uint32_t _prevFrameH = 0;
		CameraTransform _prevCamera{};
		InputIdentity _lastInputs{};
		ssgi::HistoryState _history;
		cs::render::PixelShaderResourceSnapshot<kCompositionPSSlotCount>
			_compositionBindingSnapshot;

		std::unique_ptr<cs::buffer::Texture2D> _linearDepthTex;
		std::unique_ptr<cs::buffer::Texture2D> _workingDepthTex;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> _workingDepthMipUAVs;
		std::unique_ptr<cs::buffer::Texture2D> _viewNormalTex;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> _viewNormalMipUAVs;
		winrt::com_ptr<ID3D11ShaderResourceView> _viewNormalMip0SRV;
		std::unique_ptr<cs::buffer::Texture2D> _radianceTempTex;
		std::unique_ptr<cs::buffer::Texture2D> _radianceTex;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipCount> _radianceMipUAVs;
		std::unique_ptr<cs::buffer::Texture2D> _aoRawTex;
		std::unique_ptr<cs::buffer::Texture2D> _aoDenoisedTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceSHRawTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceCoCgRawTex;
		std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> _bounceSHTex;
		std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> _bounceCoCgTex;
		std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> _accumTex;
		std::array<std::unique_ptr<cs::buffer::Texture2D>, 2> _prevGeoTex;
		std::unique_ptr<cs::buffer::Texture2D> _accumBlurTex;
		winrt::com_ptr<ID3D11Texture2D> _noiseTex;
		winrt::com_ptr<ID3D11ShaderResourceView> _noiseSRV;
		winrt::com_ptr<ID3D11SamplerState> _pointClampSampler;
		std::unique_ptr<cs::buffer::ConstantBuffer> _xegtaoCB;
		std::unique_ptr<cs::buffer::ConstantBuffer> _decodeCB;
		winrt::com_ptr<ID3D11ComputeShader> _decodeCS;
		winrt::com_ptr<ID3D11ComputeShader> _prefilterCS;
		winrt::com_ptr<ID3D11ComputeShader> _prefilterRadianceCS;
		winrt::com_ptr<ID3D11ComputeShader> _prefilterNormalCS;
		winrt::com_ptr<ID3D11ComputeShader> _radianceDisoccCS;
		winrt::com_ptr<ID3D11ComputeShader> _aoCS;
		winrt::com_ptr<ID3D11ComputeShader> _bounceCS;
		winrt::com_ptr<ID3D11ComputeShader> _denoiseCS;
		winrt::com_ptr<ID3D11ComputeShader> _bounceDenoiseCS;
		std::uint32_t _allocW = 0;
		std::uint32_t _allocH = 0;
		std::uint32_t _generation = 0;
	};
}
