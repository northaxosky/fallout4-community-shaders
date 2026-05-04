#pragma once

#include <d3d11.h>

namespace cs::features::Upscaling
{

namespace Util
{

	enum class RenderTarget
	{
		kFrameBuffer = 0,

		kRefractionNormal = 1,

		kMainPreAlpha = 2,
		kMain = 3,
		kMainTemp = 4,

		kSSRRaw = 7,
		kSSRBlurred = 8,
		kSSRBlurredExtra = 9,

		kMainVerticalBlur = 14,
		kMainHorizontalBlur = 15,

		kSSRDirection = 10,
		kSSRMask = 11,

		kUI = 17,
		kUITemp = 18,

		kGbufferNormal = 20,
		kGbufferNormalSwap = 21,
		kGbufferAlbedo = 22,
		kGbufferEmissive = 23,
		kGbufferMaterial = 24, //  Glossiness, Specular, Backlighting, SSS

		kSSAO = 28,

		kTAAAccumulation = 26,
		kTAAAccumulationSwap = 27,

		kMotionVectors = 29,

		kUIDownscaled = 36,
		kUIDownscaledComposite = 37,

		kMainDepthMips = 39,

		kUnkMask = 57,

		kSSAOTemp = 48,
		kSSAOTemp2 = 49,
		kSSAOTemp3 = 50,

		kDiffuseBuffer = 58,
		kSpecularBuffer = 59,

		kDownscaledHDR = 64,
		kDownscaledHDRLuminance2 = 65,
		kDownscaledHDRLuminance3 = 66,
		kDownscaledHDRLuminance4 = 67,
		kDownscaledHDRLuminance5Adaptation = 68,
		kDownscaledHDRLuminance6AdaptationSwap = 69,
		kDownscaledHDRLuminance6 = 70,

		kCount = 101
	};

	enum class DepthStencilTarget
	{
		kMainOtherOther = 0,
		kMainOther = 1,
		kMain = 2,
		kMainCopy = 3,
		kMainCopyCopy = 4,

		kShadowMap = 8,

		kCount = 13
	};

	[[nodiscard]] static RE::BSGraphics::State* State_GetSingleton()
	{
		REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID({ 600795, 2704621, 2704621 }) };
		return singleton.get();
	}

	[[nodiscard]] static RE::BSGraphics::RenderTargetManager* RenderTargetManager_GetSingleton()
	{
		REL::Relocation<RE::BSGraphics::RenderTargetManager*> singleton{ REL::ID({ 1508457, 2666735, 2666735 }) };
		return singleton.get();
	}

	// CommonLibF4 has a 0x30 pad in RenderTargetManager that shifts dynamicWidthRatio
	// to 0xFB8, but the OG game binary reads from 0xF88 (no pad). We must write to both
	// the struct member (for our code) and the real game offset (for the game's code).
	static constexpr std::ptrdiff_t GAME_DYNAMIC_WIDTH_RATIO_OFFSET = 0xF88;
	static constexpr std::ptrdiff_t GAME_DYNAMIC_HEIGHT_RATIO_OFFSET = 0xF8C;
	static constexpr std::ptrdiff_t GAME_IS_DYNAMIC_RES_ACTIVATED_OFFSET = 0xFA8;

	static void SetDynamicResolution(RE::BSGraphics::RenderTargetManager* rtm, float width, float height, bool activated)
	{
		// Write to struct members (used by our code)
		rtm->dynamicWidthRatio = width;
		rtm->dynamicHeightRatio = height;
		rtm->isDynamicResolutionCurrentlyActivated = activated;

		// Also write to the game's actual offsets (no-pad layout)
		if (REX::FModule::IsRuntimeOG()) {
			auto base = reinterpret_cast<uintptr_t>(rtm);
			*reinterpret_cast<float*>(base + GAME_DYNAMIC_WIDTH_RATIO_OFFSET) = width;
			*reinterpret_cast<float*>(base + GAME_DYNAMIC_HEIGHT_RATIO_OFFSET) = height;
			*reinterpret_cast<bool*>(base + GAME_IS_DYNAMIC_RES_ACTIVATED_OFFSET) = activated;
		}
	}

	static float GetGameDynamicWidthRatio(RE::BSGraphics::RenderTargetManager* rtm)
	{
		if (REX::FModule::IsRuntimeOG()) {
			return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(rtm) + GAME_DYNAMIC_WIDTH_RATIO_OFFSET);
		}
		return rtm->dynamicWidthRatio;
	}

	static float GetGameDynamicHeightRatio(RE::BSGraphics::RenderTargetManager* rtm)
	{
		if (REX::FModule::IsRuntimeOG()) {
			return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(rtm) + GAME_DYNAMIC_HEIGHT_RATIO_OFFSET);
		}
		return rtm->dynamicHeightRatio;
	}

	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const std::vector<std::pair<const char*, const char*>>& Defines, const char* ProgramType, const char* Program = "main");
}

}
