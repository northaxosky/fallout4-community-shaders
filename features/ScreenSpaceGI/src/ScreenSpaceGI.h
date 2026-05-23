#pragma once

#include "Buffer.h"
#include "Feature.h"

#include <array>
#include <memory>

namespace cs::features
{
	class ScreenSpaceGI : public Feature
	{
	public:
		static ScreenSpaceGI* GetSingleton();

		std::string_view GetName() const override { return "ScreenSpaceGI"; }

		void Load() override;
		void DrawSettings() override;

		bool ParticipatesInPresets() const override { return true; }
		bool IsInTestMode() const override { return testModeActive; }
		std::string GetPresetKey() const override { return "screen_space_gi"; }
		bool StageFromPreset(const toml::table& a_subtable, const cs::PresetApplyContext& a_ctx, std::string& a_err) override;
		void CommitStaged() override;
		void ExportToPreset(toml::table& a_subtable) override;

		// Builds depth pyramid + AO mask. Post-call thunk on DrawWorld::DeferredPrePass.
		void DrawAO();

		// Multiplies AO mask into kDiffuseBuffer. Post-call thunk on DrawWorld::DeferredLightsImpl.
		void Apply();

		enum class Preset : int
		{
			kCustom      = 0,
			kPerformance = 1,
			kQuality     = 2,
			kCinematic   = 3,
		};

		struct Settings
		{
			bool  enabled       = true;
			int   preset        = static_cast<int>(Preset::kQuality);

			int   sliceCount    = 3;
			int   stepCount     = 5;
			float aoRadius      = 200.0f;
			float aoIntensity   = 1.0f;
			float aoPower       = 2.0f;
			float thickness     = 32.0f;

			bool  applyToScene  = false;
			float applyContrast = 1.0f;

			bool  showPreview   = false;
			float previewScale  = 0.30f;
		};

		Settings settings;

	private:
		ScreenSpaceGI() = default;

		void LoadSettings();
		void SaveSettings();

		void ApplyPreset(Preset preset);
		bool SettingsMatchPreset(Preset preset) const;

		bool EnsurePyramid(uint32_t a_w, uint32_t a_h);
		bool EnsureAOResources(uint32_t a_w, uint32_t a_h);
		bool EnsureApplyResources(uint32_t a_w, uint32_t a_h, uint32_t a_format);

		ID3D11ComputeShader* GetCS(const wchar_t* a_path, ID3D11ComputeShader*& a_slot, const char* a_name);

		// Depth pyramid (R16F, 5 mips, half-res base).
		std::unique_ptr<ssgi::Texture2D>     depthPyramid;
		std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> depthMipUAVs;
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 5>  depthMipSRVs;
		std::unique_ptr<ssgi::ConstantBuffer> pyramidCB;
		ID3D11ComputeShader*                 prefilterDepthsCS = nullptr;

		// AO output (R8_UNORM, half-res).
		std::unique_ptr<ssgi::Texture2D>      aoTexture;
		std::unique_ptr<ssgi::ConstantBuffer> aoCB;
		ID3D11ComputeShader*                  aoCS = nullptr;
		winrt::com_ptr<ID3D11SamplerState>    pointClampSampler;
		winrt::com_ptr<ID3D11SamplerState>    linearClampSampler;

		// Apply pass.
		std::unique_ptr<ssgi::Texture2D>      scratchDiffuse;
		std::unique_ptr<ssgi::ConstantBuffer> applyCB;
		ID3D11ComputeShader*                  applyCS = nullptr;

		// Cached dim/format watchdogs.
		uint32_t aoWidth   = 0;
		uint32_t aoHeight  = 0;
		uint32_t pyrWidth  = 0;
		uint32_t pyrHeight = 0;
		uint32_t scratchWidth  = 0;
		uint32_t scratchHeight = 0;
		uint32_t scratchFormat = 0;

		bool testModeActive    = false;
		bool firstFireLogged   = false;
		bool enbWarningLogged  = false;

		Settings stagedSettings;
		bool     stagedValid = false;
	};
}
