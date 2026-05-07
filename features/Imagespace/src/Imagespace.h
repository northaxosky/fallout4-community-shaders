#pragma once

#include "Buffer.h"
#include "Feature.h"

#include <memory>
#include <string>

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

		// Per-frame entry point. Hooks fire from the post-call thunk on
		// Imagespace_SetUseDynamicResolutionViewportAsDefaultViewport.
		void RunFrame();

		struct Settings
		{
			bool        enabled       = true;
			int         iOperator     = 1;       // 0=Off, 1=Hable, 2=Reinhard, 3=Lottes
			float       fExposure     = 1.0f;
			bool        bLUTEnable    = false;
			std::string sLUTPath      = "";
			float       fLUTStrength  = 1.0f;
		};

		Settings settings;

	private:
		Imagespace() = default;

		void LoadSettings();
		void SaveSettings();

		bool EnsureResources();
		bool EnsureCompositeResources(uint32_t a_width, uint32_t a_height, uint32_t a_format);
		ID3D11ComputeShader* GetLumPyramidCS();
		ID3D11ComputeShader* GetCompositeCS();
		bool LoadLUTFromDisk(const std::string& a_filename);

		// Per-frame luminance probe: downsample kFrameBuffer to 1x1, CPU-read, log.
		std::unique_ptr<imagespace::Texture2D>      lumProbeTexture;
		std::unique_ptr<imagespace::ConstantBuffer> lumProbeCB;
		ID3D11ComputeShader*                        lumProbeCS = nullptr;

		// Tonemap + LUT composite pass.
		std::unique_ptr<imagespace::Texture2D>      compositeScratch;
		std::unique_ptr<imagespace::ConstantBuffer> compositeCB;
		ID3D11ComputeShader*                        compositeCS = nullptr;
		winrt::com_ptr<ID3D11Texture3D>             lutTexture;
		winrt::com_ptr<ID3D11ShaderResourceView>    lutSRV;
		winrt::com_ptr<ID3D11SamplerState>          lutSampler;
		std::string                                 lutLoadedPath;

		uint32_t                                    scratchWidth  = 0;
		uint32_t                                    scratchHeight = 0;
		uint32_t                                    scratchFormat = 0;

		uint32_t                                    probeWidth = 0;
		uint32_t                                    probeHeight = 0;
		bool                                        firstFireLogged = false;
		bool                                        testModeActive  = false;
	};
}
