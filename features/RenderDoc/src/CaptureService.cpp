#include "CaptureService.h"

#include <functional>
#include <renderdoc_app.h>
#include <stdexcept>

namespace cs::renderdoc
{
	// FO4: capture operations stay independent of engine and UI ownership.
	void CaptureService::Attach(RENDERDOC_API_1_7_0* a_api)
	{
		_api = a_api;
		_lastCaptureCount = Count();
	}

	void CaptureService::SetDirectory(std::filesystem::path a_directory)
	{
		_directory = std::move(a_directory);
		_cacheValid = false;
	}

	bool CaptureService::HasSufficientDiskSpace(int a_frames) const
	{
		std::filesystem::create_directories(_directory);
		return std::filesystem::space(_directory).available >= RequiredSpaceBytes(a_frames);
	}

	void CaptureService::Trigger(int a_frames)
	{
		if (!_api)
			throw std::runtime_error("RenderDoc capture API is unavailable");
		const auto frames = static_cast<std::uint32_t>(
			std::clamp(a_frames, kMinCaptureFrameCount, kMaxCaptureFrameCount));
		if (frames == 1)
			_api->TriggerCapture();
		else
			_api->TriggerMultiFrameCapture(frames);
		_cacheValid = false;
	}

	void CaptureService::QueueComments(std::string a_comments)
	{
		_pendingComments = std::move(a_comments);
		_cacheValid = false;
	}

	void CaptureService::Poll(const std::string& a_automaticComments)
	{
		const auto count = Count();
		if (count <= _lastCaptureCount)
			return;

		auto pending = std::move(_pendingComments);
		_pendingComments.clear();
		for (auto index = _lastCaptureCount; index < count; ++index) {
			std::uint32_t length{};
			if (!_api->GetCapture(index, nullptr, &length, nullptr) || !length)
				continue;
			std::vector<char> path(static_cast<std::size_t>(length) + 1, '\0');
			if (!_api->GetCapture(index, path.data(), &length, nullptr))
				continue;
			const auto& comments = index == _lastCaptureCount && !pending.empty() ?
			                           pending :
			                           a_automaticComments;
			_api->SetCaptureFileComments(path.data(), comments.c_str());
		}
		_lastCaptureCount = count;
		_cacheValid = false;
	}

	const std::vector<CaptureFile>& CaptureService::Inventory(bool a_forceRefresh)
	{
		const auto now = std::chrono::steady_clock::now();
		if (!_cacheValid || a_forceRefresh || now - _updated > std::chrono::seconds(5)) {
			_cacheValid = false;
			if (a_forceRefresh)
				_failedDeletions.clear();
			std::vector<CaptureFile> files;
			for (const auto& entry : std::filesystem::directory_iterator(_directory)) {
				if (!entry.is_regular_file())
					continue;
				CaptureFile file{ entry.path(), entry.file_size(), entry.last_write_time(), {} };
				if (const auto failed = _failedDeletions.find(file.path); failed != _failedDeletions.end())
					file.deletionError = failed->second;
				files.push_back(std::move(file));
			}
			std::ranges::sort(files, std::greater{}, &CaptureFile::modified);
			_files = std::move(files);
			_updated = now;
			_cacheValid = true;
		}
		return _files;
	}

	std::uint64_t CaptureService::DiskUsageBytes() const
	{
		std::uint64_t bytes{};
		for (const auto& entry : std::filesystem::directory_iterator(_directory)) {
			if (entry.is_regular_file())
				bytes += entry.file_size();
		}
		return bytes;
	}

	void CaptureService::ClearCaptures()
	{
		// FO4: deletion failures reach the existing host dialog while remaining in the inventory.
		_failedDeletions.clear();
		_cacheValid = false;
		for (const auto& entry : std::filesystem::directory_iterator(_directory)) {
			if (!entry.is_regular_file())
				continue;
			std::error_code error;
			std::filesystem::remove(entry.path(), error);
			if (error)
				_failedDeletions.emplace(entry.path(), error.message());
		}
		if (!_failedDeletions.empty())
			throw std::runtime_error("Some capture files could not be deleted; see Capture Files for details");
	}

	std::uint32_t CaptureService::Count() const
	{
		return _api ? _api->GetNumCaptures() : 0;
	}
}
