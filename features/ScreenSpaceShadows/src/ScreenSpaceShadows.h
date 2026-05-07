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

		// Writes the screen-space shadow mask. Runs in the post-call thunk on DrawWorld::DeferredPrePass.
		void DrawShadows();

		// Multiplies the mask into kDiffuseBuffer with N.L gating against the sun. Runs in the post-call thunk on DrawWorld::DeferredLightsImpl.
		void Apply();

		struct Settings
		{
			bool   enabled = true;
			int    sampleCount = 1;
			float  surfaceThickness = 0.020f;
			float  bilinearThreshold = 0.020f;
			float  shadowContrast = 1.0f;
			float  previewScale = 0.30f;
			bool   showPreview = true;

			bool   applyToScene = false;    // Off by default until visual validation passes; turn on in the menu to test.
			bool   sunOnly = true;          // Gate apply by N.L vs sun; off = global multiply.
			float  applyContrast = 1.0f;    // Apply-pass strength; 0 = no apply, 1 = full mask multiply.
		};

		Settings settings;

	private:
		ScreenSpaceShadows() = default;

		void LoadSettings();
		void SaveSettings();

		bool EnsureResources();
		bool EnsureApplyResources();
		ID3D11ComputeShader* GetRaymarchCS();
		ID3D11ComputeShader* GetApplyCS();

		uint32_t GetScaledSampleCount() const;

		// Returns the world-space sun direction (normalized) on success.
		bool GetSunDirectionWS(float& outX, float& outY, float& outZ) const;

		// Mask compute resources
		std::unique_ptr<sss::Texture2D>      shadowsTexture;
		std::unique_ptr<sss::ConstantBuffer> raymarchCB;
		winrt::com_ptr<ID3D11SamplerState>   pointBorderSampler;
		ID3D11ComputeShader*                 raymarchCS = nullptr;
		uint32_t                             lastCompiledSampleCount = 0;
		uint32_t                             shadowsWidth = 0;
		uint32_t                             shadowsHeight = 0;

		// Apply-pass resources
		std::unique_ptr<sss::Texture2D>      scratchDiffuse;
		std::unique_ptr<sss::ConstantBuffer> applyCB;
		ID3D11ComputeShader*                 applyCS = nullptr;
		DXGI_FORMAT                          diffuseBufferFormat = DXGI_FORMAT_UNKNOWN;
		uint32_t                             scratchWidth = 0;
		uint32_t                             scratchHeight = 0;
		bool                                 enbWarningLogged = false;
		bool                                 gbufferFormatLogged = false;
	};
}
