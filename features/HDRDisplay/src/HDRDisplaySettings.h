#pragma once

#include "Settings/SettingsSchema.h"

#include <cstdint>

namespace cs::features::hdr_display
{
	inline constexpr std::uint32_t kPaperWhiteMin = 80;
	inline constexpr std::uint32_t kPaperWhiteMax = 500;
	inline constexpr std::uint32_t kPeakNitsMin = 400;
	inline constexpr std::uint32_t kPeakNitsMax = 10000;

	struct Settings
	{
		// Seeded from the display on first run; the user owns it afterwards.
		bool enableHDR = false;
		std::uint32_t paperWhite = 203;
		std::uint32_t peakNits = 1000;
		float uiBrightness = 1.0f;
		bool autoDetected = false;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enableHDR", "Enable HDR", &Settings::enableHDR }.Help("Outputs HDR10 (PQ, BT.2020). Off keeps the same chain in SDR."),
			settings::Field{ "paperWhite", "Paper white", &Settings::paperWhite, settings::Range{ kPaperWhiteMin, kPaperWhiteMax } }.Help("Brightness in nits of SDR white and the interface."),
			settings::Field{ "peakNits", "Peak brightness", &Settings::peakNits, settings::Range{ kPeakNitsMin, kPeakNitsMax } }.Help("Maximum brightness of your display in nits."),
			settings::Field{ "uiBrightness", "Interface brightness", &Settings::uiBrightness, settings::Range{ 0.5f, 5.0f } }.Help("Interface brightness multiplier. Not used yet."),
			settings::Field{ "autoDetected", "HDR display detected on first run", &Settings::autoDetected } }
	};
}
