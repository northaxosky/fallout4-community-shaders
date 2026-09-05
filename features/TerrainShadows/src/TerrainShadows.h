#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "Render/PixelShaderResourceSnapshot.h"
#include "Render/PixelShaderSamplerSnapshot.h"
#include "TerrainShadowsMath.h"
#include "Utils/CSBuffer.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <d3d11.h>
#include <winrt/base.h>

namespace cs::features
{
	class TerrainShadows : public Feature
	{
	public:
		static constexpr std::uint32_t kShadowHeightPSSlot = 30;
		static constexpr std::uint32_t kSceneDepthPSSlot = 31;
		static constexpr std::uint32_t kShadowHeightSamplerPSSlot = 13;

		enum class DebugVisualization : std::uint32_t
		{
			kOff,
			kShadowTerm,
			kHeightmap
		};

		static TerrainShadows* GetSingleton();

		std::string_view GetName() const override { return "TerrainShadows"; }
		std::string_view GetDisplayName() const override { return "Terrain Shadows"; }
		std::string GetConfigKey() const override { return "TerrainShadows"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Casts long-range worldspace terrain shadows into directional lighting from an xLODGen heightmap.";
		}

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

		cs::TerrainShadowsFeatureData GetCommonBufferData() const;

		struct Settings
		{
			bool          enabled = true;
			std::uint32_t downsampleFactor =
				terrain_shadows::kDefaultDownsampleFactor;
		};

	private:
		struct alignas(16) ShadowUpdateCB
		{
			float         LightPxDir[2];
			float         LightDeltaZ[2];
			std::uint32_t StartPxCoord;
			float         PxSize[2];
			float         BlendWeight;
			float         PosRange[2];
			float         ZRange[2];
		};
		static_assert(sizeof(ShadowUpdateCB) == 48);
		STATIC_ASSERT_ALIGNAS_16(ShadowUpdateCB);

		struct alignas(16) ShadowStatisticsCB
		{
			float PosRange[2];
			float ZRange[2];
		};
		static_assert(sizeof(ShadowStatisticsCB) == 16);
		STATIC_ASSERT_ALIGNAS_16(ShadowStatisticsCB);

		struct HeightMapRecord
		{
			terrain_shadows::HeightMapMetadata metadata;
			std::filesystem::path              path;
		};

		enum class StatusSeverity
		{
			kInfo,
			kFailure
		};

		TerrainShadows() = default;

		void SaveSettings();
		void PublishSettings();

		void DiscoverHeightMaps();
		void ScanHeightMapDirectory(
			const std::filesystem::path& a_directory,
			terrain_shadows::HeightMapSource a_source,
			bool a_recurseOneLevel);
		void ConsiderHeightMapFile(
			const std::filesystem::path& a_path,
			terrain_shadows::HeightMapSource a_source);

		[[nodiscard]] terrain_shadows::BootstrapReadiness
			GetBootstrapReadiness() const noexcept;

		void OnPostDeferredPrePass();
		bool PollGameHourJump();
		void EnsureLiveResources(ID3D11DeviceContext* a_context);
		bool BuildHeightResources(
			ID3D11Device* a_device,
			ID3D11DeviceContext* a_context,
			const HeightMapRecord& a_record,
			std::uint32_t a_factor,
			std::string& a_error);
		bool UpdateShadow(ID3D11DeviceContext* a_context, bool a_refreshImmediately);
		void ReleaseLiveResources(ID3D11DeviceContext* a_context);
		void UpdateShadowStatistics(ID3D11DeviceContext* a_context);

		void SaveEngineBindings();
		void BindShadowHeights(ID3D11DeviceContext* a_context);
		void RestoreEngineBindings();
		void SaveDebugBindings();
		void BindDebugTexture(ID3D11DeviceContext* a_context);
		void RestoreDebugBindings();

		void PublishStatus(
			const std::string& a_worldspace,
			std::string_view a_detail,
			StatusSeverity a_severity = StatusSeverity::kInfo);

		static std::string ResolveWorldspaceEditorId();

		Settings _settings;
		std::atomic_bool _enabled{ true };
		std::atomic_uint32_t _requestedDownsampleFactor{
			terrain_shadows::kDefaultDownsampleFactor
		};
		std::atomic<DebugVisualization> _debugVisualization{
			DebugVisualization::kOff
		};

		std::atomic_bool _started{ false };
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _renderCallbacksReady{ false };
		std::atomic_bool _computeShaderReady{ false };
		std::atomic_bool _samplerReady{ false };
		std::atomic_bool _constantBufferReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::atomic_bool _shadowResourcesReady{ false };
		std::atomic_bool _shadowPopulated{ false };
		std::atomic_bool _mapLoaded{ false };
		std::atomic_bool _worldspaceResolved{ false };

