#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "CaptureService.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <renderdoc_app.h>
#include <stdexcept>
#include <utility>

namespace
{
	int failures{};
	std::uint32_t capturedFrames{};
	std::uint32_t completedCaptures{};
	std::vector<std::pair<std::string, std::string>> comments;

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": " << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	void RENDERDOC_CC TriggerCapture() { capturedFrames = 1; }
	void RENDERDOC_CC TriggerMultiFrameCapture(std::uint32_t a_frames) { capturedFrames = a_frames; }
	std::uint32_t RENDERDOC_CC GetNumCaptures() { return completedCaptures; }
	std::uint32_t RENDERDOC_CC GetCapture(std::uint32_t a_index, char* a_filename,
		std::uint32_t* a_length, std::uint64_t*)
	{
		if (a_index >= completedCaptures)
			return 0;
		const auto path = std::to_string(a_index) + ".rdc";
		if (a_filename)
			std::memcpy(a_filename, path.c_str(), path.size() + 1);
		*a_length = static_cast<std::uint32_t>(path.size() + 1);
		return 1;
	}
	void RENDERDOC_CC SetComments(const char* a_path, const char* a_comments)
	{
		comments.emplace_back(a_path, a_comments);
	}

	void TestCaptureCompletion()
	{
		RENDERDOC_API_1_7_0 api{};
		api.TriggerCapture = TriggerCapture;
		api.TriggerMultiFrameCapture = TriggerMultiFrameCapture;
		api.GetNumCaptures = GetNumCaptures;
		api.GetCapture = GetCapture;
		api.SetCaptureFileComments = SetComments;
		cs::renderdoc::CaptureService service;
		// An attached runtime may already have captures; those must not be overwritten.
		completedCaptures = 1;
		service.Attach(&api);
		service.Poll("metadata");
		CHECK(comments.empty());

		service.Trigger(0);
		CHECK(capturedFrames == 1);
		service.Trigger(121);
		CHECK(capturedFrames == 120);
		service.Trigger(3);
		CHECK(capturedFrames == 3);
		service.QueueComments("metadata\nUser Comments:\ninspect shadows");
		service.Poll("metadata");
		CHECK(comments.empty());
		completedCaptures += 3;
		service.Poll("metadata");
		const std::vector<std::pair<std::string, std::string>> expected{
			{ "1.rdc", "metadata\nUser Comments:\ninspect shadows" },
			{ "2.rdc", "metadata" },
			{ "3.rdc", "metadata" }
		};
		CHECK(comments == expected);
		service.Poll("metadata");
		CHECK(comments.size() == 3);
		++completedCaptures;
		service.Poll("metadata");
		CHECK(comments.back() == std::pair(std::string("4.rdc"), std::string("metadata")));
		CHECK(service.Count() == 5);
	}

	void TestDiskAndInventory(const std::filesystem::path& a_directory)
	{
		using namespace cs::renderdoc;
		CHECK(RequiredSpaceBytes(0) == 256ULL * 1024 * 1024);
		CHECK(RequiredSpaceBytes(120) == 30ULL * 1024 * 1024 * 1024);
		CHECK(RequiredSpaceBytes(121) == RequiredSpaceBytes(120));

		std::filesystem::create_directories(a_directory / "nested");
		const auto first = a_directory / "first.rdc";
		const auto second = a_directory / "runtime.log";
		std::ofstream(first, std::ios::binary) << "abc";
		std::ofstream(second, std::ios::binary) << "defgh";
		std::ofstream(a_directory / "nested" / "ignored.rdc") << "not a top-level capture";
		const auto timestamp = std::filesystem::file_time_type::clock::now();
		std::filesystem::last_write_time(first, timestamp - std::chrono::seconds(10));
		std::filesystem::last_write_time(second, timestamp);
		CaptureService service;
		service.SetDirectory(a_directory);
		CHECK(service.DiskUsageBytes() == 8);
		const auto inventory = service.Inventory();
		CHECK(inventory.size() == 2 && inventory.front().path == second);

		const HANDLE locked = CreateFileW(second.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (locked == INVALID_HANDLE_VALUE)
			throw std::runtime_error("Could not lock the capture file for the deletion failure test");
		bool rejected{};
		try {
			service.ClearCaptures();
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		CHECK(rejected);
		const auto afterFailure = service.Inventory();
		CHECK(afterFailure.size() == 1 && !afterFailure.front().deletionError.empty());
		CHECK(!std::filesystem::exists(first) && std::filesystem::exists(second));
		CloseHandle(locked);
		service.ClearCaptures();
		CHECK(service.Inventory().empty());
		CHECK(std::filesystem::exists(a_directory / "nested" / "ignored.rdc"));

		service.SetDirectory(first);
		std::ofstream(first) << "not a directory";
		rejected = false;
		try {
			(void)service.HasSufficientDiskSpace(1);
		} catch (const std::filesystem::filesystem_error&) {
			rejected = true;
		}
		CHECK(rejected);
	}
}

int main(int a_argc, char** a_argv)
{
	if (a_argc != 2) {
		std::cerr << "Expected an isolated test directory\n";
		return 1;
	}
	const std::filesystem::path directory = a_argv[1];
	std::filesystem::remove_all(directory);
	try {
		TestCaptureCompletion();
		TestDiskAndInventory(directory);
	} catch (const std::exception& error) {
		std::cerr << "Unexpected exception: " << error.what() << '\n';
		++failures;
	}
	std::error_code ignored;
	std::filesystem::remove_all(directory, ignored);
	if (failures)
		return 1;
	std::cout << "RenderDoc capture service tests passed\n";
	return 0;
}
