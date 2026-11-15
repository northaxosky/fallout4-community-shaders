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
	enum class PrepassExtraWritePhase : std::uint8_t
	{
		kNative,
		kTerrainColor,
		kTerrainData,
		kTerrainOpaque
	};
	inline constexpr std::size_t kTerrainWritePhases = 3;

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
		std::array<PrepassExtraWrite, kTerrainWritePhases> terrain{};
		// Mirrors separate R8 targets, which wrote only where R was written.
		std::uint8_t nativeRCoupled = 0;
	};

	enum class PrepassAuxChannel : std::uint8_t
	{
		kParallaxShadow,  // R, neutral 1
		kHumanSkin,       // G, neutral 0
		kMaterialAO,      // B, written as 1 until a material source claims it
		kCount
	};

	using PrepassExtraTargetId = std::size_t;

	// Load/OnPostPostLoad only; nullopt if the slot is taken or hooks failed.
	[[nodiscard]] std::optional<PrepassExtraTargetId> RegisterPrepassExtraTarget(Feature& a_owner, PrepassExtraTarget a_target);

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

	void WritePrepassExtraTargetTelemetry(telemetry::Sink& a_sink);
	// The target's SRV for a compute consumer; null unless written this frame.
	[[nodiscard]] ID3D11ShaderResourceView* GetPrepassExtraTargetSRV(PrepassExtraTargetId a_id) noexcept;

	// Load/OnPostPostLoad only.
	[[nodiscard]] bool RegisterPrepassAuxChannel(Feature& a_owner, PrepassAuxChannel a_channel, std::function<bool()> a_active);
	[[nodiscard]] ID3D11ShaderResourceView* GetPrepassAuxSRV(PrepassAuxChannel a_channel) noexcept;
}
