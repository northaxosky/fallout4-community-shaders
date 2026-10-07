#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "ShaderDefines.h"
#include "ShadowLightData.h"
#include "SkylightingSettings.h"
#include "Utils/CSBuffer.h"

#include <DirectXMath.h>
#include <d3d11.h>

#include <array>
#include <atomic>
#include <cfloat>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <winrt/base.h>

namespace cs::features
{
	class Skylighting : public ShaderFeature<skylighting_shader::kShaderDefines>
	{
	public:
		using Settings = skylighting::Settings;

		// PS slot of the probe array for forward consumers; compute consumers bind it at t50 too.
		static constexpr std::uint32_t kProbeArraySlot = 50;

		static Skylighting* GetSingleton();

		std::string_view GetName() const override { return "Skylighting"; }
		std::string_view GetDisplayName() const override { return "Skylighting"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Simulates realistic ambient lighting by calculating sky occlusion and directional lighting, providing more accurate and natural illumination in outdoor environments.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void OnLoadingMenuClosed() override;
		void Prepass() override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;
		FullscreenDebugData GetFullscreenDebugData() const noexcept override;

		// Packs b6; repeated packs within a frame must not advance the grid again.
		render::SkylightingSettings GetCommonBufferData();

		// Null unless the feature is healthy and its probes were created, so consumers fail neutral.
		ID3D11ShaderResourceView* GetProbeArraySRV() const noexcept;

	private:
		Skylighting() = default;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(skylighting::kSchema); }

		// Captures the occlusion map from a random zenith direction after the stock pass.
		void RenderOcclusion();
		enum class CaptureState : std::uint8_t
		{
			kCaptured,
			kInterior,
			kDisabled,
			kTargets,
			kFailed
		};
		CaptureState CaptureFrame();
		void CreateOcclusionResources(ID3D11Device* a_device);
		void CreateProbeResources(ID3D11Device* a_device);
		void CreateDebugResources();
		FeatureDebugTexture GetOcclusionDebugTexture() const;
		void LogCaptureSummary(CaptureState a_state);

		// Reasons accumulate until the next exterior capture consumes them.
		enum ResetReason : std::uint8_t
		{
			kResetLoad = 1,
			kResetRebuild = 2
		};
		void QueueReset(ResetReason a_reason) noexcept { _queuedReset.fetch_or(a_reason, std::memory_order_acq_rel); }
		void ResetSkylighting(ID3D11DeviceContext* a_context);

		enum class ProbeState : std::uint8_t
		{
			kPending,
			kDispatched,
			kNotFullSky,
			kNoOcclusion,
			kNoGrid
		};
		void DispatchProbeUpdate(ID3D11DeviceContext* a_context);
		void RenderDebug(ID3D11DeviceContext* a_context);
		void BindWaterProbes(ID3D11DeviceContext* a_context);

		enum class DebugVisualization : std::uint32_t
		{
			kOff,
			kDiffuse,
			kUp
		};

		Settings _settings;

		winrt::com_ptr<ID3D11Texture2D> _occlusionTexture;
		winrt::com_ptr<ID3D11DepthStencilView> _occlusionDSV;
		winrt::com_ptr<ID3D11ShaderResourceView> _occlusionSRV;
		std::atomic_bool _debugPreviewEnabled{ false };
		std::atomic<DebugVisualization> _debugVisualization{ DebugVisualization::kOff };

		static constexpr std::array<std::uint32_t, 3> kProbeArrayDims{ 256, 256, 128 };
		std::unique_ptr<cs::buffer::Texture3D> _texProbeArray;
		std::unique_ptr<cs::buffer::Texture3D> _texAccumFramesArray;
		std::unique_ptr<cs::buffer::Texture3D> _texShadowBitmask;
		std::unique_ptr<cs::buffer::Texture3D> _texShadowVisibility;
		winrt::com_ptr<ID3D11SamplerState> _comparisonSampler;
		winrt::com_ptr<ID3D11ComputeShader> _probeUpdateCompute;
		// Zeroed until cascades exist; zero split distances leave every probe lit.
		winrt::com_ptr<ID3D11Buffer> _shadowLightsBuffer;
		winrt::com_ptr<ID3D11ShaderResourceView> _shadowLightsSRV;
		std::atomic_bool _probesReady{ false };

