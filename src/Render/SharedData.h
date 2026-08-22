#pragma once

#include <array>
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace cs::render
{
	// reserved on every contributed stage
	inline constexpr std::uint32_t kSharedDataSlot = 5;
	inline constexpr std::uint32_t kFeatureDataSlot = 6;
	static_assert(kFeatureDataSlot == kSharedDataSlot + 1);

	void InitializeSharedData(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
	bool IsSharedDataReady() noexcept;

	// last published b5 world-up, for comparison against a captured native row 2
	std::array<float, 4> GetPublishedWorldUpView() noexcept;

	// startup thread only
	void EnsureSharedDataUpdateInstalled();

	void BindSharedData(ID3D11DeviceContext* a_context) noexcept;
}
