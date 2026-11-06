#pragma once

#include "Settings/SettingsSchema.h"

#include <cstdint>

namespace cs::features::hdr_display
{
	inline constexpr std::uint32_t kPaperWhiteMin = 80;
	inline constexpr std::uint32_t kPaperWhiteMax = 500;
	inline constexpr std::uint32_t kPeakNitsMin = 400;
	inline constexpr std::uint32_t kPeakNitsMax = 10000;

	// How the gamma LUT grades values above 1; an A/B dev option for now.
	enum class LutMethod : std::uint32_t
	{
		kRadial,
		kGradeTransfer,
		kCount
	};

	struct Settings
	{
		// Seeded from the display on first run; the user owns it afterwards.
		bool enableHDR = false;
		std::uint32_t paperWhite = 203;
		std::uint32_t peakNits = 1000;
		float uiBrightness = 1.0f;
		LutMethod lutMethod = LutMethod::kRadial;
		bool autoDetected = false;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enableHDR", "Enable HDR", &Settings::enableHDR }.Help("Outputs HDR10 (PQ, BT.2020). Off keeps the same chain in SDR."),
			settings::Field{ "paperWhite", "Paper white", &Settings::paperWhite, settings::Range{ kPaperWhiteMin, kPaperWhiteMax } }.Help("Brightness in nits of SDR white and the interface."),
			settings::Field{ "peakNits", "Peak brightness", &Settings::peakNits, settings::Range{ kPeakNitsMin, kPeakNitsMax } }.Help("Maximum brightness of your display in nits."),
			settings::Field{ "uiBrightness", "Interface brightness", &Settings::uiBrightness, settings::Range{ 0.5f, 5.0f } }.Help("Interface brightness multiplier."),
			settings::Field{ "lutMethod", "Color grade LUT above white (dev A/B)", &Settings::lutMethod, settings::Range{ LutMethod::kRadial, LutMethod::kGradeTransfer } }.Help("Radial scales each pixel into the LUT's range. Grade transfer applies the LUT to the SDR range and carries its change onto the highlights."),
			settings::Field{ "autoDetected", "HDR display detected on first run", &Settings::autoDetected } }
	};
}