		std::atomic_uint32_t _sourceWidth{ 0 };
		std::atomic_uint32_t _sourceHeight{ 0 };
		std::atomic_uint32_t _effectiveWidth{ 0 };
		std::atomic_uint32_t _effectiveHeight{ 0 };
		std::atomic_uint32_t _appliedFactorTelemetry{ 1 };
		std::atomic_uint64_t _allocatedBytes{ 0 };
		std::atomic_uint64_t _dispatches{ 0 };
		std::atomic_uint64_t _updates{ 0 };
		std::atomic_uint64_t _fullRefreshes{ 0 };
		std::atomic_uint64_t _binds{ 0 };
		std::atomic_uint64_t _samplerBinds{ 0 };
		std::atomic_uint64_t _debugBinds{ 0 };
		std::atomic_uint64_t _prepassRuns{ 0 };
		std::atomic_size_t _discoveredMaps{ 0 };

		std::atomic_uint64_t _lightDirectionalBinds{ 0 };
		std::atomic_uint64_t _lightInertBinds{ 0 };
		std::atomic_uint64_t _compositeDebugFamilyBinds{ 0 };
		std::atomic_uint64_t _compositeInertFamilyBinds{ 0 };
		std::array<std::atomic_uint64_t, 13> _lightFamilyBinds{};
		std::array<std::atomic_uint64_t, 13> _compositeFamilyBinds{};
		std::atomic_uint64_t _consumerDepthMissing{ 0 };
		std::atomic_uint64_t _debugDepthMissing{ 0 };

		std::atomic_uint64_t _shadowStatSamples{ 0 };
		std::atomic<double> _shadowStatBelow99Pct{ 0.0 };
		std::atomic<double> _shadowStatBelow95Pct{ 0.0 };
		std::atomic<double> _shadowStatBelow75Pct{ 0.0 };
		std::atomic<double> _shadowStatBelow50Pct{ 0.0 };
		std::atomic<double> _shadowStatMean{ 0.0 };
		std::atomic<double> _shadowStatMin{ 0.0 };
		std::atomic<double> _shadowStatMax{ 0.0 };
		std::atomic<double> _sunElevationDegrees{ 0.0 };

		mutable std::mutex _statusMutex;
		std::string _statusWorldspace;
		std::string _statusDetail;
		bool _statusFailed = false;
		std::string _validationDetail;

		// Render thread only.
		std::unordered_map<std::string, HeightMapRecord> _heightMaps;
		terrain_shadows::HeightMapMetadata _loadedMetadata;
		terrain_shadows::DdaPlan _plan;
		std::string _loadedWorldspace;
		std::string _failedWorldspace;
		std::string _failedDetail;
		std::uint32_t _failedFactor = 0;
		std::uint32_t _appliedFactor = 1;
		std::uint32_t _shadowUpdateIndex = 0;
		std::uint32_t _slicesSinceRebuild = 0;
		bool _pendingFullRefresh = false;
		bool _wasEnabledLastFrame = false;
		bool _gameHourSeeded = false;
		float _lastGameHour = 0.0f;
		bool _resourceInitFailed = false;
		std::array<float, 2> _debugHeightRange{};
		bool _shadowStatsPending = false;
		std::chrono::steady_clock::time_point _shadowStatsLastDispatch{};

		std::unique_ptr<cs::buffer::Texture2D> _heightTexture;
		std::unique_ptr<cs::buffer::Texture2D> _shadowTexture;
		// RendererData has no device during bootstrap.
		winrt::com_ptr<ID3D11Buffer> _shadowUpdateCB;
		winrt::com_ptr<ID3D11ComputeShader> _shadowUpdateCS;
		winrt::com_ptr<ID3D11SamplerState> _linearClampSampler;
		winrt::com_ptr<ID3D11ComputeShader> _shadowStatsCS;
		winrt::com_ptr<ID3D11Buffer> _shadowStatsCB;
		winrt::com_ptr<ID3D11Buffer> _shadowStatsBuffer;
		winrt::com_ptr<ID3D11UnorderedAccessView> _shadowStatsUav;
		winrt::com_ptr<ID3D11Buffer> _shadowStatsStaging;

		cs::render::PixelShaderResourceSnapshot<2> _engineShadowBinding;
		cs::render::PixelShaderSamplerSnapshot<1> _engineSamplerBinding;
		cs::render::PixelShaderResourceSnapshot<2> _debugShadowBinding;
		cs::render::PixelShaderSamplerSnapshot<1> _debugSamplerBinding;
	};
}
