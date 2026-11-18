#pragma once

#include <functional>

namespace cs
{
	class Feature;
}

namespace cs::render
{
	using FrameBoundaryCallback = std::function<void()>;

	// Main thread, before Main::Swap; register from Feature::Load only.
	bool RegisterPreSwap(FrameBoundaryCallback a_callback);
	bool RegisterPreSwap(Feature& a_owner, FrameBoundaryCallback a_callback);

	// Main thread, after Main::Swap; dispatcher drains, then registration order.
	bool RegisterPostSwap(FrameBoundaryCallback a_callback);
	bool RegisterPostSwap(Feature& a_owner, FrameBoundaryCallback a_callback);

	// Hooks the Main::OnIdle -> Main::Swap call; independent of any UI host.
	bool InstallFrameBoundary();

	[[nodiscard]] bool FrameBoundaryInstalled() noexcept;
}
