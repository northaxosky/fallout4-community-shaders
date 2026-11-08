#pragma once

#include <DirectXTex.h>

#include <filesystem>
#include <string>

namespace cs::features::screenshot_file
{
	struct Job
	{
		DirectX::ScratchImage image;
		std::filesystem::path path;
		// R10G10B10A2 PQ BT.2020 as an HDR10 PNG; otherwise an 8-bit SDR PNG.
		bool hdr = false;
		unsigned hdrPngBitDepth = 11;
		bool fixDiscordHdr = true;
		bool copyToClipboard = true;
	};

	// A game-relative folder made absolute.
	[[nodiscard]] std::filesystem::path ResolveFolder(const std::string& a_folder);

	// CS_YYYY-MM-DD_HH-MM-SS_mmm.png under a game-relative or absolute folder.
	[[nodiscard]] std::filesystem::path BuildPath(const std::string& a_folder);

	// Encoder thread: writes the file; returns the failure, or empty on success.
	[[nodiscard]] std::string Save(Job& a_job);
}
