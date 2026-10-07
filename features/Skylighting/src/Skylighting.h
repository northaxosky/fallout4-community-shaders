#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "SkylightingSettings.h"

#include <DirectXMath.h>
#include <d3d11.h>

#include <array>
#include <atomic>
#include <cfloat>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <winrt/base.h>

namespace cs::features
{
	class Skylighting : public Feature
	{
	public:
		using Settings = skylighting::Settings;

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
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;

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
		FeatureDebugTexture GetOcclusionDebugTexture() const;
		void LogCaptureSummary(CaptureState a_state);

		Settings _settings;

		winrt::com_ptr<ID3D11Texture2D> _occlusionTexture;
		winrt::com_ptr<ID3D11DepthStencilView> _occlusionDSV;
		winrt::com_ptr<ID3D11ShaderResourceView> _occlusionSRV;
		std::atomic_bool _debugPreviewEnabled{ false };

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
		double _windowCpuMsSum = 0.0;
		float _windowCpuMsMax = 0.0f;
		std::uint32_t _windowCaptures = 0;
		// Stage tick totals at the last summary: accumulate, render, predicate, stock, own.
		std::array<std::uint64_t, 5> _timingSnapshot{};
		std::uint64_t _anchorFrames = 0;
		CaptureState _loggedState = CaptureState::kDisabled;
		bool _loggedFirstCapture = false;
		bool _loggedTargetFailure = false;
	};
}
