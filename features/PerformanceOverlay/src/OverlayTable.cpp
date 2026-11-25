#include "OverlayTable.h"

#include "Host/HostClient.h"
#include "PerformanceOverlaySettings.h"

#include <cmath>
#include <format>

namespace cs::features::performance_overlay
{
	std::string Milliseconds(float a_value)
	{
		if (std::abs(a_value) < 1e-4f)
			return "0 ms";
		return a_value < 0.1f ? std::format("{:.3f} ms", a_value) : std::format("{:.2f} ms", a_value);
	}

	dmui::TextTone ComparisonTone(float a_left, float a_right)
	{
		return a_left < a_right ? dmui::TextTone::kSuccess : a_left > a_right ? dmui::TextTone::kError :
		                                                                        dmui::TextTone::kInherit;
	}

	void DrawMilliseconds(float a_value, dmui::TextTone a_tone, bool a_microseconds)
	{
		std::string text;
		if (a_microseconds && a_value < Settings::kMicrosecondThreshold)
			text = std::abs(a_value * 1000.0f) < 1e-4f ? "0 us" : std::format("{:.2f} us", a_value * 1000.0f);
		else
			text = Milliseconds(a_value);
		(void)dmui::DrawStyledText(host::HostClient::Get().Client(), text, { .tone = a_tone });
	}

	void DrawDelta(float a_baseline, float a_test)
	{
		const float delta = a_test - a_baseline;
		const float percent = a_baseline > 0.0f ? 100.0f * delta / a_baseline : 0.0f;
		auto text = (delta > 0.0f ? "+" : "") + Milliseconds(delta);
		if (std::abs(percent) >= Settings::kPercentDisplayThreshold)
			text += std::format(" ({:+.1f}%)", percent);
		(void)dmui::DrawStyledText(host::HostClient::Get().Client(), text,
			{ .tone = ComparisonTone(a_test, a_baseline) });
	}
}
