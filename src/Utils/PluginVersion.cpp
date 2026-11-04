#include "Utils/PluginVersion.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace cs::util
{
	namespace
	{
		constexpr std::uint64_t kMaxImageSize = 256ull << 20;
		constexpr std::string_view kExport = "F4SEPlugin_Version";

		struct Reader
		{
			std::span<const std::byte> image;

			template <class T>
			std::optional<T> Read(std::uint64_t a_offset) const noexcept
			{
				if (a_offset > image.size() || image.size() - a_offset < sizeof(T))
					return std::nullopt;
				T value;
				std::memcpy(&value, image.data() + a_offset, sizeof(T));
				return value;
			}
		};

		std::optional<std::uint32_t> RvaToOffset(const std::vector<IMAGE_SECTION_HEADER>& a_sections, std::uint32_t a_rva) noexcept
		{
			for (const auto& s : a_sections) {
				const auto extent = std::min(s.Misc.VirtualSize ? s.Misc.VirtualSize : s.SizeOfRawData, s.SizeOfRawData);
				if (a_rva >= s.VirtualAddress && a_rva - s.VirtualAddress < extent)
					return s.PointerToRawData + (a_rva - s.VirtualAddress);
			}
			return std::nullopt;
		}

		template <class Optional>
		std::optional<std::uint32_t> ExportRva(const Reader& a_reader, std::uint64_t a_at) noexcept
		{
			const auto header = a_reader.Read<Optional>(a_at);
			if (!header)
				return std::nullopt;
			return header->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
		}
	}

	std::optional<std::uint32_t> ParsePluginVersion(std::span<const std::byte> a_image) noexcept
	{
		try {
			const Reader r{ a_image };
			const auto dos = r.Read<IMAGE_DOS_HEADER>(0);
			if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
				return std::nullopt;
			const std::uint64_t peAt = static_cast<std::uint32_t>(dos->e_lfanew);
			if (r.Read<std::uint32_t>(peAt) != std::uint32_t{ IMAGE_NT_SIGNATURE })
				return std::nullopt;

			const auto file = r.Read<IMAGE_FILE_HEADER>(peAt + sizeof(std::uint32_t));
			if (!file || !file->NumberOfSections || !file->SizeOfOptionalHeader)
				return std::nullopt;
			const std::uint64_t optional = peAt + sizeof(std::uint32_t) + sizeof(IMAGE_FILE_HEADER);
			const auto magic = r.Read<std::uint16_t>(optional);
			std::optional<std::uint32_t> exportRva;
			if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && file->SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER64))
				exportRva = ExportRva<IMAGE_OPTIONAL_HEADER64>(r, optional);
			else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC && file->SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER32))
				exportRva = ExportRva<IMAGE_OPTIONAL_HEADER32>(r, optional);
			if (!exportRva || !*exportRva)
				return std::nullopt;

			std::vector<IMAGE_SECTION_HEADER> sections;
			for (std::uint32_t i = 0; i < file->NumberOfSections; ++i) {
				const auto section = r.Read<IMAGE_SECTION_HEADER>(
					optional + file->SizeOfOptionalHeader + std::uint64_t{ i } * sizeof(IMAGE_SECTION_HEADER));
				if (!section)
					return std::nullopt;
				sections.push_back(*section);
			}

			const auto dirAt = RvaToOffset(sections, *exportRva);
			const auto dir = dirAt ? r.Read<IMAGE_EXPORT_DIRECTORY>(*dirAt) : std::nullopt;
			if (!dir)
				return std::nullopt;
			const auto namesAt = RvaToOffset(sections, dir->AddressOfNames);
			const auto ordinalsAt = RvaToOffset(sections, dir->AddressOfNameOrdinals);
			const auto functionsAt = RvaToOffset(sections, dir->AddressOfFunctions);
			if (!namesAt || !ordinalsAt || !functionsAt)
				return std::nullopt;

			// A crafted count must not outrun the name table the image can hold.
			const std::uint64_t tableCapacity = *namesAt < a_image.size() ? (a_image.size() - *namesAt) / sizeof(std::uint32_t) : 0;
			const auto nameCount = std::min<std::uint64_t>(dir->NumberOfNames, tableCapacity);
			for (std::uint64_t i = 0; i < nameCount; ++i) {
				const auto nameRva = r.Read<std::uint32_t>(std::uint64_t{ *namesAt } + i * sizeof(std::uint32_t));
				if (!nameRva)
					break;
				const auto nameAt = RvaToOffset(sections, *nameRva);
				if (!nameAt || *nameAt > a_image.size() || a_image.size() - *nameAt < kExport.size() + 1)
					continue;
				if (std::memcmp(a_image.data() + *nameAt, kExport.data(), kExport.size()) != 0 ||
					a_image[*nameAt + kExport.size()] != std::byte{ 0 })
					continue;

				const auto ordinal = r.Read<std::uint16_t>(std::uint64_t{ *ordinalsAt } + i * sizeof(std::uint16_t));
				const auto dataRva = ordinal ? r.Read<std::uint32_t>(std::uint64_t{ *functionsAt } + std::uint64_t{ *ordinal } * sizeof(std::uint32_t)) : std::nullopt;
				const auto dataAt = dataRva ? RvaToOffset(sections, *dataRva) : std::nullopt;
				if (!dataAt)
					return std::nullopt;
				using Data = F4SE::PluginVersionData;
				if (r.Read<std::uint32_t>(*dataAt + offsetof(Data, dataVersion)) != std::uint32_t{ Data::kVersion })
					return std::nullopt;
				return r.Read<std::uint32_t>(*dataAt + offsetof(Data, pluginVersion));
			}
		} catch (...) {
		}
		return std::nullopt;
	}

	std::optional<std::uint32_t> ReadPluginVersion(const std::filesystem::path& a_dll) noexcept
	{
		try {
			std::ifstream in(a_dll, std::ios::binary | std::ios::ate);
			if (!in)
				return std::nullopt;
			const auto size = static_cast<std::uint64_t>(in.tellg());
			if (size > kMaxImageSize)
				return std::nullopt;
			std::vector<std::byte> image(size);
			in.seekg(0);
			in.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(size));
			if (!in)
				return std::nullopt;
			return ParsePluginVersion(image);
		} catch (...) {
			return std::nullopt;
		}
	}
}
