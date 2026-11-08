#include "ScreenshotFile.h"

#include "Log.h"

#include <ShlObj.h>
#include <malloc.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <string_view>

#pragma warning(push, 0)
#define SK_HDR_PNG_COMMUNITY_SHADERS
#include <sk_hdr_png.hpp>
#pragma warning(pop)

namespace cs::features::screenshot_file
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.screenshot.file");

		// Relative folders must be absolute for CF_HDROP and chat apps.
		std::filesystem::path ResolveToAbsoluteGamePath(const std::filesystem::path& a_path)
		{
			if (a_path.is_absolute())
				return a_path;
			wchar_t buffer[MAX_PATH]{};
			const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
			if (length > 0 && length < MAX_PATH)
				return std::filesystem::path(buffer).parent_path() / a_path;
			std::error_code ec;
			return std::filesystem::absolute(a_path, ec);
		}

		bool CopyFilePathToClipboard(const std::wstring& a_absolutePath)
		{
			const size_t bytes = sizeof(DROPFILES) + (a_absolutePath.size() + 2) * sizeof(wchar_t);
			HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
			if (!memory)
				return false;
			auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
			if (!drop) {
				GlobalFree(memory);
				return false;
			}
			drop->pFiles = sizeof(DROPFILES);
			drop->fWide = TRUE;
			std::memcpy(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES), a_absolutePath.c_str(),
				(a_absolutePath.size() + 1) * sizeof(wchar_t));
			GlobalUnlock(memory);

			// Another process may hold the clipboard for a moment.
			for (int attempt = 0; attempt < 8; ++attempt) {
				if (attempt > 0)
					Sleep(1 << (attempt - 1));
				if (!OpenClipboard(nullptr))
					continue;
				EmptyClipboard();
				const bool placed = SetClipboardData(CF_HDROP, memory) != nullptr;
				CloseClipboard();
				if (placed)
					return true;
			}
			GlobalFree(memory);
			return false;
		}

		void CopyToClipboard(const std::filesystem::path& a_path)
		{
			std::error_code ec;
			const auto size = std::filesystem::file_size(a_path, ec);
			if (ec || size == 0) {
				L->warn("Screenshot is missing or empty, skipping clipboard: {}", a_path.string());
				return;
			}
			if (!CopyFilePathToClipboard(a_path.wstring()))
				L->warn("Screenshot saved but the clipboard copy failed.");
		}

		bool WriteFileAtomically(const std::filesystem::path& a_path, std::string_view a_content)
		{
			auto temporary = a_path;
			temporary += L".tmp";
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				if (!file.write(a_content.data(), static_cast<std::streamsize>(a_content.size())))
					return false;
			}
			std::error_code ec;
			std::filesystem::rename(temporary, a_path, ec);
			if (ec)
				std::filesystem::remove(temporary, ec);
			return !ec;
		}

		// Discord misreads cICP, while the iCCP profile still describes the HDR color space.
		bool StripPngCicpChunk(const std::filesystem::path& a_path)
		{
			constexpr size_t kSignatureSize = 8;
			constexpr size_t kTypeOffset = 4;
			constexpr size_t kChunkOverhead = 12;  // length + type + CRC

			std::error_code ec;
			const auto fileSize = std::filesystem::file_size(a_path, ec);
			if (ec)
				return false;
			std::string data(fileSize, '\0');
			{
				std::ifstream file(a_path, std::ios::binary);
				if (!file.read(data.data(), static_cast<std::streamsize>(data.size())))
					return false;
			}
			for (size_t offset = kSignatureSize; offset + kChunkOverhead <= data.size();) {
				uint32_t length;
				std::memcpy(&length, &data[offset], sizeof(length));
				const size_t chunkSize = kChunkOverhead + _byteswap_ulong(length);
				const auto* type = &data[offset + kTypeOffset];
				// cICP precedes IDAT, so the search ends there.
				if (offset + chunkSize > data.size() || std::memcmp(type, "IDAT", 4) == 0)
					return false;
				if (std::memcmp(type, "cICP", 4) == 0) {
					data.erase(offset, chunkSize);
					return WriteFileAtomically(a_path, data);
				}
				offset += chunkSize;
			}
			return false;
		}

		struct AlignedFree
		{
			void operator()(void* a_memory) const noexcept { _aligned_free(a_memory); }
		};

		bool SaveHdrPng(const Job& a_job)
		{
			const auto* image = a_job.image.GetImage(0, 0, 0);
			if (!image || image->format != DXGI_FORMAT_R10G10B10A2_UNORM)
				return false;
			// sk_hdr_png needs tightly packed, 16-byte aligned pixels.
			constexpr size_t kBytesPerPixel = 4;
			const size_t rowBytes = image->width * kBytesPerPixel;
			std::unique_ptr<uint8_t, AlignedFree> packed(static_cast<uint8_t*>(_aligned_malloc(rowBytes * image->height, 16)));
			if (!packed)
				return false;
			for (size_t row = 0; row < image->height; ++row)
				std::memcpy(packed.get() + row * rowBytes, image->pixels + row * image->rowPitch, rowBytes);
			return sk_hdr_png::write_image_to_disk(
				a_job.path.wstring().c_str(),
				static_cast<unsigned>(image->width), static_cast<unsigned>(image->height),
				packed.get(), static_cast<int>(a_job.hdrPngBitDepth),
				sk_hdr_png::format::r10g10b10a2_unorm, false);
		}

		bool SaveSdrPng(const Job& a_job)
		{
			const auto* source = a_job.image.GetImage(0, 0, 0);
			if (!source)
				return false;
			// PNG drops alpha, which the interface layer leaves undefined.
			DirectX::ScratchImage converted;
			const DirectX::Image* image = source;
			if (source->format != DXGI_FORMAT_B8G8R8X8_UNORM) {
				if (FAILED(DirectX::Convert(*source, DXGI_FORMAT_B8G8R8X8_UNORM, DirectX::TEX_FILTER_DEFAULT, 0.0f, converted)))
					return false;
				image = converted.GetImage(0, 0, 0);
			}
			return SUCCEEDED(DirectX::SaveToWICFile(
				*image, DirectX::WIC_FLAGS_FORCE_SRGB, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), a_job.path.c_str()));
		}
	}

	std::filesystem::path ResolveFolder(const std::string& a_folder)
	{
		return ResolveToAbsoluteGamePath(std::filesystem::path(a_folder));
	}

	std::filesystem::path BuildPath(const std::string& a_folder)
	{
		SYSTEMTIME time;
		GetLocalTime(&time);
		char name[48];
		snprintf(name, sizeof(name), "CS_%04d-%02d-%02d_%02d-%02d-%02d_%03d.png",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
		return ResolveFolder(a_folder) / name;
	}

	std::string Save(Job& a_job)
	{
		std::error_code ec;
		std::filesystem::create_directories(a_job.path.parent_path(), ec);
		if (!(a_job.hdr ? SaveHdrPng(a_job) : SaveSdrPng(a_job)))
			return std::string("Could not write ") + (a_job.hdr ? "the HDR PNG" : "the PNG");
		if (a_job.hdr && a_job.fixDiscordHdr && !StripPngCicpChunk(a_job.path))
			L->warn("Could not strip the cICP chunk from the HDR PNG.");
		if (a_job.copyToClipboard)
			CopyToClipboard(a_job.path);
		return {};
	}
}
