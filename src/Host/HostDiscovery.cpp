#include "Host/HostDiscovery.h"

#include "Log.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <psapi.h>

namespace cs::host
{
	namespace
	{
		auto* L = cs::log::Get("cs.host");

		constexpr const char* kEntryPoint = "DMUI_GetHostAPI";

		using GetHostApiFn = decltype(&DMUI_GetHostAPI);

		const DMUI_HostAPI* CallEntryPoint(GetHostApiFn a_entry) noexcept
		{
#if defined(_MSC_VER)
			__try {
				return a_entry(DMUI_API_VERSION_CURRENT);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
#else
			return a_entry(DMUI_API_VERSION_CURRENT);
#endif
		}

		bool ReadHostApi(const DMUI_HostAPI* a_api, HostApiView& a_view) noexcept
		{
#if defined(_MSC_VER)
			__try {
#endif
				a_view.present = true;
				a_view.structSize = a_api->structSize;
				a_view.apiVersion = a_api->apiVersion;
				a_view.hasRegisterClient = a_api->registerClient != nullptr;
				a_view.hasRegisterPage = a_api->registerPage != nullptr;
				a_view.hasQueryState = a_api->queryState != nullptr;
				a_view.hasRequestFrame = a_api->requestFrame != nullptr;
				a_view.hasReleaseFrame = a_api->releaseFrame != nullptr;
				a_view.hasIsMenuVisible = a_api->isMenuVisible != nullptr;
				a_view.hasSelectPage = a_api->selectPage != nullptr;
				a_view.hasAttachSwapChain =
					a_api->structSize >= DMUI_HOST_API_ATTACH_SWAP_CHAIN_SIZE &&
					a_api->attachSwapChain != nullptr;
				if (a_api->imguiFingerprint &&
					a_api->imguiFingerprint->structSize >= sizeof(DMUI_ImGuiFingerprint)) {
					a_view.hasFingerprint = true;
					a_view.fingerprint = *a_api->imguiFingerprint;
				}
				return true;
#if defined(_MSC_VER)
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
#endif
		}

		std::vector<HMODULE> EnumerateModules()
		{
			std::vector<HMODULE> modules(256);
			for (int attempt = 0; attempt < 4; ++attempt) {
				DWORD needed = 0;
				if (!EnumProcessModules(
						GetCurrentProcess(),
						modules.data(),
						static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
						&needed)) {
					L->warn("Failed to enumerate loaded modules: {}", GetLastError());
					return {};
				}

				const std::size_t count = needed / sizeof(HMODULE);
				if (count <= modules.size()) {
					modules.resize(count);
					return modules;
				}
				modules.resize(count + 32);
			}
			return {};
		}

		std::string ModulePath(HMODULE a_module)
		{
			std::wstring wide(MAX_PATH, L'\0');
			for (int attempt = 0; attempt < 4; ++attempt) {
				const DWORD length = GetModuleFileNameW(a_module, wide.data(), static_cast<DWORD>(wide.size()));
				if (length == 0)
					return {};
				if (length < wide.size()) {
					wide.resize(length);
					break;
				}
				wide.resize(wide.size() * 2);
			}

			const int required = WideCharToMultiByte(
				CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
			if (required <= 0)
				return {};

			std::string path(static_cast<std::size_t>(required), '\0');
			if (WideCharToMultiByte(
					CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), path.data(), required, nullptr, nullptr) <= 0)
				return {};
			return path;
		}

		std::string LowerAscii(std::string_view a_text)
		{
			std::string lowered(a_text);
			std::ranges::transform(lowered, lowered.begin(), [](char a_character) {
				const auto value = static_cast<unsigned char>(a_character);
				return static_cast<char>(value >= 'A' && value <= 'Z' ? value - 'A' + 'a' : value);
			});
			return lowered;
		}
	}

	std::optional<DiscoveredHost> DiscoverHost(const DMUI_ImGuiFingerprint& a_expected) noexcept
	{
		try {
			std::vector<HostCandidate> candidates;
			std::unordered_map<std::string, const DMUI_HostAPI*> apis;

			for (HMODULE module : EnumerateModules()) {
				if (!module)
					continue;
				const auto entry = reinterpret_cast<GetHostApiFn>(
					reinterpret_cast<void*>(GetProcAddress(module, kEntryPoint)));
				if (!entry)
					continue;

				auto path = ModulePath(module);
				if (path.empty())
					path = "<unknown module>";

				const auto* api = CallEntryPoint(entry);
				HostApiView view{};
				if (api && !ReadHostApi(api, view)) {
					view = HostApiView{};
					api = nullptr;
				}

				candidates.push_back(HostCandidate{
					.sortKey = LowerAscii(path),
					.displayPath = path,
					.compatibility = EvaluateHost(view, a_expected) });
				apis.insert_or_assign(path, api);
			}

			if (candidates.empty())
				return std::nullopt;

			const auto selection = SelectHost(candidates);
			if (selection.HasAmbiguousExporters())
				L->warn("{} modules export {}; only the first compatible one by path is used",
					selection.exporterCount, kEntryPoint);

			for (const auto& candidate : candidates) {
				if (candidate.compatibility == HostCompatibility::kCompatible)
					L->info("Compatible Dear-Modding UI host: {}", candidate.displayPath);
				else
					L->warn("Ignoring Dear-Modding UI host {}: {}",
						candidate.displayPath,
						DescribeCompatibility(candidate.compatibility));
			}

			if (!selection.selected)
				return std::nullopt;

			const auto& chosen = candidates[*selection.selected];
			if (selection.HasAmbiguousHosts())
				L->warn("{} compatible hosts are loaded; registering with {} only",
					selection.compatibleCount,
					chosen.displayPath);

			const auto found = apis.find(chosen.displayPath);
			if (found == apis.end() || !found->second)
				return std::nullopt;
			return DiscoveredHost{ found->second, chosen.displayPath };
		} catch (const std::exception& e) {
			L->warn("Dear-Modding UI host discovery failed: {}", e.what());
			return std::nullopt;
		} catch (...) {
			L->warn("Dear-Modding UI host discovery failed");
			return std::nullopt;
		}
	}
}
