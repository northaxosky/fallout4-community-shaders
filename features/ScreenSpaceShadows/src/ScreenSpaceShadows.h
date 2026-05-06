#pragma once

#include "Buffer.h"
#include "Feature.h"

#include <memory>

namespace cs::features
{
	class ScreenSpaceShadows : public Feature
	{
	public:
		static ScreenSpaceShadows* GetSingleton();

		std::string_view GetName() const override { return "ScreenSpaceShadows"; }

		void Load() override;
		void DrawSettings() override;

		// Phase 2 dispatch entry point. Invoked from the post-call thunk on DrawWorld::DeferredPrePass.
		void DrawShadows();

		struct Settings
		{
			bool   enabled = true;
			int    sampleCount = 1;          // [1, 4] multiplier; ports Skyrim CS's GetScaledSampleCount.
			float  surfaceThickness = 0.020f;// Bend SSS recommends 0.005; Skyrim ships 0.020.
			float  bilinearThreshold = 0.020f;
			float  shadowContrast = 1.0f;
			float  previewScale = 0.30f;     // ImGui debug viewer size relative to mask resolution.
			bool   showPreview = true;
		};

		Settings settings;

	private:
		ScreenSpaceShadows() = default;

		void LoadSettings();
		void SaveSettings();

		bool EnsureResources();
		ID3D11ComputeShader* GetRaymarchCS();

		uint32_t GetScaledSampleCount() const;

		std::unique_ptr<sss::Texture2D>     shadowsTexture;
		std::unique_ptr<sss::ConstantBuffer> raymarchCB;
		winrt::com_ptr<ID3D11SamplerState>  pointBorderSampler;
		ID3D11ComputeShader*                raymarchCS = nullptr;
		uint32_t                            lastCompiledSampleCount = 0;
		uint32_t                            shadowsWidth = 0;
		uint32_t                            shadowsHeight = 0;
	};
}
