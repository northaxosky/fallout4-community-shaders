#pragma once

#include "Feature.h"
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
			bool enabled = false;
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
			std::uint32_t Padding[2];
		};
		static_assert(sizeof(ResolveCB) % 16 == 0);

		// Matches XeGTAOCB in Shaders/XeGTAO/common.hlsli (112 bytes, 7x16).
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
			float         _pad;
		};
		static_assert(sizeof(XeGTAOCB) == 112);

		// Matches DecodeCB in Shaders/XeGTAO/decode.cs.hlsl.
		struct alignas(16) DecodeCB
		{
			DirectX::XMFLOAT4X4 InvProj;      // row-major; = CameraMatrices.invProj
			float               RcpFrameDim[2];
			float               FrameDim[2];
		};
		static_assert(sizeof(DecodeCB) % 16 == 0);

		ScreenSpaceGI() = default;

		void SaveSettings();
		void OnComputeResolve();
		void OnAnchorDumpFrameBegin();
		void OnAnchorDumpDraw();
		void OnAnchorDumpFrameEnd();
		void OnPreSunLightDraw();
		void OnPostDeferredLights();
		bool EnsureResources();
		void CaptureOracle(ID3D11DeviceContext* a_context, RE::BSGraphics::State* a_state);

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
		winrt::com_ptr<ID3D11Texture2D> _noiseTex;
		winrt::com_ptr<ID3D11ShaderResourceView> _noiseSRV;
		winrt::com_ptr<ID3D11SamplerState> _pointClampSampler;
		std::unique_ptr<cs::buffer::ConstantBuffer> _xegtaoCB;
		std::unique_ptr<cs::buffer::ConstantBuffer> _decodeCB;
		winrt::com_ptr<ID3D11ComputeShader> _resolveCS;
		winrt::com_ptr<ID3D11ComputeShader> _decodeCS;
		winrt::com_ptr<ID3D11ComputeShader> _prefilterCS;
		winrt::com_ptr<ID3D11ComputeShader> _aoCS;
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
	};
}
