#pragma once

#include "World/WaterData.h"

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <functional>

namespace cs::engine
{
	struct WaterDataStatus
	{
		std::uint32_t waterCells;
		float cameraCellHeight;
	};

	[[nodiscard]] WaterDataStatus GetWaterDataStatus() noexcept;
	void InstallWaterRippleVisibilityFilter(std::function<bool()> a_suppress);
	void FillWaterData(
		std::array<DirectX::XMFLOAT4, 25>& a_data,
		float& a_systemHeight,
		const DirectX::XMFLOAT3& a_cameraOrigin);
}
