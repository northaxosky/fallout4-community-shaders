#pragma once

#include <dxgi1_6.h>

namespace cs::features::swap_chain_facade
{
	[[nodiscard]] inline DXGI_SWAP_CHAIN_DESC BuildDescription(
		const DXGI_SWAP_CHAIN_DESC& a_requested,
		const DXGI_SWAP_CHAIN_DESC1& a_inner) noexcept
	{
		auto result = a_requested;
		result.BufferDesc.Width = a_inner.Width;
		result.BufferDesc.Height = a_inner.Height;
		result.BufferDesc.Format = a_inner.Format;
		result.SampleDesc = { 1, 0 };
		result.BufferCount = a_inner.BufferCount;
		result.Windowed = TRUE;
		result.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		result.Flags = 0;
		return result;
	}

	[[nodiscard]] inline DXGI_SWAP_CHAIN_DESC1 BuildDescription1(
		const DXGI_SWAP_CHAIN_DESC& a_description) noexcept
	{
		return {
			.Width = a_description.BufferDesc.Width,
			.Height = a_description.BufferDesc.Height,
			.Format = a_description.BufferDesc.Format,
			.Stereo = FALSE,
			.SampleDesc = a_description.SampleDesc,
			.BufferUsage = a_description.BufferUsage,
			.BufferCount = a_description.BufferCount,
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = a_description.SwapEffect,
			.AlphaMode = DXGI_ALPHA_MODE_IGNORE,
			.Flags = a_description.Flags
		};
	}

	[[nodiscard]] inline DXGI_SWAP_CHAIN_FULLSCREEN_DESC
		BuildFullscreenDescription(
			const DXGI_SWAP_CHAIN_DESC& a_description) noexcept
	{
		return {
			.RefreshRate = a_description.BufferDesc.RefreshRate,
			.ScanlineOrdering =
				a_description.BufferDesc.ScanlineOrdering,
			.Scaling = a_description.BufferDesc.Scaling,
			.Windowed = TRUE
		};
	}

	[[nodiscard]] inline bool SupportsResizeBufferCount(
		UINT a_requestedCount,
		const DXGI_SWAP_CHAIN_DESC& a_description) noexcept
	{
		return !a_requestedCount ||
			a_requestedCount == a_description.BufferCount;
	}

	[[nodiscard]] inline UINT PreservePrivateResizeFlags(
		UINT a_requestedFlags,
		const DXGI_SWAP_CHAIN_DESC1& a_inner) noexcept
	{
		constexpr UINT creationOnlyFlags =
			DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
			DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		return a_requestedFlags | (a_inner.Flags & creationOnlyFlags);
	}
}
