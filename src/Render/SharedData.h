#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace cs::render
{
	// Reserved for the shared substrate on every stage an injection contributor activates.
	inline constexpr std::uint32_t kSharedDataSlot = 5;
	inline constexpr std::uint32_t kFeatureDataSlot = 6;
	static_assert(kFeatureDataSlot == kSharedDataSlot + 1);

	// Creates and seeds b5/b6; call once during D3D11 bootstrap.
	void InitializeSharedData(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
	bool IsSharedDataReady() noexcept;

	// Installs the per-frame update; startup thread only.
	void EnsureSharedDataUpdateInstalled();

	// Binds b5/b6 without mapping; safe from per-draw dispatch.
	void BindSharedData(ID3D11DeviceContext* a_context) noexcept;
}
