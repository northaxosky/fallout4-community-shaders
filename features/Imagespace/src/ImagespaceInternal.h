#pragma once

namespace cs::features::detail
{
	// Imagespace shared state is render-thread-only.
	void AssertRenderThread(const char* a_where) noexcept;
}
