#pragma once

#include "Buffer.h"
#include "Feature.h"

#include <memory>

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
			bool   enabled = true;
		};

		Settings settings;

	private:
		Imagespace() = default;

		void LoadSettings();
		void SaveSettings();

		bool EnsureResources();
		ID3D11ComputeShader* GetLumPyramidCS();

		// Per-frame luminance probe: downsample kFrameBuffer to 1x1, CPU-read, log.
		std::unique_ptr<imagespace::Texture2D>     lumProbeTexture;
		std::unique_ptr<imagespace::ConstantBuffer> lumProbeCB;
		ID3D11ComputeShader*                        lumProbeCS = nullptr;

		uint32_t                                    probeWidth = 0;
		uint32_t                                    probeHeight = 0;
		bool                                        firstFireLogged = false;
	};
}
