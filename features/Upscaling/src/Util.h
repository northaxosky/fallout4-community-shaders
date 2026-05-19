#pragma once

#include <d3d11.h>

#include "Engine.h"

namespace cs::features::upscaling
{

namespace Util
{

	using RenderTarget = cs::engine::RenderTarget;
	using DepthStencilTarget = cs::engine::DepthStencilTarget;

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

	// CommonLibF4 has a 0x30 pad shifting dynamic-res offsets; OG binary reads the no-pad layout.
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
