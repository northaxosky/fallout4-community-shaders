#include "Compat/PluginScan.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <system_error>

#include <Windows.h>

#include "F4SE/Interfaces.h"

namespace cs::compat
{
	namespace
	{
		const std::uint8_t* Bounded(const std::uint8_t* a_base, std::size_t a_imageSize, std::uint64_t a_rva, std::uint64_t a_size)
		{
			return a_rva <= a_imageSize && a_size <= a_imageSize - a_rva ? a_base + a_rva : nullptr;
		}

		const F4SE::PluginVersionData* FindVersionData(const std::uint8_t* a_base)
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(a_base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE)
				return nullptr;
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(a_base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE ||
				nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
				nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
				return nullptr;

			const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
			const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
			const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
				Bounded(a_base, imageSize, dir.VirtualAddress, sizeof(IMAGE_EXPORT_DIRECTORY)));
			if (!dir.Size || !exports)
				return nullptr;

			const auto* names = reinterpret_cast<const std::uint32_t*>(
				Bounded(a_base, imageSize, exports->AddressOfNames, std::uint64_t{ exports->NumberOfNames } * sizeof(std::uint32_t)));
			const auto* ordinals = reinterpret_cast<const std::uint16_t*>(
				Bounded(a_base, imageSize, exports->AddressOfNameOrdinals, std::uint64_t{ exports->NumberOfNames } * sizeof(std::uint16_t)));
			const auto* functions = reinterpret_cast<const std::uint32_t*>(
				Bounded(a_base, imageSize, exports->AddressOfFunctions, std::uint64_t{ exports->NumberOfFunctions } * sizeof(std::uint32_t)));
			if (!names || !ordinals || !functions)
				return nullptr;

			constexpr std::string_view kExport = "F4SEPlugin_Version";
			for (std::uint32_t i = 0; i < exports->NumberOfNames; ++i) {
				if (names[i] >= imageSize)
					continue;
				const auto* name = reinterpret_cast<const char*>(a_base + names[i]);
				const auto room = std::min<std::size_t>(imageSize - names[i], kExport.size() + 1);
				if (std::strncmp(name, kExport.data(), room) != 0 || room <= kExport.size())
					continue;
				if (ordinals[i] >= exports->NumberOfFunctions)
					return nullptr;
				return reinterpret_cast<const F4SE::PluginVersionData*>(
					Bounded(a_base, imageSize, functions[ordinals[i]], sizeof(F4SE::PluginVersionData)));
			}
			return nullptr;
		}

		bool Read(const std::filesystem::path& a_file, ScannedPlugin& a_out)
		{
			// Image-resource mapping never runs DllMain or resolves imports.
			const auto handle = ::LoadLibraryExW(a_file.c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
			if (!handle)
				return false;
			// The low bits of an image-resource handle are flags, not address.
			const auto* base = reinterpret_cast<const std::uint8_t*>(reinterpret_cast<std::uintptr_t>(handle) & ~std::uintptr_t{ 3 });
			bool found = false;
			if (const auto* data = FindVersionData(base);
				data && data->dataVersion == F4SE::PluginVersionData::kVersion) {
				const auto* end = static_cast<const char*>(std::memchr(data->pluginName, '\0', sizeof(data->pluginName)));
				if (end && end != data->pluginName) {
					a_out.name.assign(data->pluginName, end);
					a_out.version = data->GetPluginVersion();
					a_out.file = a_file;
					found = true;
				}
			}
			::FreeLibrary(handle);
			return found;
		}
	}

	std::vector<ScannedPlugin> ScanF4SEPlugins(const std::filesystem::path& a_directory)
	{
		std::vector<ScannedPlugin> plugins;
		std::error_code ec;
		for (std::filesystem::directory_iterator it(a_directory, ec), end; !ec && it != end; it.increment(ec)) {
			if (!it->is_regular_file(ec) || _wcsicmp(it->path().extension().c_str(), L".dll") != 0)
				continue;
			ScannedPlugin plugin;
			if (Read(it->path(), plugin))
				plugins.push_back(std::move(plugin));
		}
		return plugins;
	}
}
