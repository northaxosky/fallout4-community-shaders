#pragma once

#include "DynamicCubemapsSettings.h"

#include "Feature.h"
#include "FeatureCategories.h"
#include "ShaderDefines.h"

#include <DirectXMath.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <winrt/base.h>

struct ID3D11Buffer;
struct ID3D11ComputeShader;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Resource;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D11UnorderedAccessView;

namespace cs
{
	struct DynamicCubemapsFeatureData;
}

namespace cs::features
{
	class DynamicCubemaps :
		public ShaderFeature<dc::kShaderDefines>
	{
	public:
		enum class DebugVisualization : std::uint32_t
		{
			kOff,
			kCaptureInput,
			kFilteredReflections
		};

		using Settings = dynamic_cubemaps::Settings;

		static DynamicCubemaps* GetSingleton();

		std::string_view GetName() const override { return "DynamicCubemaps"; }
		std::string_view GetDisplayName() const override { return "Dynamic Cubemaps"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Real-time environment mapping and reflections.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void Prepass() override;
		void OnLoadingMenuClosed() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;

		cs::DynamicCubemapsFeatureData GetCommonBufferData() const;

	private:
		static constexpr std::uint32_t kCubemapSize = 256;
		static constexpr std::uint32_t kMipLevels = 9;
		static constexpr std::uint32_t kBc6hMipLevels = 7;
		static constexpr std::uint32_t kPreviewWidth = 512;
		static constexpr std::uint32_t kPreviewHeight = 256;
		static constexpr std::uint32_t kDynamicCubemapPSSlot = 30;
		static constexpr std::uint32_t kDynamicCubemapPSSlotCount = 2;
		static constexpr std::uint32_t kCompositionPSSlot = 34;
		static constexpr std::uint32_t kCompositionPSSlotCount = 2;

		struct CubeTexture
		{
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11ShaderResourceView> srv;
			winrt::com_ptr<ID3D11UnorderedAccessView> mip0Uav;
			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, kMipLevels> mipUavs;
		};

		struct CaptureStream
		{
			CubeTexture color;
			CubeTexture raw;
			CubeTexture position;
		};

		struct CompressedCube
		{
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11ShaderResourceView> srv;
		};

		struct alignas(16) UpdateCubemapCB
		{
			DirectX::XMFLOAT3 CameraPreviousPosAdjust;
			std::uint32_t CaptureIndex = 0;
			float CaptureDeltaTime = 0.0f;
			std::uint32_t ResetCapture = 0;
			std::uint32_t pad0[2]{};
			DirectX::XMFLOAT4 CaptureCameraOrigin{};
		};
		static_assert(sizeof(UpdateCubemapCB) == 48);

		struct alignas(16) PrepareCaptureCB
		{
			DirectX::XMFLOAT4X4 InvProj;
		};

		struct alignas(16) CaptureLightingState
		{
			float ReferenceLuminance = 0.0f;
			std::uint32_t PendingResetMask = 0;
			std::uint32_t Reset = 0;
			std::uint32_t Initialized = 0;
		};

		struct alignas(16) SpecularMapFilterSettingsCB
		{
			float roughness = 0.0f;
			float pad[3]{};
		};

		struct alignas(16) BC6HEncodeCB
		{
			std::uint32_t textureSizeInBlocksX = 0;
			std::uint32_t textureSizeInBlocksY = 0;
			std::uint32_t mipLevel = 0;
			std::uint32_t pad = 0;
		};

		enum class NextTask : std::uint32_t
		{
			kCaptureInferAndIrradianceA,
			kIrradianceBA,
			kIrradianceBBAndBC6H,
			kCaptureInferAndIrradianceA2,
			kIrradianceBA2,
			kIrradianceBBAndBC6H2
		};

		DynamicCubemaps() = default;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(dynamic_cubemaps::kSchema); }
		void PublishSettings() noexcept;
		void PostDeferred();
		void BindComposition(ID3D11DeviceContext* a_context);
		void ResolveReflectionMode();
		void UpdateCubemap();
		void UpdateCubemapCapture(bool a_reflections);
		void Inference(bool a_reflections);
		void Irradiance(
			std::uint32_t a_startLevel,
			std::uint32_t a_endLevel,
			bool a_doSetup);
		void CompressToBC6H(bool a_reflections);
		void RenderCubemapPreview();
		FeatureDebugTexture GetCubemapDebugTexture() const;
		bool CreateResources(ID3D11Device* a_device);
		void ResetCapture();

		CaptureStream& Stream(bool a_reflections);
		ID3D11ComputeShader* UpdateShader(bool a_reflections) const;
		ID3D11ComputeShader* InferShader(bool a_reflections) const;

		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _enabled{ true };
		std::atomic_bool _enabledSSR{ true };
		std::atomic<float> _materialReflections{ 1.0f };
		std::atomic_bool _queuedReset{ false };
		std::atomic_bool _activeReflections{ false };
		std::atomic_bool _fakeReflections{ false };
		std::atomic_bool _engineReflectionCube{ false };
		std::atomic_bool _cameraReadyLastFrame{ false };
		std::atomic_bool _previewPopulated{ false };
		std::array<std::atomic_bool, 2> _cubemapValid{};
		std::atomic_uint32_t _captureSourceWidth{ 0 };
		std::atomic_uint32_t _captureSourceHeight{ 0 };
		std::atomic_uint32_t _captureSourceFormat{ 0 };
		std::atomic_uint64_t _dispatchCount{ 0 };
		std::atomic_uint64_t _compressionDispatchCount{ 0 };
		std::atomic_uint64_t _previewDispatchCount{ 0 };
		std::atomic_uint64_t _repeatCallbacks{ 0 };
		std::atomic<DebugVisualization> _debugVisualization{
			DebugVisualization::kOff
		};
		Settings _settings;

		CaptureStream _baseStream;
		CaptureStream _reflectionsStream;
		CubeTexture _inferred;
		CubeTexture _filtered;
		CubeTexture _environment;
		CubeTexture _reflections;
		CubeTexture _preparedPosition;
		CubeTexture _preparedColor;
		CubeTexture _preparedUV;
		winrt::com_ptr<ID3D11ShaderResourceView> _preparedPositionArraySRV;
		winrt::com_ptr<ID3D11ShaderResourceView> _preparedColorArraySRV;
		winrt::com_ptr<ID3D11ShaderResourceView> _preparedUVArraySRV;
		CompressedCube _environmentBC6H;
		CompressedCube _reflectionsBC6H;
		winrt::com_ptr<ID3D11ShaderResourceView> _filteredArraySRV;
		winrt::com_ptr<ID3D11Texture2D> _bc6hScratchTexture;
		std::array<
			winrt::com_ptr<ID3D11UnorderedAccessView>,
			kBc6hMipLevels>
			_bc6hScratchUAVs;
		winrt::com_ptr<ID3D11Buffer> _lightingStateBuffer;
		winrt::com_ptr<ID3D11UnorderedAccessView> _lightingStateUAV;
		winrt::com_ptr<ID3D11Texture2D> _previewTexture;
		winrt::com_ptr<ID3D11ShaderResourceView> _previewSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> _previewUAV;

		winrt::com_ptr<ID3D11Resource> _defaultCubemapResource;
		winrt::com_ptr<ID3D11ShaderResourceView> _defaultCubemap;
		winrt::com_ptr<ID3D11SamplerState> _computeSampler;
		winrt::com_ptr<ID3D11Buffer> _updateBuffer;
		winrt::com_ptr<ID3D11Buffer> _prepareBuffer;
		winrt::com_ptr<ID3D11Buffer> _filterBuffer;
		winrt::com_ptr<ID3D11Buffer> _bc6hBuffer;
		winrt::com_ptr<ID3D11ComputeShader> _detectLightingCS;
		winrt::com_ptr<ID3D11ComputeShader> _prepareCS;
		winrt::com_ptr<ID3D11ComputeShader> _prepareReflectionsCS;
		winrt::com_ptr<ID3D11ComputeShader> _updateCS;
		winrt::com_ptr<ID3D11ComputeShader> _updateReflectionsCS;
		winrt::com_ptr<ID3D11ComputeShader> _updateFakeReflectionsCS;
		winrt::com_ptr<ID3D11ComputeShader> _updateSkyReflectionsCS;
		winrt::com_ptr<ID3D11ComputeShader> _inferCS;
		winrt::com_ptr<ID3D11ComputeShader> _inferReflectionsCS;
		winrt::com_ptr<ID3D11ComputeShader> _inferFakeReflectionsCS;
		winrt::com_ptr<ID3D11ComputeShader> _irradianceCS;
		winrt::com_ptr<ID3D11ComputeShader> _bc6hEncodeCS;
		winrt::com_ptr<ID3D11ComputeShader> _previewCS;

		std::atomic<NextTask> _nextTask{
			NextTask::kCaptureInferAndIrradianceA
		};
		std::array<bool, 2> _resetCapture{ true, true };
		std::array<DirectX::XMFLOAT3, 2> _cameraPreviousPosAdjust{};
		std::array<double, 2> _previousCaptureTime{};
		float _previousHoursPassed = 0.0f;
		std::uint32_t _lastCallbackFrame = 0;
		bool _lastCallbackFrameValid = false;
		std::string _validationDetail;
	};
}