#pragma once

namespace cs::features::detail
{
	// Defined in Imagespace.cpp; asserts all Imagespace shared-state access is on the render thread.
	void AssertRenderThread(const char* a_where);
}
