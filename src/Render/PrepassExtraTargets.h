#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <dxgiformat.h>

struct ID3D11ShaderResourceView;

namespace cs::telemetry
{
	class Sink;
}

namespace cs
{
	class Feature;
}

namespace cs::engine
{
	// How a draw writes the extra targets: native states map to themselves, a terrain
	// replay phase overrides them.
	enum class PrepassExtraWritePhase : std::uint8_t
	{
		kNative,
		kTerrainColor,
		kTerrainData,
		kTerrainOpaque
	};
	inline constexpr std::size_t kTerrainWritePhases = 3;

	// One target's write under a terrain replay phase.
	struct PrepassExtraWrite
	{
		bool blend = false;  // lerp by source alpha; otherwise replace
		std::uint8_t mask = 0;
	};

	// A feature-owned target the deferred prepass writes beside the G-buffer.
	struct PrepassExtraTarget
	{
		std::string name;                          // debug-name prefix
		std::uint32_t rtvSlot = 0;                 // SV_Target index, past the engine's six
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;  // screen-sized, render target and SRV
		std::array<float, 4> clear{};
		std::optional<std::uint32_t> publishSlot;  // pixel SRV slot bound after the prepass
		std::function<bool()> active;              // evaluated once per prepass start
		// Indexed by terrain phase (color, data, opaque).
		std::array<PrepassExtraWrite, kTerrainWritePhases> terrain{};
	};

	using PrepassExtraTargetId = std::size_t;

	// Load/OnPostPostLoad only; nullopt if the slot is taken or hooks failed.
	[[nodiscard]] std::optional<PrepassExtraTargetId> RegisterPrepassExtraTarget(Feature& a_owner, PrepassExtraTarget a_target);

	// The phase the draws made inside the scope write under; render thread only.
	class ScopedPrepassExtraWritePhase
	{
	public:
		explicit ScopedPrepassExtraWritePhase(PrepassExtraWritePhase a_phase) noexcept;
		~ScopedPrepassExtraWritePhase() noexcept;
		ScopedPrepassExtraWritePhase(const ScopedPrepassExtraWritePhase&) = delete;
		ScopedPrepassExtraWritePhase& operator=(const ScopedPrepassExtraWritePhase&) = delete;

	private:
		PrepassExtraWritePhase _previous;
	};

	// Derived blend states, binds and the descriptors seen, for the telemetry census.
	void WritePrepassExtraTargetTelemetry(telemetry::Sink& a_sink);
	// The target's SRV for a compute consumer; null unless written this frame.
	[[nodiscard]] ID3D11ShaderResourceView* GetPrepassExtraTargetSRV(PrepassExtraTargetId a_id) noexcept;
}
