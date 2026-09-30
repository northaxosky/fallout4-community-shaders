#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct RENDERDOC_API_1_7_0;

namespace cs::renderdoc
{
	inline constexpr int kMinCaptureFrameCount = 1;
	inline constexpr int kMaxCaptureFrameCount = 120;

	[[nodiscard]] constexpr std::uint64_t RequiredSpaceBytes(int a_frames)
	{
		return std::max(std::uint64_t{ 100 } * 1024 * 1024,
			std::uint64_t{ 256 } * 1024 * 1024 *
				static_cast<std::uint64_t>(std::clamp(a_frames, kMinCaptureFrameCount, kMaxCaptureFrameCount)));
	}

	struct CaptureFile
	{
		std::filesystem::path path;
		std::uint64_t bytes{};
		std::filesystem::file_time_type modified{};
		std::string deletionError;
	};

	class CaptureService
	{
	public:
		void Attach(RENDERDOC_API_1_7_0* a_api);
		void SetDirectory(std::filesystem::path a_directory);
		[[nodiscard]] bool HasSufficientDiskSpace(int a_frames) const;
		void Trigger(int a_frames);
		void QueueComments(std::string a_comments);
		void Poll(const std::string& a_automaticComments);
		const std::vector<CaptureFile>& Inventory(bool a_forceRefresh = false);
		std::uint64_t DiskUsageBytes() const;
		void ClearCaptures();
		[[nodiscard]] std::uint32_t Count() const;

	private:
		RENDERDOC_API_1_7_0* _api{};
		std::filesystem::path _directory;
		std::string _pendingComments;
		std::uint32_t _lastCaptureCount{};
		std::vector<CaptureFile> _files;
		std::unordered_map<std::filesystem::path, std::string> _failedDeletions;
		std::chrono::steady_clock::time_point _updated{};
		bool _cacheValid{};
	};
}
