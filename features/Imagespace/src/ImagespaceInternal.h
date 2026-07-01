#pragma once

namespace cs::features::detail
{
	// Defined in Imagespace.cpp. Debug tripwire asserting that all Imagespace shared-state access
	// happens on the single render thread (RunFrame mid-frame, DrawSettings/preset commits end-frame).
	void AssertRenderThread(const char* a_where);
}
