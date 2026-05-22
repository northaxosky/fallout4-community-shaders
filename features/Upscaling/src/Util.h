#pragma once

#include <d3d11.h>

#include "Engine.h"

namespace cs::features::upscaling
{

namespace Util
{

	using RenderTarget = cs::engine::RenderTarget;
	using DepthStencilTarget = cs::engine::DepthStencilTarget;

	// RTM dynamic-resolution field offsets per Fallout4RE exports/cs-rtm-dynamic-res-offsets.json @ a124812.
	// OG has no 0x30 pad at 0xDC4; NG and AE do. CommonLibF4 compiles a unified padded layout that lands at
	// 0xFB8/0xFBC/0xFD8 for the three fields, which is OG-broken and NG/AE-broken-for-isActivated only.
	struct DynamicResOffsets
	{
		std::ptrdiff_t widthRatio;
		std::ptrdiff_t heightRatio;
		std::ptrdiff_t isActivated;
	};

	static constexpr DynamicResOffsets kOffsetsOG{ 0xF88, 0xF8C, 0xFA8 };
	static constexpr DynamicResOffsets kOffsetsNGAE{ 0xFB8, 0xFBC, 0xFE5 };

	[[nodiscard]] inline DynamicResOffsets GetDynamicResOffsets()
	{
		return REX::FModule::IsRuntimeOG() ? kOffsetsOG : kOffsetsNGAE;
	}

	static void SetDynamicResolution(RE::BSGraphics::RenderTargetManager* rtm, float width, float height, bool activated)
	{
		// Write to binary offsets (cross-runtime correct per cs-rtm-dynamic-res-offsets.json).
		const auto off = GetDynamicResOffsets();
		auto base = reinterpret_cast<uintptr_t>(rtm);
		*reinterpret_cast<float*>(base + off.widthRatio)  = width;
		*reinterpret_cast<float*>(base + off.heightRatio) = height;
		*reinterpret_cast<bool*>(base + off.isActivated)  = activated;

		// Also write to CommonLibF4 struct members so existing struct-reader code stays in sync.
		// On NG/AE this is the same memory as off.widthRatio/heightRatio; on OG the struct compiles
		// to padded offsets that don't match the binary, so this write only keeps struct readers fed
		// (do not rely on it for engine-visible state).
		rtm->dynamicWidthRatio = width;
		rtm->dynamicHeightRatio = height;
		rtm->isDynamicResolutionCurrentlyActivated = activated;
	}

	static float GetGameDynamicWidthRatio(RE::BSGraphics::RenderTargetManager* rtm)
	{
		const auto off = GetDynamicResOffsets();
		return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(rtm) + off.widthRatio);
	}

	static float GetGameDynamicHeightRatio(RE::BSGraphics::RenderTargetManager* rtm)
	{
		const auto off = GetDynamicResOffsets();
		return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(rtm) + off.heightRatio);
	}

}

}
