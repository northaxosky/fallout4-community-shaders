#pragma once

#include "Buffer.h"
#include "Feature.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace cs::features
{
	class Imagespace : public Feature
	{
	public:
		static Imagespace* GetSingleton();

		std::string_view GetName() const override { return "Imagespace"; }

		void Load() override;
		void OnPostPostLoad() override;
		void DrawSettings() override;

		void RunFrame();

		struct Settings
		{
			bool        enabled            = true;

			// Tonemap + LUT.
			int         iOperator          = 1;
			float       fExposure          = 1.0f;
			bool        bLUTEnable         = false;
			std::string sLUTPath           = "";
			float       fLUTStrength       = 1.0f;

			// Adaptive exposure.
			bool        bAdaptiveExposure  = true;
			float       fAdaptationSpeed   = 1.0f;
			float       fExposureKey       = 0.18f;
			float       fExposureMin       = 0.25f;
			float       fExposureMax       = 4.0f;

			// Bloom.
			bool        bBloomEnable       = true;
			float       fBloomThreshold    = 0.85f;
			float       fBloomIntensity    = 0.05f;
			int         iBloomMips         = 5;

			// Lens.
			bool        bVignetteEnable    = true;
			float       fVignetteIntensity = 0.3f;
			bool        bCAEnable          = true;
			float       fCAIntensity       = 0.5f;
			bool        bSharpenEnable     = true;
			float       fSharpness         = 0.4f;

			// Bokeh DOF (IS-5; UI lands in IS-5b alongside INI defaults + ENB-aware logic).
			bool        bDOFEnable         = false;
			float       fAperture          = 0.05f;
			float       fFocusDistance     = 1500.0f;
			float       fFocalLength       = 50.0f;
			float       fFocusRange        = 200.0f;
			int         iDOFQuality        = 1;
			float       fCoCLimitFactor    = 0.04f;
		};

		Settings settings;

	private:
		Imagespace() = default;

		void LoadSettings();
		void SaveSettings();

		bool EnsureCompositeResources(uint32_t a_width, uint32_t a_height, uint32_t a_format);
		bool EnsurePyramidResources(uint32_t a_width, uint32_t a_height);
		bool EnsureBloomResources(uint32_t a_width, uint32_t a_height, int a_mips);
		bool EnsureDOFResources(uint32_t a_width, uint32_t a_height);
		void RunDOF(uint32_t a_width, uint32_t a_height, ID3D11Texture2D* a_fbTex);
		ID3D11ComputeShader* GetCS(const wchar_t* a_path, ID3D11ComputeShader*& a_slot, const char* a_name);
		bool LoadLUTFromDisk(const std::string& a_filename);

		// Composite (tonemap + LUT + bloom-add + lens).
		std::unique_ptr<imagespace::Texture2D>      compositeScratch;
		std::unique_ptr<imagespace::ConstantBuffer> compositeCB;
		ID3D11ComputeShader*                        compositeCS = nullptr;
		winrt::com_ptr<ID3D11ShaderResourceView>    lutSRV;
		winrt::com_ptr<ID3D11SamplerState>          lutSampler;
		std::string                                 lutLoadedPath;

		// Adaptive exposure: log-luma pyramid + ping-pong scalar.
		std::unique_ptr<imagespace::Texture2D>      lumPyramid;                 // single Texture2D, multi-mip
		std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> lumPyramidUAVs;
		std::unique_ptr<imagespace::ConstantBuffer> pyramidCB;
		ID3D11ComputeShader*                        lumPyramidCS = nullptr;
		std::array<std::unique_ptr<imagespace::Texture2D>, 2> expoPingPong;
		std::unique_ptr<imagespace::ConstantBuffer> exposureCB;
		ID3D11ComputeShader*                        exposureCS = nullptr;
		uint32_t                                    pyramidMipCount  = 0;
		int                                         expoFrameIdx     = 0;

		// Bloom chain + scratch.
		std::array<std::unique_ptr<imagespace::Texture2D>, 6> bloomChain;
		std::array<std::unique_ptr<imagespace::Texture2D>, 6> bloomScratch;
		std::unique_ptr<imagespace::ConstantBuffer> bloomCB;
		std::unique_ptr<imagespace::ConstantBuffer> bloomThresholdCB;
		ID3D11ComputeShader*                        bloomThresholdCS = nullptr;
		ID3D11ComputeShader*                        bloomDownCS      = nullptr;
		ID3D11ComputeShader*                        bloomUpCS        = nullptr;
		int                                         bloomMipsAlloc   = 0;

		// Bokeh DOF (IS-5).
		std::unique_ptr<imagespace::Texture2D>      dofCoCTex;             // half-res R16F: linearised CoC (signed)
		std::unique_ptr<imagespace::Texture2D>      dofTileTex;            // /16 R16G16F: per-tile {minCoC, maxCoC} for early-out
		std::unique_ptr<imagespace::Texture2D>      dofHalfColor;          // half-res R11G11B10F: downsampled scene (Pass 1 output)
		std::unique_ptr<imagespace::Texture2D>      dofHalfBlurred;        // half-res R11G11B10F: blur output (Pass 3 output) - ping-pong with dofHalfColor
		std::unique_ptr<imagespace::ConstantBuffer> dofCB;
		ID3D11ComputeShader*                        dofDepthCoCCS    = nullptr;
		ID3D11ComputeShader*                        dofDilateCS      = nullptr;
		ID3D11ComputeShader*                        dofBlurCS        = nullptr;
		ID3D11ComputeShader*                        dofCompositeCS   = nullptr;
		winrt::com_ptr<ID3D11SamplerState>          dofLinearClampSampler;
		uint32_t                                    dofWidth         = 0;
		uint32_t                                    dofHeight        = 0;

		// Cached for dim-change reallocation.
		uint32_t                                    scratchWidth  = 0;
		uint32_t                                    scratchHeight = 0;
		uint32_t                                    scratchFormat = 0;
		uint32_t                                    pyramidWidth  = 0;
		uint32_t                                    pyramidHeight = 0;
		uint32_t                                    bloomWidth    = 0;
		uint32_t                                    bloomHeight   = 0;

		bool                                        firstFireLogged = false;
		bool                                        testModeActive  = false;
	};
}
