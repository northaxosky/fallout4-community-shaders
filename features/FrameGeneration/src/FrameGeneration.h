#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "FeatureCategories.h"

#include <atomic>
#include <cstdint>

namespace cs::features
{

class FrameGeneration : public cs::Feature
{
public:
	enum class FrameGenType : int { kFSR3 = 0, kDLSSG = 1, kXeSSFG = 2 };
	enum class FrameGenSkipReason : std::uint8_t
	{
		kNotDecided,
		kActive,
		kUserDisabled,
		kExclusiveFullscreen,
		kNoModule,
		kENBSwapChainOwner,
		kDlssgUpscalerConflict,
		kRenderDoc
	};

	static FrameGeneration* GetSingleton()
	{
		static FrameGeneration singleton;
		return &singleton;
	}

	std::string_view GetName() const override { return "FrameGeneration"; }
	std::string GetFeatureSummary() const override { return "Generates intermediate frames via DLSS-G, FSR3-FG, or XeSS-FG to roughly double displayed FPS."; }
	std::string GetCategory() const override { return FeatureCategories::kPerformance; }
	bool Configure(const toml::table& a_config, std::string& a_error) override;
	void Load() override;
	void OnPostPostLoad() override;
	void DrawSettings() override;
	void RestoreDefaultSettings() override;
	bool HasResettableSettings() const override { return true; }
	bool ProducesTelemetry() const override { return true; }
	void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

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
	bool hasCapturedFrame = false;
	std::uint32_t lastCapturedFrameIndex = 0;
	std::uint64_t telemetryLastCapturedFrame = 0;

	void SaveSettings();

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

	void SetFrameGenSkipReason(FrameGenSkipReason a_reason) noexcept
	{
		_frameGenSkipReason.store(a_reason, std::memory_order_relaxed);
	}

	FrameGenSkipReason GetFrameGenSkipReason() const noexcept
	{
		return _frameGenSkipReason.load(std::memory_order_relaxed);
	}

	void SetLastKnownWindowed(bool a_windowed) noexcept
	{
		_wasExclusiveFullscreen.store(!a_windowed, std::memory_order_relaxed);
	}

	bool WasExclusiveFullscreen() const noexcept
	{
		return _wasExclusiveFullscreen.load(std::memory_order_relaxed);
	}

private:
	std::atomic<FrameGenSkipReason> _frameGenSkipReason{ FrameGenSkipReason::kNotDecided };
	std::atomic<bool> _wasExclusiveFullscreen{ false };
};

}
