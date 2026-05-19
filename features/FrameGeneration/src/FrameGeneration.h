#pragma once

#include "Buffer.h"

#include "SimpleIni.h"
#include "Feature.h"

namespace cs::features
{

class FrameGeneration : public cs::Feature
{
public:
	enum class FrameGenType : int { kFSR3 = 0, kDLSSG = 1, kXeSSFG = 2 };

	static FrameGeneration* GetSingleton()
	{
		static FrameGeneration singleton;
		return &singleton;
	}

		std::string_view GetName() const override { return "FrameGeneration"; }
		void Load() override;
		void OnPostPostLoad() override;
		void DrawSettings() override;

	struct Settings
	{
		bool frameGenerationMode = true;
		bool frameLimitMode = true;
		bool disableInMenus = true;  ///< Disable frame generation in menus to avoid interpolation artifacts
		bool debugLogging = false;
		int frameGenType = 0;    // 0=FSR3, 1=DLSS-G, 2=XeSS-FG
		int frameGenFrames = 1;  // 1=2x, 2=3x, 3=4x (MFG, RTX 50+ only)
	};

	Settings settings;

	bool highFPSPhysicsFixLoaded = false;

	FrameGenType activeFrameGenType = FrameGenType::kFSR3;
	bool d3d12Interop = false;
	double refreshRate = 0.0f;

	framegeneration::Texture2D* HUDLessBufferShared[2];
	framegeneration::Texture2D* depthBufferShared[2];
	framegeneration::Texture2D* motionVectorBufferShared[2];
	framegeneration::Texture2D* UIAlphaBufferShared[2];

	winrt::com_ptr<ID3D12Resource> HUDLessBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> depthBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> motionVectorBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> UIAlphaBufferShared12[2];

	ID3D11ComputeShader* copyDepthToSharedBufferCS = nullptr;
	ID3D11ComputeShader* generateSharedBuffersCS = nullptr;
	ID3D11ComputeShader* uiAlphaMaskCS = nullptr;

	bool setupBuffers = false;

	void LoadSettings();
	void SaveSettings();
	void ReloadSettingsIfNeeded();

	void CreateFrameGenerationResources();
	void PreAlpha();
	void PostAlpha();
	void CopyBuffersToSharedResources();
	void GenerateUIAlphaMask();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter(bool a_useFrameGeneration);

	void GameFrameLimiter();

	static double GetRefreshRate(HWND a_window);

	void PostDisplay();

	void Reset();

	static void InstallHooks();
};

}
