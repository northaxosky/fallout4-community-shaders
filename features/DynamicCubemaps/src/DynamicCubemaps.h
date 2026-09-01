#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Render/PixelShaderResourceSnapshot.h"

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
		public Feature,
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		enum class DebugVisualization : std::uint32_t
		{
			kOff,
			kCaptureInput,
			kFilteredReflections,
			kReflectionContribution
		};

		struct Settings
		{
			bool enabled = true;
		};

		static DynamicCubemaps* GetSingleton();

		std::string_view GetName() const override { return "DynamicCubemaps"; }
		std::string_view GetDisplayName() const override { return "Dynamic Cubemaps"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Captures and prefilters the current scene for deferred environment reflections.";
		}
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnDataLoaded() override;
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

		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent& a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

	private:
		static constexpr std::uint32_t kCubemapSize = 256;
		static constexpr std::uint32_t kMipLevels = 9;
		static constexpr std::uint32_t kBc6hMipLevels = 7;
		static constexpr std::uint32_t kPreviewWidth = 512;
		static constexpr std::uint32_t kPreviewHeight = 256;
		static constexpr std::uint32_t kDynamicCubemapPSSlot = 16;
		static constexpr std::uint32_t kDynamicCubemapPSSlotCount = 2;

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
			DirectX::XMFLOAT3 previousCameraOrigin{};
			bool reset = true;
		};

		struct CompressedCube
		{
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11ShaderResourceView> srv;
		};

		struct alignas(16) UpdateCubemapCB
		{
			DirectX::XMFLOAT4 ViewToWorld[3];
			DirectX::XMFLOAT4 CameraOrigin;
			DirectX::XMFLOAT4 CameraPreviousOrigin;
			DirectX::XMFLOAT4 NDCToViewMul;
			DirectX::XMFLOAT4 NDCToViewAdd;
			DirectX::XMFLOAT4X4 InvProj;
			DirectX::XMFLOAT4 ActiveRatioAndExtent;
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

		void SaveSettings();
		void PublishSettings() noexcept;
		void SaveBindings();
		void RestoreBindings();
		void BindCubemaps(ID3D11DeviceContext* a_context);
		void UpdateCubemap();
		void UpdateCubemapCapture(bool a_reflections);
		void Inference(bool a_reflections);
		void Irradiance(
			bool a_reflections,
			std::uint32_t a_startLevel,
			std::uint32_t a_endLevel,
			bool a_doSetup);
		void CompressToBC6H(bool a_reflections);
		void RenderCubemapPreview();
		FeatureDebugTexture GetCubemapDebugTexture() const;
		bool CreateResources(ID3D11Device* a_device);
		void ResetCapture();
		ID3D11ShaderResourceView* ResolveReflectionFallback() const noexcept;

		CaptureStream& Stream(bool a_reflections);
		CubeTexture& Filtered(bool a_reflections);

		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _enabled{ true };
		std::atomic_bool _queuedReset{ false };
		std::atomic_bool _usedEngineReflectionFallback{ false };
		std::atomic_bool _reflectionFallbackResolved{ false };
		std::atomic_bool _cameraReadyLastFrame{ false };
		std::atomic_bool _previewPopulated{ false };
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
		CubeTexture _environment;
		CubeTexture _reflections;
		CompressedCube _environmentBC6H;
		CompressedCube _reflectionsBC6H;
		winrt::com_ptr<ID3D11ShaderResourceView> _environmentArraySRV;
		winrt::com_ptr<ID3D11ShaderResourceView> _reflectionsArraySRV;
		winrt::com_ptr<ID3D11Texture2D> _bc6hScratchTexture;
		std::array<
			winrt::com_ptr<ID3D11UnorderedAccessView>,
			kBc6hMipLevels>
			_bc6hScratchUAVs;
		winrt::com_ptr<ID3D11Texture2D> _previewTexture;
		winrt::com_ptr<ID3D11ShaderResourceView> _previewSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> _previewUAV;

		winrt::com_ptr<ID3D11Resource> _defaultCubemapResource;
		winrt::com_ptr<ID3D11ShaderResourceView> _defaultCubemap;
		winrt::com_ptr<ID3D11SamplerState> _computeSampler;
		winrt::com_ptr<ID3D11Buffer> _updateBuffer;
		winrt::com_ptr<ID3D11Buffer> _filterBuffer;
		winrt::com_ptr<ID3D11Buffer> _bc6hBuffer;
		winrt::com_ptr<ID3D11ComputeShader> _updateCS;
		winrt::com_ptr<ID3D11ComputeShader> _updateReflectionsCS;
		winrt::com_ptr<ID3D11ComputeShader> _inferCS;
		winrt::com_ptr<ID3D11ComputeShader> _inferReflectionsCS;
		winrt::com_ptr<ID3D11ComputeShader> _irradianceCS;
		winrt::com_ptr<ID3D11ComputeShader> _bc6hEncodeCS;
		winrt::com_ptr<ID3D11ComputeShader> _previewCS;

		cs::render::PixelShaderResourceSnapshot<kDynamicCubemapPSSlotCount>
			_engineBindings;

		std::atomic<NextTask> _nextTask{
			NextTask::kCaptureInferAndIrradianceA
		};
		float _previousHoursPassed = 0.0f;
		std::uint32_t _lastCallbackFrame = 0;
		bool _lastCallbackFrameValid = false;
		bool _captureSourceLogged = false;
		std::string _validationDetail;
	};
}
