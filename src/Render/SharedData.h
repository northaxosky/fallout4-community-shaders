#pragma once

#include "FeatureBuffer.h"

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

	// startup thread only
	void EnsureSharedDataUpdateInstalled();

	void BindSharedData(ID3D11DeviceContext* a_context) noexcept;

	enum class FeatureDataOverrideResult
	{
		kUnchanged,
		kWritten,
		kFailed
	};

	FeatureDataOverrideResult BindExtendedTranslucencyFeatureData(
		ID3D11DeviceContext* a_context,
		const ExtendedTranslucencyFeatureData& a_data) noexcept;
}
