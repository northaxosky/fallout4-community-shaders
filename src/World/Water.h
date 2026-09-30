#pragma once

#include "World/WaterData.h"

#include <DirectXMath.h>
#include <array>
#include <cstdint>

namespace cs::engine
{
	struct WaterDataStatus
	{
		std::uint32_t waterCells;
		float cameraCellHeight;
	};

	[[nodiscard]] WaterDataStatus GetWaterDataStatus() noexcept;
	void FillWaterData(
		std::array<DirectX::XMFLOAT4, 25>& a_data,
		float& a_systemHeight,
		const DirectX::XMFLOAT3& a_cameraOrigin);
}
