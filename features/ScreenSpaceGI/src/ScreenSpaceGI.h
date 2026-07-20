#pragma once

#include "Feature.h"
#include "Render/Engine.h"
#include "Utils/CSBuffer.h"

#include <DirectXMath.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <winrt/base.h>

namespace cs::features
{
	class ScreenSpaceGI : public Feature
	{
	public:
		static ScreenSpaceGI* GetSingleton();

		std::string_view GetName() const override { return "ScreenSpaceGI"; }
		std::string GetCategory() const override { return "Lighting"; }
		std::string GetFeatureSummary() const override { return "Screen-space ambient occlusion and indirect diffuse lighting."; }
		std::vector<FeatureRequirement> GetRequirements() const override { return {}; }
		EnbPolicy GetEnbPolicy() const override { return EnbPolicy::kDeactivate; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }
		bool IsReady();

		struct Settings
		{
			bool  denoiseEnabled = true;
			float denoiseRadius = 2.0f;
			float effectRadius = 256.0f;
			float aoPower = 2.5f;
			float depthFadeStart = 40000.0f;
			float depthFadeEnd = 50000.0f;
			int   numSlices = 4;
			int   numSteps = 16;
			bool  enabled = false;
			bool  kssaoProbeEnabled = false;
			int   kssaoProbeMode = 0;
			float kssaoProbeValue = 0.0f;
			int   kssaoProbeRt = 45;
			int   kssaoProbeAnchor = 2;
			int   kssaoProbeLumaRt = 3;
			bool  kssaoProbeAllFinal = false;
			bool  noiseFrozen = true;
		};

		struct CaptureConfig
		{
			bool                       enabled = false;
			std::vector<std::uint32_t> formIds;
			int                        settleFrames = 300;
			int                        intervalFrames = 120;
			int                        maxSnapshots = 4;
			int                        hotkey = 0x78;
			std::string                output = "ScreenSpaceGI\\oracle_capture.json";
		};

	private:
		struct alignas(16) ResolveCB
		{
			std::uint32_t Extent[2];
			std::uint32_t Origin[2];
			std::uint32_t FrameIndex;
			std::uint32_t HasAO;
			float         AoPower;
			std::uint32_t Padding;
		};
		static_assert(sizeof(ResolveCB) % 16 == 0);

		// Matches XeGTAOCB in Shaders/XeGTAO/common.hlsli (128 bytes, 8x16).
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
		};
		static_assert(sizeof(XeGTAOCB) == 128);

		// Matches DecodeCB in Shaders/XeGTAO/decode.cs.hlsl.
		struct alignas(16) DecodeCB
		{
			DirectX::XMFLOAT4X4 InvProj;      // row-major; = CameraMatrices.invProj
			float               RcpFrameDim[2];
			float               FrameDim[2];
		};
		static_assert(sizeof(DecodeCB) % 16 == 0);

		struct alignas(16) KssaoOverwriteCB
		{
			std::uint32_t TargetExtent[2];
			std::uint32_t SourceExtent[2];
			std::uint32_t Mode;
			std::uint32_t Padding[3];
		};
		static_assert(sizeof(KssaoOverwriteCB) % 16 == 0);

		ScreenSpaceGI() = default;

		void SaveSettings();
		void OnComputeResolve();
		void OnKssaoOverwrite(int a_anchor);
		void OnKssaoReadback();
		void OnAnchorDumpFrameBegin();
		void OnAnchorDumpDraw();
		void OnAnchorDumpFrameEnd();
		void OnPreSunLightDraw();
		void OnPostDeferredLights();
		// Dev diagnostic: brokered PSSetShader bind-trace. Logs each distinct bound-PS sha + DLI phase during the armed dump window to locate the ambient/IBL draw.
		void OnPixelShaderBind(ID3D11PixelShader* a_bound);
		static void PixelShaderBindTrampoline(ID3D11PixelShader* a_bound);
		bool EnsureResources();
		void CaptureOracle(ID3D11DeviceContext* a_context, RE::BSGraphics::State* a_state);
		// Writes our AO into one engine SAO render target; OnKssaoOverwrite loops it over the SAO-Final family when kssao_probe_all_final is set.
		void OverwriteRt(cs::engine::RenderTarget a_target);

		static constexpr std::uint32_t kBouncePSSlot = 0;
		static constexpr std::uint32_t kAOPSSlot = 13;

