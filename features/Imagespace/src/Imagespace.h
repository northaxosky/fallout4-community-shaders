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
		std::vector<std::string_view> GetDependencies() const override { return { "Upscaling" }; }

		void Load() override;
		void OnPostPostLoad() override;
		void DrawSettings() override;

		void RunFrame();

		enum class Preset : int
		{
			kCustom    = 0,
			kSubtle    = 1,
			kStandard  = 2,
			kVivid     = 3,
			kCinematic = 4,
		};

		struct Settings
		{
			bool        enabled            = true;
			int         preset             = static_cast<int>(Preset::kStandard);
			bool        forceWithENB       = false;

			// Tonemap + LUT.
			int         tonemapOperator    = 1;
			float       exposure           = 1.0f;
			bool        lutEnable          = false;
			std::string lutPath            = "";
			float       lutStrength        = 1.0f;

			// Adaptive exposure.
			bool        adaptiveExposure   = true;
			float       adaptationSpeedUp  = 0.5f;
			float       adaptationSpeedDown = 2.0f;
			float       exposureKey        = 0.18f;
			float       exposureMin        = 0.05f;
			float       exposureMax        = 4.0f;

			// Bloom.
			bool        bloomEnable        = true;
			float       bloomThreshold     = 0.85f;
			float       bloomIntensity     = 0.05f;
			int         bloomMips          = 5;

			// Lens.
			bool        vignetteEnable     = true;
			float       vignetteIntensity  = 0.3f;
			bool        caEnable           = true;
			float       caIntensity        = 0.5f;
			bool        sharpenEnable      = true;
			float       sharpness          = 0.4f;

			// Sun + lens.
			bool        sunspriteEnable    = true;
			float       sunspriteIntensity = 0.6f;
			float       sunspriteSize      = 0.05f;
			bool        lensFlareEnable    = false;
			float       lensFlareIntensity = 0.8f;
			int         lensFlareGhosts    = 5;

			// Bokeh DOF.
			bool        dofEnable          = false;
			float       aperture           = 0.05f;
			float       focusDistance      = 1500.0f;
			float       focalLength        = 50.0f;
			float       focusRange         = 200.0f;
			int         dofQuality         = 1;
			float       cocLimitFactor     = 0.04f;
		};

		Settings settings;

	private:
		Imagespace() = default;

		void LoadSettings();
		void SaveSettings();

		void ApplyPreset(Preset preset);
		bool SettingsMatchPreset(Preset preset) const;

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
		std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> lumPyramidMipSRVs;
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

		// Bokeh DOF.
		std::unique_ptr<imagespace::Texture2D>      dofCoCTex;             // half-res R16F signed CoC
		std::unique_ptr<imagespace::Texture2D>      dofTileTex;            // /16 R16G16F {minCoC, maxCoC} for blur early-out
		std::unique_ptr<imagespace::Texture2D>      dofHalfColor;          // half-res R11G11B10F downsampled scene
		std::unique_ptr<imagespace::Texture2D>      dofNearBlurred;        // half-res R11G11B10F foreground blur output
		std::unique_ptr<imagespace::Texture2D>      dofFarBlurred;         // half-res R11G11B10F background blur output
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
