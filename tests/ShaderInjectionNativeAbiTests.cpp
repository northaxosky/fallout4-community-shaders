#include "ShaderInjectionNativeAbiTests.h"

#include "Render/ShaderInjection.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cs::tests
{
	namespace
	{
		using Defines = std::map<std::string, std::string, std::less<>>;

		struct Failure : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		struct AdmissionBinding
		{
			std::string sourcePath;
			Defines     defines;
			std::string origin;
		};

		using AdmissionIndex =
			std::map<std::string, std::vector<AdmissionBinding>, std::less<>>;

		struct KnownRoute
		{
			std::string_view target;
			std::string_view variant;
			std::string_view stockSha1;
			std::string_view reason;
		};

		struct UnselectableRoute
		{
			std::string_view target;
			std::string_view variant;
		};

		constexpr std::array kKnownUnverifiedRoutes{
			KnownRoute{
				"deferred_prepass",
				"default",
				"c493970c042ccd90363c57596ff53f6fdd22ce5f",
				"PrePass has no consumer admission manifest"
			},
		};

		constexpr std::array kUnselectableRoutes{
			UnselectableRoute{ "deferred_composite", "default" },
			UnselectableRoute{ "vls_slice_scatter", "default" }
		};

		void Require(bool a_condition, std::string a_message)
		{
			if (!a_condition)
				throw Failure(std::move(a_message));
		}

		std::string NormalizeSha1(
			std::string_view a_sha1,
			std::string_view a_origin)
		{
			Require(
				a_sha1.size() == 40,
				std::string(a_origin) + ": SHA-1 must contain 40 hex digits");
			std::string normalized;
			normalized.reserve(a_sha1.size());
			for (const char character : a_sha1) {
				const auto value = static_cast<unsigned char>(character);
				Require(
					std::isxdigit(value) != 0,
					std::string(a_origin) + ": SHA-1 contains a non-hex digit");
				normalized.push_back(
					static_cast<char>(std::tolower(value)));
			}
			return normalized;
		}

		void AddDefine(
			Defines& a_defines,
			std::string a_name,
			std::string a_value,
			std::string_view a_origin)
		{
			Require(
				!a_name.empty(),
				std::string(a_origin) + ": define name is empty");
			const auto existing = a_defines.find(a_name);
			if (existing == a_defines.end()) {
				a_defines.emplace(
					std::move(a_name),
					std::move(a_value));
				return;
			}
			Require(
				existing->second == a_value,
				std::string(a_origin)
					+ ": define has conflicting values: "
					+ existing->first);
		}

		void AddManifestDefine(
			Defines& a_defines,
			std::string_view a_token,
			std::string_view a_origin)
		{
			const auto separator = a_token.find('=');
			if (separator == std::string_view::npos) {
				AddDefine(
					a_defines,
					std::string(a_token),
					"1",
					a_origin);
				return;
			}
			AddDefine(
				a_defines,
				std::string(a_token.substr(0, separator)),
				std::string(a_token.substr(separator + 1)),
				a_origin);
		}

		void AddManifestDefines(
			Defines& a_defines,
			const nlohmann::json& a_tokens,
			std::string_view a_origin)
		{
			Require(
				a_tokens.is_array(),
				std::string(a_origin) + ": defines must be an array");
			for (const auto& token : a_tokens) {
				Require(
					token.is_string(),
					std::string(a_origin)
						+ ": every define must be a string");
				AddManifestDefine(
					a_defines,
					token.get_ref<const std::string&>(),
					a_origin);
			}
		}

		std::string NormalizeManifestSource(
			std::string a_source,
			std::string_view a_origin)
		{
			std::ranges::replace(a_source, '\\', '/');
			constexpr std::string_view root = "shaders/";
			Require(
				a_source.starts_with(root),
				std::string(a_origin)
					+ ": source must be rooted under shaders/");
			a_source.erase(0, root.size());
			Require(
				!a_source.empty(),
				std::string(a_origin) + ": source path is empty");
			return a_source;
		}

		std::string NormalizeRuntimeSource(
			std::wstring_view a_source,
			std::string_view a_origin)
		{
			std::string normalized;
			normalized.reserve(a_source.size());
			for (const wchar_t character : a_source) {
				const auto value = static_cast<std::uint32_t>(character);
				Require(
					value <= 0x7FU,
					std::string(a_origin)
						+ ": runtime source path must be ASCII");
				normalized.push_back(
					character == L'\\' ? '/' : static_cast<char>(value));
			}
			Require(
				!normalized.empty(),
				std::string(a_origin) + ": runtime source path is empty");
			return normalized;
		}

		std::string FormatDefines(const Defines& a_defines)
		{
			std::string result = "{";
			bool first = true;
			for (const auto& [name, value] : a_defines) {
				if (!first)
					result += ", ";
				first = false;
				result += name + "=" + value;
			}
			result += "}";
			return result;
		}

		std::string FormatBinding(const AdmissionBinding& a_binding)
		{
			return a_binding.origin + " source='" + a_binding.sourcePath
				+ "' defines=" + FormatDefines(a_binding.defines);
		}

		void LoadManifest(
			const std::filesystem::path& a_path,
			AdmissionIndex& a_index)
		{
			const auto manifestName = a_path.filename().string();
			std::ifstream input(a_path);
			Require(
				input.is_open(),
				manifestName + ": could not open manifest");

			nlohmann::json document;
			try {
				input >> document;
			} catch (const nlohmann::json::exception& error) {
				throw Failure(
					manifestName + ": invalid JSON: " + error.what());
			}

			Require(
				document.is_object(),
				manifestName + ": manifest root must be an object");
			Require(
				document.contains("schema")
					&& document.at("schema").is_string()
					&& document.at("schema")
						.get_ref<const std::string&>()
						== "fo4cs.native-abi-admission",
				manifestName + ": unsupported manifest schema");
			Require(
				document.contains("schema_version")
					&& document.at("schema_version").is_number_integer()
					&& document.at("schema_version").get<int>() == 1,
				manifestName + ": unsupported manifest schema version");
			Require(
				document.contains("source")
					&& document.at("source").is_string(),
				manifestName + ": source must be a string");
			const auto source = NormalizeManifestSource(
				document.at("source").get<std::string>(),
				manifestName);

			Require(
				document.contains("common_defines"),
				manifestName + ": common_defines is missing");
			Defines commonDefines;
			AddManifestDefines(
				commonDefines,
				document.at("common_defines"),
				manifestName + ":common_defines");

			Require(
				document.contains("entries")
					&& document.at("entries").is_array(),
				manifestName + ": entries must be an array");
			const auto& entries = document.at("entries");
			for (std::size_t index = 0; index < entries.size(); ++index) {
				const auto origin = manifestName + ":entries["
					+ std::to_string(index) + "]";
				const auto& entry = entries.at(index);
				Require(
					entry.is_object(),
					origin + ": entry must be an object");
				Require(
					entry.contains("native_blob_sha1")
						&& entry.at("native_blob_sha1").is_string(),
					origin + ": native_blob_sha1 must be a string");
				Require(
					entry.contains("defines"),
					origin + ": defines is missing");

				auto defines = commonDefines;
				AddManifestDefines(
					defines,
					entry.at("defines"),
					origin + ":defines");
				const auto sha1 = NormalizeSha1(
					entry.at("native_blob_sha1")
						.get_ref<const std::string&>(),
					origin);
				a_index[sha1].push_back({
					.sourcePath = source,
					.defines = std::move(defines),
					.origin = origin
				});
			}
		}

		AdmissionIndex LoadAdmissionIndex(
			const std::filesystem::path& a_manifestDirectory,
			std::size_t& a_manifestCount)
		{
			Require(
				std::filesystem::is_directory(a_manifestDirectory),
				a_manifestDirectory.string()
					+ ": manifest directory does not exist");

			std::vector<std::filesystem::path> manifests;
			for (const auto& item :
				std::filesystem::directory_iterator(a_manifestDirectory)) {
				if (!item.is_regular_file())
					continue;
				const auto filename = item.path().filename().string();
				if (std::string_view(filename).ends_with(
						"-native-abi.json")) {
					manifests.push_back(item.path());
				}
			}
			std::ranges::sort(manifests);
			Require(
				!manifests.empty(),
				a_manifestDirectory.string()
					+ ": no *-native-abi.json manifests found");

			AdmissionIndex index;
			for (const auto& manifest : manifests)
				LoadManifest(manifest, index);
			a_manifestCount = manifests.size();
			return index;
		}

		Defines GetRuntimeDefines(
			const engine::ShaderInjectionTargetMetadata& a_target,
			const engine::ShaderReplacementVariantRegistration& a_variant,
			std::string_view a_route)
		{
			Defines defines;
			for (const auto& define : a_target.baseDefines) {
				AddDefine(
					defines,
					std::string(define.name),
					std::string(define.value),
					a_route);
			}
			for (const auto& [name, value] :
				a_variant.compilation.defines) {
				AddDefine(defines, name, value, a_route);
			}
			return defines;
		}

		std::optional<std::size_t> FindKnownUnverifiedRoute(
			std::string_view a_target,
			std::string_view a_variant,
			std::string_view a_sha1)
		{
			for (std::size_t index = 0;
				index < kKnownUnverifiedRoutes.size();
				++index) {
				const auto& route = kKnownUnverifiedRoutes[index];
				if (route.target == a_target
					&& route.variant == a_variant
					&& route.stockSha1 == a_sha1) {
					return index;
				}
			}
			return std::nullopt;
		}

		std::optional<std::size_t> FindUnselectableRoute(
			std::string_view a_target,
			std::string_view a_variant)
		{
			for (std::size_t index = 0;
				index < kUnselectableRoutes.size();
				++index) {
				const auto& route = kUnselectableRoutes[index];
				if (route.target == a_target
					&& route.variant == a_variant) {
					return index;
				}
			}
			return std::nullopt;
		}

		bool BindingsAgree(
			const std::vector<AdmissionBinding>& a_bindings,
			std::string& a_conflict)
		{
			const auto& expected = a_bindings.front();
			for (std::size_t index = 1;
				index < a_bindings.size();
				++index) {
				const auto& candidate = a_bindings[index];
				if (candidate.sourcePath != expected.sourcePath
					|| candidate.defines != expected.defines) {
					a_conflict = FormatBinding(expected) + " conflicts with "
						+ FormatBinding(candidate);
					return false;
				}
			}
			return true;
		}

		void PrintFailure(std::string_view a_message)
		{
			std::cerr << "FAIL: " << a_message << '\n';
		}
	}

	int RunShaderInjectionNativeAbiTests(
		const std::filesystem::path& a_manifestDirectory)
	{
		try {
			std::size_t manifestCount = 0;
			const auto admissions =
				LoadAdmissionIndex(a_manifestDirectory, manifestCount);
			const auto routes =
				engine::GetDefaultShaderReplacementVariants();
			std::array<bool, kKnownUnverifiedRoutes.size()>
				seenUnverified{};
			std::array<bool, kUnselectableRoutes.size()>
				seenUnselectable{};
			std::size_t verifiedCount = 0;
			std::size_t unverifiedCount = 0;
			std::size_t unselectableCount = 0;
			bool ok = true;

			for (const auto& route : routes) {
				const auto* target =
					engine::GetShaderInjectionTarget(route.targetId);
				if (!target) {
					PrintFailure(
						"default shader replacement route has an invalid target");
					ok = false;
					continue;
				}
				const auto routeName =
					std::string(target->name) + "/" + route.name;

				if (route.expectedStockSha1.empty()) {
					// Routes without stock guards cannot be selected by the hash resolver.
					const auto known = FindUnselectableRoute(
						target->name,
						route.name);
					if (!known) {
						PrintFailure(
							"route '" + routeName
							+ "' unexpectedly has no stock SHA-1 guard");
						ok = false;
						continue;
					}
					if (seenUnselectable[*known]) {
						PrintFailure(
							"unselectable route appears more than once: '"
							+ routeName + "'");
						ok = false;
						continue;
					}
					seenUnselectable[*known] = true;
					++unselectableCount;
					std::cout
						<< "OUT OF SCOPE: route '" << routeName
						<< "' has no stock SHA-1 guard and cannot be selected\n";
					continue;
				}

				const auto sha1 = NormalizeSha1(
					route.expectedStockSha1,
					routeName);
				const auto admission = admissions.find(sha1);
				const auto knownUnverified =
					FindKnownUnverifiedRoute(
						target->name,
						route.name,
						sha1);
				if (admission == admissions.end()) {
					if (!knownUnverified) {
						PrintFailure(
							"route '" + routeName + "' stock SHA-1 "
							+ sha1
							+ " has no native ABI admission entry");
						ok = false;
						continue;
					}
					if (seenUnverified[*knownUnverified]) {
						PrintFailure(
							"known-unverified route appears more than once: '"
							+ routeName + "'");
						ok = false;
						continue;
					}
					seenUnverified[*knownUnverified] = true;
					++unverifiedCount;
					std::cout
						<< "UNVERIFIED: route '" << routeName
						<< "' stock SHA-1 " << sha1 << " -- "
						<< kKnownUnverifiedRoutes[*knownUnverified].reason
						<< '\n';
					continue;
				}

				if (knownUnverified) {
					seenUnverified[*knownUnverified] = true;
					PrintFailure(
						"route '" + routeName
						+ "' now has an admission entry; remove its known-unverified allowance");
					ok = false;
				}

				const auto& bindings = admission->second;
				std::string conflict;
				if (!BindingsAgree(bindings, conflict)) {
					PrintFailure(
						"route '" + routeName + "' stock SHA-1 "
						+ sha1
						+ " has conflicting native ABI admissions: "
						+ conflict);
					ok = false;
					continue;
				}
				if (bindings.size() > 1) {
					std::cout
						<< "EQUIVALENT: route '" << routeName
						<< "' stock SHA-1 " << sha1 << " has "
						<< bindings.size()
						<< " agreeing admission entries\n";
				}

				const auto& binding = bindings.front();
				const auto runtimeSource = NormalizeRuntimeSource(
					route.compilation.sourcePath,
					routeName);
				const auto runtimeDefines =
					GetRuntimeDefines(*target, route, routeName);
				bool routeOk = true;
				if (runtimeSource != binding.sourcePath) {
					PrintFailure(
						"route '" + routeName + "' stock SHA-1 "
						+ sha1 + " source mismatch: runtime='"
						+ runtimeSource + "', admission='"
						+ binding.sourcePath + "' at "
						+ binding.origin);
					ok = false;
					routeOk = false;
				}
				if (runtimeDefines != binding.defines) {
					PrintFailure(
						"route '" + routeName + "' stock SHA-1 "
						+ sha1 + " defines mismatch: runtime="
						+ FormatDefines(runtimeDefines)
						+ ", admission="
						+ FormatDefines(binding.defines)
						+ " at " + binding.origin);
					ok = false;
					routeOk = false;
				}
				if (routeOk) {
					++verifiedCount;
					std::cout
						<< "VERIFIED: route '" << routeName
						<< "' stock SHA-1 " << sha1 << " matches "
						<< binding.origin << '\n';
				}
			}

			for (std::size_t index = 0;
				index < seenUnverified.size();
				++index) {
				if (seenUnverified[index])
					continue;
				const auto& route = kKnownUnverifiedRoutes[index];
				PrintFailure(
					"known-unverified allowance was not exercised: '"
					+ std::string(route.target) + "/"
					+ std::string(route.variant) + "' stock SHA-1 "
					+ std::string(route.stockSha1));
				ok = false;
			}
			for (std::size_t index = 0;
				index < seenUnselectable.size();
				++index) {
				if (seenUnselectable[index])
					continue;
				const auto& route = kUnselectableRoutes[index];
				PrintFailure(
					"expected unselectable route was not found: '"
					+ std::string(route.target) + "/"
					+ std::string(route.variant) + "'");
				ok = false;
			}

			if (!ok)
				return 1;
			std::cout
				<< "PASS: shader delivery native ABI gate verified "
				<< verifiedCount << " routes, surfaced "
				<< unverifiedCount << " unverified routes, and excluded "
				<< unselectableCount << " unselectable routes across "
				<< manifestCount << " manifests\n";
			return 0;
		} catch (const std::exception& error) {
			PrintFailure(error.what());
			return 1;
		}
	}
}
