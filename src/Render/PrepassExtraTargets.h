#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <dxgiformat.h>

struct ID3D11ShaderResourceView;

namespace cs
{
	class Feature;
}

namespace cs::engine
{
	struct ShaderReplacementRegistration;

	// A feature-owned render target the deferred prepass writes beside the G-buffer.
	struct PrepassExtraTarget
	{
		std::string name;                          // debug-name prefix
		std::uint32_t rtvSlot = 0;                 // SV_Target index, past the engine's six
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;  // screen-sized, render target and SRV
		std::array<float, 4> clear{};
		std::optional<std::uint32_t> publishSlot;  // pixel SRV slot bound after the prepass
		std::function<bool()> active;              // evaluated once per prepass start
	};

	using PrepassExtraTargetId = std::size_t;

	// Load/OnPostPostLoad only; nullopt if the slot is taken or hooks failed.
	[[nodiscard]] std::optional<PrepassExtraTargetId> RegisterPrepassExtraTarget(Feature& a_owner, PrepassExtraTarget a_target);
	// Plugs the target bind into a feature's kDeferredPrepass registration.
	void AttachPrepassExtraTargets(ShaderReplacementRegistration& a_registration);
	// The target's SRV for a compute consumer; null unless it was written this frame.
	[[nodiscard]] ID3D11ShaderResourceView* GetPrepassExtraTargetSRV(PrepassExtraTargetId a_id) noexcept;
}
