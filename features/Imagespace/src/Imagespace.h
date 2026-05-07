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
		};

		Settings settings;

	private:
		Imagespace() = default;

		void LoadSettings();
		void SaveSettings();

		bool EnsureCompositeResources(uint32_t a_width, uint32_t a_height, uint32_t a_format);
		bool EnsurePyramidResources(uint32_t a_width, uint32_t a_height);
		bool EnsureBloomResources(uint32_t a_width, uint32_t a_height, int a_mips);
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
