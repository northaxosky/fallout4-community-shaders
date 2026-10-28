#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Render/Engine.h"
#include "ShaderDefines.h"
#include "SubsurfaceScatteringConstants.h"
#include "SubsurfaceScatteringSettings.h"
#include "Utils/CSBuffer.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <winrt/base.h>

namespace cs::features
{
	// Replaces the stock skin blur with upstream's separable or Burley scattering.
	class SubsurfaceScattering :
		public ShaderFeature<subsurface_scattering_shader::kShaderDefines>
	{
	public:
		static SubsurfaceScattering* GetSingleton();
		std::string_view GetName() const override { return "SubsurfaceScattering"; }
		std::string_view GetDisplayName() const override { return "Subsurface Scattering"; }
		std::string GetCategory() const override { return FeatureCategories::kMaterials; }
		std::string GetFeatureSummary() const override { return "Screen-space subsurface scattering for skin."; }
		bool Configure(const toml::table&, std::string&) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		void OnRuntimeQuarantined(std::string_view, std::string_view) noexcept override { RestoreNativeControls(); }
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink&) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view) noexcept override;
		using Settings = subsurface_scattering::Settings;

	private:
		using Texture = std::unique_ptr<cs::buffer::Texture2D>;

		struct alignas(16) ConsumerCB
		{
			std::uint32_t Produced{}, pad[3]{};
		};

		// Inputs shaped like upstream's mask, normal and albedo, plus the blur intermediates.
		struct Resources
		{
			Texture mask, normal, albedo, diffuseNoAlbedo, horizontal;
			UINT width{}, height{};
			DXGI_FORMAT format{};
		};

		// Native values restored when the feature is disabled.
		struct NativeSnapshot
		{
			bool screenSpaceSubsurfaceScattering{}, characterLight{};
			float rim{}, fill{};
		};

		enum class Preview : std::uint8_t
		{
			kNone,
			kMask,
			kDiffuse
		};

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(subsurface_scattering::kSchema, _settings); }
		bool CompileShaders();
		bool EnsureResources(ID3D11Texture2D* a_target);
		void UpdateKernels();
		void UpdateConstants(const cs::engine::WorldCameraRecord&);
		void ApplyNativeControls();
		void RestoreNativeControls() noexcept;
		void OnPreDeferredComposite();
		void OnPreSecondDeferredComposite();
		void UpdateConsumer(bool a_produced);
		FeatureDebugTexture GetDebugTexture(Preview a_view) const;

		// FO4: the native composite owns b0, b2 and b12; the consumer takes the free b9.
		static constexpr UINT kConsumerSlot = 9;
		static constexpr UINT kConstantsSlot = 1;
		Settings _settings;
		Resources _textures;
		subsurface_scattering::Constants _data{};
		std::array<float, 12> _kernelKey{};
		bool _kernelsBuilt = false;
		std::unique_ptr<cs::buffer::ConstantBuffer> _constants, _consumer;
		std::optional<ConsumerCB> _consumerData;
		winrt::com_ptr<ID3D11SamplerState> _pointSampler;
		winrt::com_ptr<ID3D11ComputeShader> _prepare, _extract, _horizontal, _vertical, _burley;
		std::optional<NativeSnapshot> _native;
		std::atomic_bool _started{ false }, _resourcesReady{ false }, _failed{ false };
		std::atomic_bool _unsupported{ false }, _produced{ false };
		std::atomic<Preview> _preview{ Preview::kNone };
		std::atomic_bool _nativeApplied{ false };
		std::atomic_uint32_t _dispatches{ 0 };
		std::atomic<std::uint8_t> _lastMode{ 0 };
	};
}