		Settings _settings;
		CaptureConfig _capture;
		std::atomic_bool _started{ false };
		std::atomic_bool _resourcesReady{ false };
		std::atomic_bool _resourceInitFailed{ false };
		std::atomic_bool _ssgiBound{ false };

		std::unique_ptr<cs::buffer::Texture2D> _bounceTexture;
		std::unique_ptr<cs::buffer::Texture2D> _aoTexture;
		std::unique_ptr<cs::buffer::ConstantBuffer> _resolveCB;
		std::unique_ptr<cs::buffer::Texture2D> _linearDepthTex;
		std::unique_ptr<cs::buffer::Texture2D> _workingDepthTex;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> _workingDepthMipUAVs;
		std::unique_ptr<cs::buffer::Texture2D> _viewNormalTex;
		std::unique_ptr<cs::buffer::Texture2D> _aoRawTex;
		std::unique_ptr<cs::buffer::Texture2D> _aoDenoisedTex;
		winrt::com_ptr<ID3D11Texture2D> _noiseTex;
		winrt::com_ptr<ID3D11ShaderResourceView> _noiseSRV;
		winrt::com_ptr<ID3D11SamplerState> _pointClampSampler;
		std::unique_ptr<cs::buffer::ConstantBuffer> _xegtaoCB;
		std::unique_ptr<cs::buffer::ConstantBuffer> _decodeCB;
		winrt::com_ptr<ID3D11ComputeShader> _resolveCS;
		winrt::com_ptr<ID3D11ComputeShader> _decodeCS;
		winrt::com_ptr<ID3D11ComputeShader> _prefilterCS;
		winrt::com_ptr<ID3D11ComputeShader> _aoCS;
		winrt::com_ptr<ID3D11ComputeShader> _denoiseCS;
		std::array<winrt::com_ptr<ID3D11ComputeShader>, 4> _kssaoOverwriteCS;
		std::unique_ptr<cs::buffer::ConstantBuffer> _kssaoOverwriteCB;
		winrt::com_ptr<ID3D11Texture2D> _kssaoScratch;
		winrt::com_ptr<ID3D11ShaderResourceView> _kssaoScratchSRV;
		winrt::com_ptr<ID3D11Texture2D> _kssaoLumaStaging;
		DXGI_FORMAT _kssaoScratchFormat = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT _kssaoLumaFormat = DXGI_FORMAT_UNKNOWN;
		std::uint32_t _kssaoScratchW = 0;
		std::uint32_t _kssaoScratchH = 0;
		std::uint32_t _kssaoLumaW = 0;
		std::uint32_t _kssaoLumaH = 0;
		std::uint32_t _kssaoReadbackFrame = 0;
		bool _kssaoNotReadyLogged = false;
		bool _kssaoUnsupportedLogged = false;
		bool _kssaoReadbackUnsupportedLogged = false;
		bool _kssaoReadbackEnteredLogged = false;
		bool _kssaoReadbackNullRtmLogged = false;
		bool _kssaoReadbackNullDeviceLogged = false;
		bool _kssaoReadbackZeroExtentLogged = false;
		bool _kssaoOverwriteFiredLogged = false;
		bool _kssaoOverwriteMode0Logged = false;
		bool _kssaoDispatchLoggedOnce = false;
		bool _kssaoOverwriteSkipLogged = false;
		bool _compositeSlotMapLogged = false;
		bool _xegtaoGateLogged = false;
		bool _xegtaoProducedLogged = false;
		bool _xegtaoCbLogged = false;
		bool _xegtaoInnerFailLogged = false;
		bool _aoStatsLogged = false;
		std::uint32_t _allocW = 0;
		std::uint32_t _allocH = 0;
		std::uint32_t _generation = 0;
		bool _captureArmed = false;
		bool _captureKeyDown = false;
		int _snapshotCount = 0;
		std::string _captureJson;
		std::atomic_bool _dumpArmed{ false };
		bool _dumpKeyDown = false;
		int _dumpOrdinal = 0;
		int _dumpFramesLogged = 0;
		int _dumpTripleMatches = 0;
		int _dumpMatchOrdinal = -1;
		int _dumpIdentityMatches = 0;
		std::atomic_bool _insideDLI{ false };
		std::atomic_bool _bindTraceArmed{ false };
		std::mutex _bindTraceMutex;
		std::unordered_set<std::string> _bindTraceSeen;
	};
}
