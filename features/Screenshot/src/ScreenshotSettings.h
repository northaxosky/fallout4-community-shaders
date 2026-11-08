#pragma once

#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace cs::features::screenshot_settings
{
	inline constexpr std::uint32_t kHdrPngBitDepthMin = 7;
	inline constexpr std::uint32_t kHdrPngBitDepthMax = 16;

	// Centered crops; FO4 has no free-form editor.
	enum class CropAspect : std::uint8_t
	{
		kFullFrame,
		k16x9,
		k21x9,
		k32x9,
		k4x3,
		k1x1
	};

	inline constexpr std::array kCropAspects{
		settings::Choice{ "full_frame", CropAspect::kFullFrame },
		settings::Choice{ "16:9", CropAspect::k16x9 },
		settings::Choice{ "21:9", CropAspect::k21x9 },
		settings::Choice{ "32:9", CropAspect::k32x9 },
		settings::Choice{ "4:3", CropAspect::k4x3 },
		settings::Choice{ "1:1", CropAspect::k1x1 }
	};

	struct Settings
	{
		std::string screenshotPath = "Screenshots";
		bool applyCropToScreenshot = true;
		CropAspect cropAspect = CropAspect::kFullFrame;
		bool captureWithoutInterface = false;
		std::uint32_t hdrPngBitDepth = 11;
		bool fixDiscordHdr = true;
		bool copyToClipboard = true;
		bool disableVanillaScreenshot = true;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "ScreenshotPath", "Folder", &Settings::screenshotPath }.Help("Relative paths resolve against the game folder; absolute paths save there directly."),
			settings::Field{ "ApplyCropToScreenshot", "Apply crop", &Settings::applyCropToScreenshot }.Help("Crops the saved screenshot to the aspect ratio below."),
			settings::ChoiceField{ "CropAspect", "Crop aspect ratio", &Settings::cropAspect, kCropAspects }.Help("Centered crop of the captured frame."),
			settings::Field{ "CaptureWithoutInterface", "Capture without interface", &Settings::captureWithoutInterface }.Help("Saves the scene without the HUD and menus."),
			settings::Field{ "HdrPngBitDepth", "HDR PNG bit depth", &Settings::hdrPngBitDepth, settings::Range{ kHdrPngBitDepthMin, kHdrPngBitDepthMax } }.Help("Quantization of the HDR PNG; higher values grow the file with diminishing returns."),
			settings::Field{ "FixDiscordHdr", "Fix Discord HDR screenshots", &Settings::fixDiscordHdr }.Help("Removes the cICP tag from HDR PNGs, which Discord shows washed out; the ICC profile still describes the HDR color space."),
			settings::Field{ "CopyToClipboard", "Copy saved file to clipboard", &Settings::copyToClipboard }.Help("Places the saved file on the clipboard to paste in Explorer or chat apps."),
			settings::Field{ "DisableVanillaScreenshot", "Disable vanilla screenshot", &Settings::disableVanillaScreenshot }.Help("Print Screen saves only this feature's file, not the game's own.") }
	};

	struct AspectRatio
	{
		std::uint32_t width;
		std::uint32_t height;
	};

	[[nodiscard]] constexpr AspectRatio Ratio(CropAspect a_aspect) noexcept
	{
		switch (a_aspect) {
		case CropAspect::k16x9:
			return { 16, 9 };
		case CropAspect::k21x9:
			return { 21, 9 };
		case CropAspect::k32x9:
			return { 32, 9 };
		case CropAspect::k4x3:
			return { 4, 3 };
		case CropAspect::k1x1:
			return { 1, 1 };
		default:
			return { 0, 0 };
		}
	}

	struct PixelRegion
	{
		std::uint32_t x = 0;
		std::uint32_t y = 0;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
	};

	// Largest centered region of the aspect; even edges keep chroma aligned.
	[[nodiscard]] constexpr PixelRegion CropRegion(CropAspect a_aspect, std::uint32_t a_width, std::uint32_t a_height) noexcept
	{
		const auto [ratioWidth, ratioHeight] = Ratio(a_aspect);
		PixelRegion region{ 0, 0, a_width, a_height };
		if (ratioWidth == 0 || a_width == 0 || a_height == 0)
			return region;
		// Cross products: the frame is wider than the ratio when w * rh > h * rw.
		if (static_cast<std::uint64_t>(a_width) * ratioHeight > static_cast<std::uint64_t>(a_height) * ratioWidth)
			region.width = static_cast<std::uint32_t>(static_cast<std::uint64_t>(a_height) * ratioWidth / ratioHeight);
		else
			region.height = static_cast<std::uint32_t>(static_cast<std::uint64_t>(a_width) * ratioHeight / ratioWidth);
		region.width = std::max(2u, region.width & ~1u);
		region.height = std::max(2u, region.height & ~1u);
		region.x = (a_width - region.width) / 2 & ~1u;
		region.y = (a_height - region.height) / 2 & ~1u;
		return region;
	}
}