		std::array<winrt::com_ptr<ID3D11ComputeShader>, 2> _debugCompute;
		std::unique_ptr<cs::buffer::Texture2D> _debugTexture;
		bool _debugFrameReady = false;

		std::atomic<std::uint8_t> _queuedReset{ 0 };
		bool _summaryPending = false;

		// misc parameters
		float occlusionDistance = 10000.f;
		// Slack below the probe grid for eye movement between the grid update and the occlusion render.
		static constexpr float OCCLUSION_BELOW_GRID_MARGIN = 512.f;
		static constexpr float MIN_OCCLUDER_RADIUS = 32.0f;

		// cached variables
		// World height of the probe grid's bottom layer, from the snapped grid origin.
		float probeGridBottomZ = -FLT_MAX;
		DirectX::XMFLOAT4X4 OcclusionTransform{};
		DirectX::XMFLOAT4 OcclusionDir{};
		std::uint32_t frameCount = 0;
		// The probe update needs a published matrix; the first capture can fail at load.
		bool _hasOcclusion = false;

		// Render thread only; the frame stamp makes repeated packs reuse one advance.
		struct GridState
		{
			std::uint32_t frame = UINT32_MAX;
			DirectX::XMFLOAT3 prevCellID{};
			DirectX::XMFLOAT3 cellID{};
			render::SkylightingSettings block{};
		} _grid;

		// Counters are atomic for telemetry; the window state is render-thread only.
		struct CaptureCounters
		{
			std::atomic<std::uint64_t> captures{ 0 };
			std::atomic<std::uint64_t> skippedInterior{ 0 };
			std::atomic<std::uint64_t> skippedDisabled{ 0 };
			std::atomic<std::uint64_t> skippedTargets{ 0 };
			std::atomic<std::uint64_t> failed{ 0 };
			std::atomic<float> cpuMsAverage{ 0.0f };
			std::atomic<float> cpuMsMax{ 0.0f };
			// Per-capture window averages: accumulate, render, hook predicate, hook stock, hook own.
			std::array<std::atomic<float>, 5> stageMs{};
			std::atomic_bool stockTargetRestored{ true };
		} _counters;
		struct ProbeCounters
		{
			std::atomic<std::uint64_t> dispatches{ 0 };
			std::atomic<std::uint64_t> skippedNotFullSky{ 0 };
			std::atomic<std::uint64_t> skippedNoOcclusion{ 0 };
			std::atomic<std::uint64_t> skippedNoGrid{ 0 };
			std::atomic<std::uint64_t> resets{ 0 };
			std::atomic<std::uint64_t> resetsLoad{ 0 };
			std::atomic<std::uint64_t> resetsRebuild{ 0 };
			std::atomic<std::uint64_t> debugFrames{ 0 };
			std::atomic<std::uint64_t> waterDraws{ 0 };
			std::atomic<float> gpuMs{ 0.0f };
			// Latest frame: cell id, array origin, valid margin.
			std::array<std::atomic<std::int32_t>, 9> grid{};
		} _probeCounters;
		double _windowCpuMsSum = 0.0;
		float _windowCpuMsMax = 0.0f;
		std::uint32_t _windowCaptures = 0;
		// Stage tick totals at the last summary: accumulate, render, predicate, stock, own.
		std::array<std::uint64_t, 5> _timingSnapshot{};
		std::uint64_t _anchorFrames = 0;
		CaptureState _loggedState = CaptureState::kDisabled;
		ProbeState _probeState = ProbeState::kPending;
		bool _loggedFirstCapture = false;
		bool _loggedTargetFailure = false;

		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
	};
}
