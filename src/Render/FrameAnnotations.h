#pragma once

#include <cstdint>

namespace RE
{
	class BSShader;
}

namespace cs::render::frame_annotations
{
	// Hooks exist only if on at startup; Enabled() gates per-draw events.
	void Install();
	void OnDataLoaded();

	[[nodiscard]] bool Installed() noexcept;
	// The saved setting; differs from Installed() until the next launch.
	[[nodiscard]] bool Requested() noexcept;
	// Saves the setting and, once installed, applies it to the live flag.
	bool SetRequested(bool a_requested);

	// Capture tools hold a ScopedForce so every event emits.
	[[nodiscard]] bool Enabled() noexcept;

	class ScopedForce
	{
	public:
		ScopedForce() noexcept;
		~ScopedForce() noexcept;

		ScopedForce(const ScopedForce&) = delete;
		ScopedForce(ScopedForce&&) = delete;
		ScopedForce& operator=(const ScopedForce&) = delete;
		ScopedForce& operator=(ScopedForce&&) = delete;
	};

	void OnTechniqueBound(const RE::BSShader* a_shader, std::uint32_t a_pixelShaderId);
}
