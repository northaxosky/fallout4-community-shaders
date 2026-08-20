#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Render/Engine.h"
#include "Utils/CSBuffer.h"

#include <DirectXMath.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <winrt/base.h>

namespace cs::features
{
	class ScreenSpaceGI : public Feature
	{
	public:
		static ScreenSpaceGI* GetSingleton();

		std::string_view GetName() const override { return "ScreenSpaceGI"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override { return "Screen-space ambient occlusion and indirect diffuse lighting."; }
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		struct Settings
		{
			bool  denoiseEnabled = true;
			float denoiseRadius = 2.0f;
			float effectRadius = 256.0f;
			float aoPower = 3.0f;
			float depthFadeStart = 40000.0f;
			float depthFadeEnd = 50000.0f;
			float bounceStrength = 2.0f;
			int   radianceSourceRT = static_cast<int>(cs::engine::RenderTarget::kMain);
			int   bounceDelivery = 2;
			int   numSlices = 4;
			int   numSteps = 16;
			bool  enabled = false;
			int   mode = 2;
			bool  noiseFrozen = true;
			bool  injectAmbientPass = false;
		};

	private:
		struct alignas(16) ResolveCB
		{
			std::uint32_t Extent[2];
			std::uint32_t Origin[2];
			std::uint32_t FrameIndex;
			std::uint32_t HasAO;
			float         AoPower;
			std::uint32_t HasBounce;
			float         BounceStrength;
			float         Padding[3];
			float         ViewToWorld[12];
		};
		static_assert(sizeof(ResolveCB) % 16 == 0);

		// Must match Shaders/XeGTAO/common.hlsli.
		struct alignas(16) XeGTAOCB
		{
			float         NDCToViewMul[4];
			float         NDCToViewAdd[4];
			float         TexDim[2];
			float         RcpTexDim[2];
			float         FrameDim[2];
			float         RcpFrameDim[2];
			std::uint32_t FrameIndex;
			std::uint32_t NumSlices;
			std::uint32_t NumSteps;
			float         MinScreenRadius;
			float         AORadius;
			float         EffectRadius;
			float         Thickness;
			float         AOPower;
			float         DepthFadeRange[2];
			float         DepthFadeScaleConst;
			float         BlurRadius;
			float         DistanceNormalisation;
			float         CenterBeta;
			float         _pad[2];
			float         RadianceScale[2];
			float         _bouncePad[2];
			float         ViewToWorld[12];
		};
		static_assert(sizeof(XeGTAOCB) == 192);

		// Must match Shaders/XeGTAO/decode.cs.hlsl.
		struct alignas(16) DecodeCB
		{
			DirectX::XMFLOAT4X4 InvProj;  // Row-major and untransposed.
			float               RcpFrameDim[2];
			float               FrameDim[2];
		};
		static_assert(sizeof(DecodeCB) % 16 == 0);

		struct alignas(16) AOIntegrationCB
		{
			std::uint32_t TargetExtent[2];
			std::uint32_t SourceExtent[2];
			std::uint32_t Mode;
			std::uint32_t Padding[3];
		};
		static_assert(sizeof(AOIntegrationCB) % 16 == 0);

		struct alignas(16) BounceIntegrationCB
		{
			std::uint32_t TargetExtent[2];
			std::uint32_t SourceExtent[2];
		};
		static_assert(sizeof(BounceIntegrationCB) == 16);

		struct alignas(16) BounceTelemetryCB
		{
			std::uint32_t SourceExtent[2];
			std::uint32_t OutputExtent[2];
		};
		static_assert(sizeof(BounceTelemetryCB) == 16);

		ScreenSpaceGI() = default;

		void SaveSettings();
		void OnComputeResolve();
		void OnAOIntegration();
		void OnPreSunLightDraw();
		void OnAmbientPassInjection(ID3D11DeviceContext* a_context);
		void OnPostDeferredLights();
		bool IsReady();
		bool EnsureBakeResources();
		bool EnsureResources();
		void IntegrateAO(cs::engine::RenderTarget a_target);
		void IntegrateBounce();
		void PollBounceTelemetry(ID3D11DeviceContext* a_context);
		void QueueBounceTelemetry(
			ID3D11DeviceContext* a_context,
			std::uint32_t a_sourceWidth,
			std::uint32_t a_sourceHeight,
			std::uint32_t a_frameIndex);

		static constexpr std::uint32_t kBouncePSSlot = 26;
		static constexpr std::uint32_t kBounceTelemetryWidth = 64;
		static constexpr std::uint32_t kBounceTelemetryHeight = 36;
		static constexpr std::uint32_t kBounceTelemetryIntervalFrames = 30;

		Settings _settings;
		std::atomic_bool _started{ false };
		std::atomic_bool _bakeResourcesReady{ false };
		std::atomic_bool _bakeResourceInitFailed{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _resourceInitFailed{ false };
		std::atomic_bool _ssgiBound{ false };
		std::atomic_bool _ssgiBoundLastFrame{ false };
		std::atomic_bool _bounceInjectedLastFrame{ false };
		std::atomic_uint32_t _bounceAnchorBindsLastFrame{ 0 };
		std::atomic_uint32_t _ambientPreDrawMatchesLastFrame{ 0 };
		std::atomic_uint32_t _ambientPreDrawBounceBindsLastFrame{ 0 };
		std::atomic_bool _bounceRTVActiveLastFrame{ false };
		std::atomic_uint32_t _bounceRTVDrawsLastFrame{ 0 };
		std::atomic_bool _aoProducedLastFrame{ false };
		std::atomic_bool _bounceProducedLastFrame{ false };
		std::atomic_bool _radianceAvailableLastFrame{ false };
		std::atomic_bool _denoisedLastFrame{ false };
		std::atomic_bool _aoIntegrationActiveLastFrame{ false };
		std::atomic_uint32_t _resolveDispatchedLastFrame{ 0 };
		std::atomic_uint32_t _aoIntegrationDispatchedLastFrame{ 0 };
		std::atomic_bool _bounceTelemetryReady{ false };
		std::atomic<float> _bounceMean{ 0.0f };
		std::atomic<float> _bounceMax{ 0.0f };
		std::atomic<float> _bounceNonzeroFraction{ 0.0f };
		bool _ambientPassRegistered = false;

		std::unique_ptr<cs::buffer::Texture2D> _bounceTexture;
		std::unique_ptr<cs::buffer::Texture2D> _aoTexture;
		std::unique_ptr<cs::buffer::ConstantBuffer> _resolveCB;
		std::unique_ptr<cs::buffer::Texture2D> _linearDepthTex;
		std::unique_ptr<cs::buffer::Texture2D> _workingDepthTex;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> _workingDepthMipUAVs;
		std::unique_ptr<cs::buffer::Texture2D> _viewNormalTex;
		std::unique_ptr<cs::buffer::Texture2D> _aoRawTex;
		std::unique_ptr<cs::buffer::Texture2D> _aoDenoisedTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceSHRawTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceCoCgRawTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceSHDenoisedTex;
		std::unique_ptr<cs::buffer::Texture2D> _bounceCoCgDenoisedTex;
		winrt::com_ptr<ID3D11Texture2D> _noiseTex;
		winrt::com_ptr<ID3D11ShaderResourceView> _noiseSRV;
		winrt::com_ptr<ID3D11SamplerState> _pointClampSampler;
		std::unique_ptr<cs::buffer::ConstantBuffer> _xegtaoCB;
		std::unique_ptr<cs::buffer::ConstantBuffer> _decodeCB;
		winrt::com_ptr<ID3D11ComputeShader> _resolveCS;
		winrt::com_ptr<ID3D11ComputeShader> _decodeCS;
		winrt::com_ptr<ID3D11ComputeShader> _prefilterCS;
		winrt::com_ptr<ID3D11ComputeShader> _aoCS;
		winrt::com_ptr<ID3D11ComputeShader> _bounceCS;
		winrt::com_ptr<ID3D11ComputeShader> _denoiseCS;
		winrt::com_ptr<ID3D11ComputeShader> _bounceDenoiseCS;
		winrt::com_ptr<ID3D11ComputeShader> _bounceTelemetryCS;
		std::unique_ptr<cs::buffer::ConstantBuffer> _bounceTelemetryCB;
		std::unique_ptr<cs::buffer::Texture2D> _bounceTelemetryStats;
		winrt::com_ptr<ID3D11Texture2D> _bounceTelemetryReadback;
		bool _bounceTelemetryPending = false;
		std::uint32_t _bounceTelemetryLastQueuedFrame = 0;
		std::array<winrt::com_ptr<ID3D11ComputeShader>, 4> _aoIntegrationCS;
		std::unique_ptr<cs::buffer::ConstantBuffer> _aoIntegrationCB;
		winrt::com_ptr<ID3D11Texture2D> _aoIntegrationScratch;
		winrt::com_ptr<ID3D11ShaderResourceView> _aoIntegrationScratchSRV;
		DXGI_FORMAT _aoIntegrationScratchFormat = DXGI_FORMAT_UNKNOWN;
		std::uint32_t _aoIntegrationScratchW = 0;
		std::uint32_t _aoIntegrationScratchH = 0;
		winrt::com_ptr<ID3D11VertexShader> _bounceIntegrationVS;
		winrt::com_ptr<ID3D11PixelShader> _bounceIntegrationPS;
		std::unique_ptr<cs::buffer::ConstantBuffer> _bounceIntegrationCB;
		winrt::com_ptr<ID3D11BlendState> _bounceIntegrationBlendState;
		winrt::com_ptr<ID3D11DepthStencilState> _bounceIntegrationDepthStencilState;
		winrt::com_ptr<ID3D11RasterizerState> _bounceIntegrationRasterizerState;
		std::uint32_t _bounceAllocW = 0;
		std::uint32_t _bounceAllocH = 0;
		std::uint32_t _allocW = 0;
		std::uint32_t _allocH = 0;
		std::uint32_t _generation = 0;
	};
}
