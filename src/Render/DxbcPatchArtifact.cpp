#include "Render/DxbcPatchArtifact.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace cs::engine::dxbc_patch
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::string_view kSchema =
			"fo4re.sss-dxbc-patch-plans";
		constexpr std::string_view kRelease =
			"fallout4-ae-1.11.221.0";
		constexpr std::string_view kExecutableVersion =
			"1.11.221.0";
		constexpr std::string_view kExecutableSha256 =
			"428f9996cc4248e26c0f62f9fdd3eaf0e5eb305834b67ee5996538e593218b61";
		constexpr std::uint64_t kExecutableLength = 55293864;
		constexpr std::string_view kEngineContractScope =
			"BSDFLightShaderMacros::Get+GetPixelShaderID@fallout4-ae-1.11.221.0";
		constexpr std::size_t kExpectedCandidates = 64;
		constexpr std::size_t kExpectedCandidateIdentities = 133;

		struct ParseFailure : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		struct ParsedCandidate
		{
			StockIdentity stock;
			CandidateStatus status = CandidateStatus::kUnproven;
			std::vector<std::string> opaquePsids;
			std::vector<std::uint32_t> fxpOrdinals;
			Json identities;
		};

		struct DxbcChunk
		{
			std::array<char, 4> fourcc{};
			std::size_t offset = 0;
			std::size_t payloadSize = 0;
		};

		struct DxbcLayout
		{
			std::vector<DxbcChunk> chunks;
			std::size_t executableIndex = 0;
		};

		bool CheckedAdd(
			std::size_t a_left,
			std::size_t a_right,
			std::size_t& a_result) noexcept
		{
			if (a_left > std::numeric_limits<std::size_t>::max() - a_right)
				return false;
			a_result = a_left + a_right;
			return true;
		}

		bool IsLowerHex(
			std::string_view a_value,
			std::size_t a_length) noexcept
		{
			return a_value.size() == a_length
				&& std::ranges::all_of(a_value, [](char a_character) {
					return (a_character >= '0' && a_character <= '9')
						|| (a_character >= 'a' && a_character <= 'f');
				});
		}

		void RequireKeys(
			const Json& a_value,
			std::initializer_list<std::string_view> a_keys,
			std::string_view a_path)
		{
			if (!a_value.is_object() || a_value.size() != a_keys.size())
				throw ParseFailure(std::string(a_path) + " has unexpected fields");
			for (const auto key : a_keys) {
				if (!a_value.contains(std::string(key)))
					throw ParseFailure(std::string(a_path) + " is missing " + std::string(key));
			}
		}

		const Json& Member(
			const Json& a_value,
			std::string_view a_key,
			std::string_view a_path)
		{
			if (!a_value.is_object())
				throw ParseFailure(std::string(a_path) + " must be an object");
			const auto iterator = a_value.find(std::string(a_key));
			if (iterator == a_value.end())
				throw ParseFailure(std::string(a_path) + " is missing " + std::string(a_key));
			return *iterator;
		}

		std::string ReadString(
			const Json& a_value,
			std::string_view a_path)
		{
			if (!a_value.is_string())
				throw ParseFailure(std::string(a_path) + " must be a string");
			const auto result = a_value.get<std::string>();
			if (result.empty())
				throw ParseFailure(std::string(a_path) + " must not be empty");
			return result;
		}

		std::uint64_t ReadUnsigned(
			const Json& a_value,
			std::string_view a_path)
		{
			if (a_value.is_number_unsigned())
				return a_value.get<std::uint64_t>();
			if (a_value.is_number_integer()) {
				const auto signedValue = a_value.get<std::int64_t>();
				if (signedValue >= 0)
					return static_cast<std::uint64_t>(signedValue);
			}
			throw ParseFailure(std::string(a_path) + " must be a non-negative integer");
		}

		std::size_t ReadSize(
			const Json& a_value,
			std::string_view a_path)
		{
			const auto result = ReadUnsigned(a_value, a_path);
			if (result > std::numeric_limits<std::size_t>::max())
				throw ParseFailure(std::string(a_path) + " exceeds the platform size limit");
			return static_cast<std::size_t>(result);
		}

		void RequireStringValue(
			const Json& a_value,
			std::string_view a_expected,
			std::string_view a_path)
		{
			if (!a_value.is_string()
				|| a_value.get_ref<const std::string&>() != a_expected) {
				throw ParseFailure(std::string(a_path) + " is unsupported");
			}
		}

		sha1::Sha1Result ReadSha1(
			const Json& a_value,
			std::string_view a_path)
		{
			const auto text = ReadString(a_value, a_path);
			sha1::Sha1Result result{};
			if (!IsLowerHex(text, 40) || !sha1::Sha1FromHex(text, result))
				throw ParseFailure(std::string(a_path) + " is not a lowercase SHA-1");
			return result;
		}

		sha256::Sha256Result ReadSha256(
			const Json& a_value,
			std::string_view a_path)
		{
			const auto text = ReadString(a_value, a_path);
			sha256::Sha256Result result{};
			if (!IsLowerHex(text, 64) || !sha256::Sha256FromHex(text, result))
				throw ParseFailure(std::string(a_path) + " is not a lowercase SHA-256");
			return result;
		}

		std::array<std::uint8_t, 16> ReadChecksum(
			const Json& a_value,
			std::string_view a_path)
		{
			const auto text = ReadString(a_value, a_path);
			if (!IsLowerHex(text, 32))
				throw ParseFailure(std::string(a_path) + " is not a lowercase checksum");

			std::array<std::uint8_t, 16> result{};
			for (std::size_t index = 0; index < result.size(); ++index) {
				unsigned parsed = 0;
				const auto first = text.data() + index * 2;
				const auto last = first + 2;
				const auto conversion =
					std::from_chars(first, last, parsed, 16);
				if (conversion.ec != std::errc{} || conversion.ptr != last)
					throw ParseFailure(std::string(a_path) + " is malformed");
				result[index] = static_cast<std::uint8_t>(parsed);
			}
			return result;
		}

		StockIdentity ReadStock(
			const Json& a_value,
			std::string_view a_path)
		{
			RequireKeys(a_value, { "length", "sha1", "sha256" }, a_path);
			StockIdentity result;
			result.length = ReadSize(Member(a_value, "length", a_path), "stock.length");
			if (result.length == 0 || result.length > kMaximumShaderBytes)
				throw ParseFailure(std::string(a_path) + ".length is out of range");
			result.sha1 = ReadSha1(Member(a_value, "sha1", a_path), "stock.sha1");
			result.sha256 = ReadSha256(Member(a_value, "sha256", a_path), "stock.sha256");
			return result;
		}

		bool StockEqual(
			const StockIdentity& a_left,
			const StockIdentity& a_right) noexcept
		{
			return a_left.length == a_right.length
				&& a_left.sha1.bytes == a_right.sha1.bytes
				&& a_left.sha256 == a_right.sha256;
		}

		std::uint32_t ReadOpaquePsid(
			const Json& a_value,
			std::string_view a_path,
			std::string& a_text)
		{
			a_text = ReadString(a_value, a_path);
			if (a_text.size() != 10
				|| a_text[0] != '0'
				|| a_text[1] != 'x'
				|| !IsLowerHex(std::string_view(a_text).substr(2), 8)) {
				throw ParseFailure(std::string(a_path) + " is not an opaque PSID");
			}
			std::uint32_t result = 0;
			const auto conversion = std::from_chars(
				a_text.data() + 2,
				a_text.data() + a_text.size(),
				result,
				16);
			if (conversion.ec != std::errc{}
				|| conversion.ptr != a_text.data() + a_text.size()) {
				throw ParseFailure(std::string(a_path) + " is malformed");
			}
			return result;
		}

		CandidateStatus ReadCandidateStatus(
			const Json& a_value)
		{
			const auto status = ReadString(a_value, "candidate.status");
			if (status == "PASS")
				return CandidateStatus::kPass;
			if (status == "FAIL")
				return CandidateStatus::kFail;
			if (status == "UNPROVEN")
				return CandidateStatus::kUnproven;
			throw ParseFailure("candidate.status is unsupported");
		}

		Target TargetForRecipe(std::string_view a_recipe)
		{
			if (a_recipe == "sss-mask-load-base-v1")
				return Target::kDirectional;
			if (a_recipe == "sss-mask-load-ibl-v1")
				return Target::kDirectionalIbl;
			throw ParseFailure("plan.recipe_id names an unsupported target");
		}

		std::vector<std::byte> ReadDwords(
			const Json& a_value,
			std::size_t a_expectedCount,
			std::string_view a_path)
		{
			if (!a_value.is_array() || a_value.size() != a_expectedCount)
				throw ParseFailure(std::string(a_path) + " has the wrong dword count");

			std::vector<std::byte> result;
			result.reserve(a_expectedCount * sizeof(std::uint32_t));
			for (const auto& raw : a_value) {
				const auto text = ReadString(raw, a_path);
				if (!IsLowerHex(text, 8))
					throw ParseFailure(std::string(a_path) + " contains a malformed dword");
				std::uint32_t value = 0;
				const auto conversion = std::from_chars(
					text.data(),
					text.data() + text.size(),
					value,
					16);
				if (conversion.ec != std::errc{}
					|| conversion.ptr != text.data() + text.size()) {
					throw ParseFailure(std::string(a_path) + " contains a malformed dword");
				}
				for (unsigned shift = 0; shift < 32; shift += 8) {
					result.push_back(static_cast<std::byte>(
						(value >> shift) & 0xFF));
				}
			}
			return result;
		}

		Insertion ReadInsertion(
			const Json& a_value,
			std::size_t a_expectedDwords,
			std::string_view a_path)
		{
			RequireKeys(
				a_value,
				{ "executable_dword", "file_offset", "inserted_dwords", "preimage" },
				a_path);
			Insertion result;
			result.executableDword = ReadSize(
				Member(a_value, "executable_dword", a_path),
				"insertion.executable_dword");
			result.fileOffset = ReadSize(
				Member(a_value, "file_offset", a_path),
				"insertion.file_offset");
			result.bytes = ReadDwords(
				Member(a_value, "inserted_dwords", a_path),
				a_expectedDwords,
				"insertion.inserted_dwords");

			const auto& preimage = Member(a_value, "preimage", a_path);
			RequireKeys(preimage, { "length", "offset", "sha256" }, "insertion.preimage");
			result.preimage.length = ReadSize(
				Member(preimage, "length", "insertion.preimage"),
				"preimage.length");
			result.preimage.offset = ReadSize(
				Member(preimage, "offset", "insertion.preimage"),
				"preimage.offset");
			result.preimage.sha256 = ReadSha256(
				Member(preimage, "sha256", "insertion.preimage"),
				"preimage.sha256");
			return result;
		}

		sha256::Sha256Result DigestJson(const Json& a_value)
		{
			auto canonical = a_value.dump();
			canonical.push_back('\n');
			const auto result =
				sha256::Sha256Compute(canonical.data(), canonical.size());
			if (sha256::Sha256IsZero(result))
				throw ParseFailure("SHA-256 computation failed");
			return result;
		}

		std::uint16_t ReadU16(
			std::span<const std::byte> a_bytes,
			std::size_t a_offset) noexcept
		{
			return static_cast<std::uint16_t>(
				std::to_integer<std::uint8_t>(a_bytes[a_offset]))
				| static_cast<std::uint16_t>(
					std::to_integer<std::uint8_t>(a_bytes[a_offset + 1])
					<< 8);
		}

		std::uint32_t ReadU32(
			std::span<const std::byte> a_bytes,
			std::size_t a_offset) noexcept
		{
			return static_cast<std::uint32_t>(
				std::to_integer<std::uint8_t>(a_bytes[a_offset]))
				| (static_cast<std::uint32_t>(
					std::to_integer<std::uint8_t>(a_bytes[a_offset + 1]))
					<< 8)
				| (static_cast<std::uint32_t>(
					std::to_integer<std::uint8_t>(a_bytes[a_offset + 2]))
					<< 16)
				| (static_cast<std::uint32_t>(
					std::to_integer<std::uint8_t>(a_bytes[a_offset + 3]))
					<< 24);
		}

		void WriteU32(
			std::span<std::byte> a_bytes,
			std::size_t a_offset,
			std::uint32_t a_value) noexcept
		{
			for (unsigned shift = 0; shift < 32; shift += 8) {
				a_bytes[a_offset + shift / 8] =
					static_cast<std::byte>((a_value >> shift) & 0xFF);
			}
		}

		constexpr std::array<std::uint32_t, 64> kChecksumRotations{
			7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
			5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
			4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
			6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
		};

		constexpr std::array<std::uint32_t, 64> kChecksumConstants{
			0xD76AA478, 0xE8C7B756, 0x242070DB, 0xC1BDCEEE,
			0xF57C0FAF, 0x4787C62A, 0xA8304613, 0xFD469501,
			0x698098D8, 0x8B44F7AF, 0xFFFF5BB1, 0x895CD7BE,
			0x6B901122, 0xFD987193, 0xA679438E, 0x49B40821,
			0xF61E2562, 0xC040B340, 0x265E5A51, 0xE9B6C7AA,
			0xD62F105D, 0x02441453, 0xD8A1E681, 0xE7D3FBC8,
			0x21E1CDE6, 0xC33707D6, 0xF4D50D87, 0x455A14ED,
			0xA9E3E905, 0xFCEFA3F8, 0x676F02D9, 0x8D2A4C8A,
			0xFFFA3942, 0x8771F681, 0x6D9D6122, 0xFDE5380C,
			0xA4BEEA44, 0x4BDECFA9, 0xF6BB4B60, 0xBEBFBC70,
			0x289B7EC6, 0xEAA127FA, 0xD4EF3085, 0x04881D05,
			0xD9D4D039, 0xE6DB99E5, 0x1FA27CF8, 0xC4AC5665,
			0xF4292244, 0x432AFF97, 0xAB9423A7, 0xFC93A039,
			0x655B59C3, 0x8F0CCC92, 0xFFEFF47D, 0x85845DD1,
			0x6FA87E4F, 0xFE2CE6E0, 0xA3014314, 0x4E0811A1,
			0xF7537E82, 0xBD3AF235, 0x2AD7D2BB, 0xEB86D391
		};

		std::array<std::uint32_t, 4> CompressChecksum(
			std::array<std::uint32_t, 4> a_state,
			std::span<const std::byte, 64> a_block) noexcept
		{
			std::array<std::uint32_t, 16> words{};
			for (std::size_t index = 0; index < words.size(); ++index)
				words[index] = ReadU32(a_block, index * 4);

			auto a = a_state[0];
			auto b = a_state[1];
			auto c = a_state[2];
			auto d = a_state[3];
			for (std::size_t index = 0; index < 64; ++index) {
				std::uint32_t mixed = 0;
				std::size_t wordIndex = 0;
				if (index < 16) {
					mixed = (b & c) | (~b & d);
					wordIndex = index;
				} else if (index < 32) {
					mixed = (d & b) | (~d & c);
					wordIndex = (5 * index + 1) % 16;
				} else if (index < 48) {
					mixed = b ^ c ^ d;
					wordIndex = (3 * index + 5) % 16;
				} else {
					mixed = c ^ (b | ~d);
					wordIndex = (7 * index) % 16;
				}
				const auto step = a + mixed
					+ kChecksumConstants[index] + words[wordIndex];
				a = d;
				d = c;
				c = b;
				b += std::rotl(step, static_cast<int>(
					kChecksumRotations[index]));
			}
			a_state[0] += a;
			a_state[1] += b;
			a_state[2] += c;
			a_state[3] += d;
			return a_state;
		}

		std::optional<DxbcLayout> ParseDxbc(
			std::span<const std::byte> a_bytecode) noexcept
		{
			if (a_bytecode.size() < 32
				|| a_bytecode.size() > kMaximumShaderBytes
				|| std::memcmp(a_bytecode.data(), "DXBC", 4) != 0
				|| ReadU16(a_bytecode, 20) != 1
				|| ReadU16(a_bytecode, 22) != 0
				|| ReadU32(a_bytecode, 24) != a_bytecode.size()
				|| !ValidateDxbcChecksum(a_bytecode)) {
				return std::nullopt;
			}

			const auto chunkCount = ReadU32(a_bytecode, 28);
			if (chunkCount == 0 || chunkCount > 128)
				return std::nullopt;
			std::size_t tableBytes = 0;
			if (!CheckedAdd(32, static_cast<std::size_t>(chunkCount) * 4, tableBytes)
				|| tableBytes > a_bytecode.size()) {
				return std::nullopt;
			}

			DxbcLayout layout;
			try {
				layout.chunks.reserve(chunkCount);
			} catch (...) {
				return std::nullopt;
			}
			std::set<std::uint32_t> uniqueOffsets;
			std::vector<std::pair<std::size_t, std::size_t>> intervals;
			std::size_t executableCount = 0;
			for (std::uint32_t index = 0; index < chunkCount; ++index) {
				const auto offset = ReadU32(a_bytecode, 32 + index * 4);
				if (offset % 4 != 0
					|| offset < tableBytes
					|| offset > a_bytecode.size() - 8
					|| !uniqueOffsets.insert(offset).second) {
					return std::nullopt;
				}
				const auto payloadSize = ReadU32(a_bytecode, offset + 4);
				std::size_t chunkEnd = 0;
				if (!CheckedAdd(
						static_cast<std::size_t>(offset) + 8,
						payloadSize,
						chunkEnd)
					|| chunkEnd > a_bytecode.size()) {
					return std::nullopt;
				}
				DxbcChunk chunk;
				std::memcpy(chunk.fourcc.data(), a_bytecode.data() + offset, 4);
				chunk.offset = offset;
				chunk.payloadSize = payloadSize;
				layout.chunks.push_back(chunk);
				intervals.emplace_back(offset, chunkEnd);
				const bool executable =
					chunk.fourcc == std::array<char, 4>{ 'S', 'H', 'D', 'R' }
					|| chunk.fourcc == std::array<char, 4>{ 'S', 'H', 'E', 'X' };
				if (executable) {
					layout.executableIndex = index;
					++executableCount;
				}
			}

			std::ranges::sort(intervals);
			for (std::size_t index = 1; index < intervals.size(); ++index) {
				if (intervals[index - 1].second > intervals[index].first)
					return std::nullopt;
			}
			if (executableCount != 1)
				return std::nullopt;

			const auto& executable = layout.chunks[layout.executableIndex];
			if (executable.payloadSize < 8 || executable.payloadSize % 4 != 0)
				return std::nullopt;
			const auto payloadOffset = executable.offset + 8;
			const auto versionToken = ReadU32(a_bytecode, payloadOffset);
			const auto dwordLength = ReadU32(a_bytecode, payloadOffset + 4);
			const auto stage = versionToken >> 16;
			const auto major = (versionToken >> 4) & 0xF;
			const auto minor = versionToken & 0xF;
			if (stage != 0
				|| major != 5
				|| minor != 0
				|| (versionToken & 0x0000FF00) != 0
				|| dwordLength != executable.payloadSize / 4) {
				return std::nullopt;
			}
			return layout;
		}

		bool ValidatePlanRanges(const Plan& a_plan) noexcept
		{
			const auto validateInsertion =
				[&a_plan](const Insertion& a_insertion) {
					std::size_t payloadOffset = 0;
					std::size_t expectedOffset = 0;
					std::size_t preimageEnd = 0;
					return a_insertion.fileOffset % 4 == 0
						&& a_insertion.bytes.size() % 4 == 0
						&& !a_insertion.bytes.empty()
						&& CheckedAdd(
							a_plan.executableChunkOffset,
							8,
							payloadOffset)
						&& a_insertion.executableDword
							<= (std::numeric_limits<std::size_t>::max()
								- payloadOffset) / 4
						&& CheckedAdd(
							payloadOffset,
							a_insertion.executableDword * 4,
							expectedOffset)
						&& expectedOffset == a_insertion.fileOffset
						&& a_insertion.fileOffset <= a_plan.stock.length
						&& CheckedAdd(
							a_insertion.preimage.offset,
							a_insertion.preimage.length,
							preimageEnd)
						&& a_insertion.preimage.length != 0
						&& preimageEnd <= a_plan.stock.length;
				};
			return a_plan.executableChunkOffset % 4 == 0
				&& validateInsertion(a_plan.declaration)
				&& validateInsertion(a_plan.code)
				&& a_plan.declaration.fileOffset != a_plan.code.fileOffset;
		}

		std::size_t ShiftedOffset(
			std::size_t a_offset,
			const Plan& a_plan) noexcept
		{
			std::size_t result = a_offset;
			for (const auto* insertion :
				{ &a_plan.declaration, &a_plan.code }) {
				if (insertion->fileOffset <= a_offset)
					result += insertion->bytes.size();
			}
			return result;
		}
	}

	ArtifactParseResult ParseArtifact(std::string_view a_bytes)
	{
		try {
			if (a_bytes.empty() || a_bytes.size() > kMaximumArtifactBytes)
				throw ParseFailure("artifact size is out of range");
			const auto document = Json::parse(
				a_bytes.begin(),
				a_bytes.end(),
				nullptr,
				false,
				true);
			if (document.is_discarded())
				throw ParseFailure("artifact JSON is malformed");

			auto canonical = document.dump();
			canonical.push_back('\n');
			if (canonical.size() != a_bytes.size()
				|| !std::equal(
					canonical.begin(),
					canonical.end(),
					a_bytes.begin())) {
				throw ParseFailure("artifact JSON is not canonical");
			}

			RequireKeys(
				document,
				{
					"archive",
					"candidates",
					"coverage",
					"denominator",
					"engine_scope",
					"evidence",
					"fallback",
					"plans",
					"release_scope",
					"runtime_contract",
					"schema",
					"schema_version",
					"status"
				},
				"artifact");
			RequireStringValue(Member(document, "schema", "artifact"), kSchema, "schema");
			if (ReadUnsigned(Member(document, "schema_version", "artifact"), "schema_version") != 1)
				throw ParseFailure("schema_version is unsupported");
			RequireStringValue(
				Member(document, "status", "artifact"),
				"provisional",
				"status");
			RequireStringValue(
				Member(document, "release_scope", "artifact"),
				"architecture-proof-dev-only",
				"release_scope");

			Artifact artifact;
			artifact.status = "provisional";
			artifact.releaseScope = "architecture-proof-dev-only";

			const auto& engineScope = Member(document, "engine_scope", "artifact");
			RequireKeys(
				engineScope,
				{
					"engine_contract_scope",
					"executable_length",
					"executable_sha256",
					"executable_version",
					"release"
				},
				"engine_scope");
			RequireStringValue(
				Member(engineScope, "release", "engine_scope"),
				kRelease,
				"engine_scope.release");
			RequireStringValue(
				Member(engineScope, "executable_version", "engine_scope"),
				kExecutableVersion,
				"engine_scope.executable_version");
			RequireStringValue(
				Member(engineScope, "executable_sha256", "engine_scope"),
				kExecutableSha256,
				"engine_scope.executable_sha256");
			if (ReadUnsigned(
					Member(engineScope, "executable_length", "engine_scope"),
					"engine_scope.executable_length")
				!= kExecutableLength) {
				throw ParseFailure("engine_scope.executable_length is unsupported");
			}
			RequireStringValue(
				Member(engineScope, "engine_contract_scope", "engine_scope"),
				kEngineContractScope,
				"engine_scope.engine_contract_scope");
			artifact.runtime.release = std::string(kRelease);
			artifact.runtime.executableVersion =
				std::string(kExecutableVersion);
			if (!sha256::Sha256FromHex(
					kExecutableSha256,
					artifact.runtime.executableSha256)) {
				throw ParseFailure("compiled executable SHA-256 is invalid");
			}
			artifact.runtime.executableLength = kExecutableLength;
			artifact.runtime.engineContractScope =
				std::string(kEngineContractScope);

			const auto& archive = Member(document, "archive", "artifact");
			RequireKeys(
				archive,
				{
					"length",
					"logical_name",
					"member",
					"member_length",
					"member_sha256",
					"sha256"
				},
				"archive");
			RequireStringValue(
				Member(archive, "logical_name", "archive"),
				"Data/Fallout4 - Shaders.ba2",
				"archive.logical_name");
			RequireStringValue(
				Member(archive, "member", "archive"),
				"shadersfx/shaders011.fxp",
				"archive.member");
			RequireStringValue(
				Member(archive, "sha256", "archive"),
				"4ac98b8fe72385f0cd13053bf5e81203fdaabe101377011e152457b705060659",
				"archive.sha256");
			RequireStringValue(
				Member(archive, "member_sha256", "archive"),
				"f3254023504c4bab250162284a28efe2ed79fbf7a9b81e26d0fa9f22660d1d1f",
				"archive.member_sha256");
			if (ReadUnsigned(Member(archive, "length", "archive"), "archive.length")
					!= 12845022
				|| ReadUnsigned(
					Member(archive, "member_length", "archive"),
					"archive.member_length")
					!= 12844936) {
				throw ParseFailure("archive scope is unsupported");
			}

			const auto& runtimeContract =
				Member(document, "runtime_contract", "artifact");
			RequireKeys(
				runtimeContract,
				{
					"input_components",
					"input_register",
					"invariant",
					"resource_register",
					"resource_type",
					"sampler",
					"sampling",
					"shadow_component",
					"shadow_register"
				},
				"runtime_contract");
			RequireStringValue(
				Member(runtimeContract, "input_components", "runtime_contract"),
				"xy",
				"runtime_contract.input_components");
			RequireStringValue(
				Member(runtimeContract, "resource_type", "runtime_contract"),
				"Texture2D<float>",
				"runtime_contract.resource_type");
			RequireStringValue(
				Member(runtimeContract, "sampling", "runtime_contract"),
				"Load",
				"runtime_contract.sampling");
			RequireStringValue(
				Member(runtimeContract, "shadow_component", "runtime_contract"),
				"w",
				"runtime_contract.shadow_component");
			RequireStringValue(
				Member(runtimeContract, "invariant", "runtime_contract"),
				"Every patched draw samples the intended SSS mask from Texture2D<float> t6 using integer SV_POSITION Load.",
				"runtime_contract.invariant");
			if (!Member(runtimeContract, "sampler", "runtime_contract").is_null()
				|| ReadUnsigned(
					Member(runtimeContract, "input_register", "runtime_contract"),
					"runtime_contract.input_register")
					!= 0
				|| ReadUnsigned(
					Member(runtimeContract, "resource_register", "runtime_contract"),
					"runtime_contract.resource_register")
					!= 6
				|| ReadUnsigned(
					Member(runtimeContract, "shadow_register", "runtime_contract"),
					"runtime_contract.shadow_register")
					!= 1) {
				throw ParseFailure("runtime contract is unsupported");
			}

			const auto& fallback = Member(document, "fallback", "artifact");
			RequireKeys(
				fallback,
				{ "build_mismatch", "hash_mismatch", "unknown" },
				"fallback");
			for (const auto key : { "build_mismatch", "hash_mismatch", "unknown" }) {
				RequireStringValue(
					Member(fallback, key, "fallback"),
					"stock",
					std::string("fallback.") + key);
			}

			const auto& denominator =
				Member(document, "denominator", "artifact");
			RequireKeys(
				denominator,
				{
					"candidate_occurrences",
					"candidate_unique_blobs",
					"route_occurrences",
					"route_unique_blobs"
				},
				"denominator");
			if (ReadUnsigned(
					Member(denominator, "candidate_occurrences", "denominator"),
					"denominator.candidate_occurrences")
					!= 133
				|| ReadUnsigned(
					Member(denominator, "candidate_unique_blobs", "denominator"),
					"denominator.candidate_unique_blobs")
					!= 64
				|| ReadUnsigned(
					Member(denominator, "route_occurrences", "denominator"),
					"denominator.route_occurrences")
					!= 306
				|| ReadUnsigned(
					Member(denominator, "route_unique_blobs", "denominator"),
					"denominator.route_unique_blobs")
					!= 166) {
				throw ParseFailure("denominator is unsupported");
			}

			const auto& evidence = Member(document, "evidence", "artifact");
			RequireKeys(
				evidence,
				{
					"census_build_fingerprint",
					"census_run_fingerprint",
					"classification_sha256",
					"proof_index_sha256",
					"route_occurrence_sha256"
				},
				"evidence");
			for (const auto key : {
					"census_build_fingerprint",
					"census_run_fingerprint",
					"classification_sha256",
					"proof_index_sha256",
					"route_occurrence_sha256" }) {
				(void)ReadSha256(
					Member(evidence, key, "evidence"),
					std::string("evidence.") + key);
			}

			const auto& candidates = Member(document, "candidates", "artifact");
			if (!candidates.is_array()
				|| candidates.size() != kExpectedCandidates) {
				throw ParseFailure("candidates does not contain the exact denominator");
			}

			std::map<std::string, ParsedCandidate, std::less<>>
				candidatesByHash;
			std::set<std::tuple<std::string, ShaderStage, std::uint32_t>>
				routeKeys;
			std::set<std::uint32_t> allFxpOrdinals;
			std::string previousCandidateHash;
			std::size_t passCount = 0;
			std::size_t failCount = 0;
			std::size_t unprovenCount = 0;
			std::size_t identityCount = 0;
			for (std::size_t candidateIndex = 0;
				candidateIndex < candidates.size();
				++candidateIndex) {
				const auto& candidate = candidates[candidateIndex];
				RequireKeys(
					candidate,
					{ "action", "fxp_ordinals", "identities", "status", "stock" },
					"candidate");
				const auto stock = ReadStock(
					Member(candidate, "stock", "candidate"),
					"candidate.stock");
				const auto stockHash = sha256::Sha256ToHex(stock.sha256);
				if ((!previousCandidateHash.empty()
						&& previousCandidateHash >= stockHash)
					|| candidatesByHash.contains(stockHash)) {
					throw ParseFailure("candidates are unsorted or duplicated");
				}
				previousCandidateHash = stockHash;

				ParsedCandidate parsed;
				parsed.stock = stock;
				parsed.status = ReadCandidateStatus(
					Member(candidate, "status", "candidate"));
				const auto expectedAction =
					parsed.status == CandidateStatus::kPass
					? "patch"
					: "stock";
				RequireStringValue(
					Member(candidate, "action", "candidate"),
					expectedAction,
					"candidate.action");
				switch (parsed.status) {
				case CandidateStatus::kPass:
					++passCount;
					break;
				case CandidateStatus::kFail:
					++failCount;
					break;
				case CandidateStatus::kUnproven:
					++unprovenCount;
					break;
				}

				const auto& identities =
					Member(candidate, "identities", "candidate");
				const auto& ordinals =
					Member(candidate, "fxp_ordinals", "candidate");
				if (!identities.is_array()
					|| identities.empty()
					|| !ordinals.is_array()
					|| ordinals.size() != identities.size()) {
					throw ParseFailure("candidate routes and FXP ordinals disagree");
				}
				parsed.identities = identities;
				std::optional<std::tuple<
					std::string,
					std::string,
					std::string,
					std::string,
					std::string,
					std::size_t>> previousIdentity;
				for (std::size_t identityIndex = 0;
					identityIndex < identities.size();
					++identityIndex) {
					const auto& identity = identities[identityIndex];
					RequireKeys(
						identity,
						{
							"opaque_psid",
							"stage",
							"stock_length",
							"stock_sha1",
							"stock_sha256",
							"subclass"
						},
						"identity");
					RequireStringValue(
						Member(identity, "subclass", "identity"),
						"BSDFLightShader",
						"identity.subclass");
					RequireStringValue(
						Member(identity, "stage", "identity"),
						"pixel",
						"identity.stage");
					std::string psidText;
					const auto psid = ReadOpaquePsid(
						Member(identity, "opaque_psid", "identity"),
						"identity.opaque_psid",
						psidText);
					const auto identitySha1 = ReadSha1(
						Member(identity, "stock_sha1", "identity"),
						"identity.stock_sha1");
					const auto identitySha256 = ReadSha256(
						Member(identity, "stock_sha256", "identity"),
						"identity.stock_sha256");
					const auto identityLength = ReadSize(
						Member(identity, "stock_length", "identity"),
						"identity.stock_length");
					if (identityLength != stock.length
						|| identitySha1.bytes != stock.sha1.bytes
						|| identitySha256 != stock.sha256) {
						throw ParseFailure("candidate identity disagrees with stock");
					}
					const auto identityKey = std::tuple{
						std::string("BSDFLightShader"),
						std::string("pixel"),
						psidText,
						sha1::Sha1ToHex(identitySha1),
						sha256::Sha256ToHex(identitySha256),
						identityLength
					};
					if (previousIdentity && *previousIdentity >= identityKey)
						throw ParseFailure("candidate identities are unsorted or duplicated");
					previousIdentity = identityKey;
					if (!routeKeys.emplace(
							"BSDFLightShader",
							ShaderStage::kPixel,
							psid).second) {
						throw ParseFailure("candidate route tuple conflicts");
					}
					parsed.opaquePsids.push_back(psidText);
					artifact.routes.push_back({
						.variant = {
							"BSDFLightShader",
							ShaderStage::kPixel,
							ShaderVariantId{ psid }
						},
						.stock = stock,
						.status = parsed.status
					});
					++identityCount;
				}

				std::uint32_t previousOrdinal = 0;
				for (std::size_t ordinalIndex = 0;
					ordinalIndex < ordinals.size();
					++ordinalIndex) {
					const auto ordinalValue = ReadUnsigned(
						ordinals[ordinalIndex],
						"candidate.fxp_ordinal");
					if (ordinalValue < 3072
						|| ordinalValue > 3377
						|| ordinalValue > std::numeric_limits<std::uint32_t>::max()) {
						throw ParseFailure("candidate FXP ordinal is out of range");
					}
					const auto ordinal =
						static_cast<std::uint32_t>(ordinalValue);
					if ((ordinalIndex != 0 && previousOrdinal >= ordinal)
						|| !allFxpOrdinals.insert(ordinal).second) {
						throw ParseFailure("candidate FXP ordinals are unsorted or duplicated");
					}
					previousOrdinal = ordinal;
					parsed.fxpOrdinals.push_back(ordinal);
				}
				candidatesByHash.emplace(stockHash, std::move(parsed));
			}
			if (identityCount != kExpectedCandidateIdentities)
				throw ParseFailure("candidate identities do not match the denominator");

			const auto& plans = Member(document, "plans", "artifact");
			if (!plans.is_array() || plans.empty() || plans.size() != passCount)
				throw ParseFailure("PASS candidates and plans disagree");
			std::string previousPlanHash;
			std::set<std::string, std::less<>> planHashes;
			Json proofIndex = Json::object();
			proofIndex["proofs"] = Json::array();
			for (std::size_t planIndex = 0;
				planIndex < plans.size();
				++planIndex) {
				const auto& rawPlan = plans[planIndex];
				RequireKeys(
					rawPlan,
					{
						"code_insertion",
						"declaration_insertion",
						"executable_chunk_offset",
						"expected_patched",
						"fxp_ordinals",
						"identities",
						"opaque_psids",
						"proof",
						"recipe_id",
						"registers",
						"scratch_proof",
						"static_observation_sha256",
						"static_plan_receipt_sha256",
						"stock",
						"verdict"
					},
					"plan");
				RequireStringValue(
					Member(rawPlan, "verdict", "plan"),
					"PASS",
					"plan.verdict");
				Plan plan;
				plan.stock = ReadStock(
					Member(rawPlan, "stock", "plan"),
					"plan.stock");
				const auto stockHash =
					sha256::Sha256ToHex(plan.stock.sha256);
				if ((!previousPlanHash.empty()
						&& previousPlanHash >= stockHash)
					|| !planHashes.insert(stockHash).second) {
					throw ParseFailure("plans are unsorted or duplicated");
				}
				previousPlanHash = stockHash;
				const auto candidate = candidatesByHash.find(stockHash);
				if (candidate == candidatesByHash.end()
					|| candidate->second.status != CandidateStatus::kPass
					|| !StockEqual(candidate->second.stock, plan.stock)) {
					throw ParseFailure("plan does not identify one PASS candidate");
				}
				if (Member(rawPlan, "identities", "plan")
						!= candidate->second.identities) {
					throw ParseFailure("plan identities disagree with its candidate");
				}

				const auto& planOrdinals =
					Member(rawPlan, "fxp_ordinals", "plan");
				if (!planOrdinals.is_array()
					|| planOrdinals.size()
						!= candidate->second.fxpOrdinals.size()) {
					throw ParseFailure("plan FXP ordinals disagree with its candidate");
				}
				for (std::size_t index = 0;
					index < planOrdinals.size();
					++index) {
					if (ReadUnsigned(
							planOrdinals[index],
							"plan.fxp_ordinal")
						!= candidate->second.fxpOrdinals[index]) {
						throw ParseFailure("plan FXP ordinals disagree with its candidate");
					}
				}

				const auto& planPsids =
					Member(rawPlan, "opaque_psids", "plan");
				if (!planPsids.is_array()
					|| planPsids.size()
						!= candidate->second.opaquePsids.size()) {
					throw ParseFailure("plan PSIDs disagree with its candidate");
				}
				for (std::size_t index = 0;
					index < planPsids.size();
					++index) {
					RequireStringValue(
						planPsids[index],
						candidate->second.opaquePsids[index],
						"plan.opaque_psids");
				}

				plan.recipeId = ReadString(
					Member(rawPlan, "recipe_id", "plan"),
					"plan.recipe_id");
				plan.target = TargetForRecipe(plan.recipeId);
				plan.executableChunkOffset = ReadSize(
					Member(rawPlan, "executable_chunk_offset", "plan"),
					"plan.executable_chunk_offset");
				plan.declaration = ReadInsertion(
					Member(rawPlan, "declaration_insertion", "plan"),
					4,
					"plan.declaration_insertion");
				plan.code = ReadInsertion(
					Member(rawPlan, "code_insertion", "plan"),
					24,
					"plan.code_insertion");
				if (!ValidatePlanRanges(plan))
					throw ParseFailure("plan insertion range is malformed");

				std::size_t executablePayloadOffset = 0;
				if (!CheckedAdd(
						plan.executableChunkOffset,
						8,
						executablePayloadOffset)
					|| plan.declaration.fileOffset
						!= executablePayloadOffset
							+ plan.declaration.executableDword * 4
					|| plan.code.fileOffset
						!= executablePayloadOffset
							+ plan.code.executableDword * 4
					|| plan.declaration.preimage.length != 32
					|| plan.declaration.fileOffset < 16
					|| plan.declaration.preimage.offset
						!= plan.declaration.fileOffset - 16
					|| plan.code.preimage.length != 36
					|| plan.code.preimage.offset + plan.code.preimage.length
						!= plan.code.fileOffset) {
					throw ParseFailure("plan insertion anchors are inconsistent");
				}
				const auto declarationEnd =
					plan.declaration.preimage.offset
					+ plan.declaration.preimage.length;
				const auto codeEnd =
					plan.code.preimage.offset + plan.code.preimage.length;
				if (plan.declaration.preimage.offset < codeEnd
					&& plan.code.preimage.offset < declarationEnd) {
					throw ParseFailure("plan preimage ranges overlap");
				}

				const auto& expected =
					Member(rawPlan, "expected_patched", "plan");
				RequireKeys(
					expected,
					{ "checksum", "length", "sha1", "sha256" },
					"plan.expected_patched");
				plan.expectedChecksum = ReadChecksum(
					Member(expected, "checksum", "plan.expected_patched"),
					"plan.expected_patched.checksum");
				plan.expectedLength = ReadSize(
					Member(expected, "length", "plan.expected_patched"),
					"plan.expected_patched.length");
				plan.expectedSha1 = ReadSha1(
					Member(expected, "sha1", "plan.expected_patched"),
					"plan.expected_patched.sha1");
				plan.expectedSha256 = ReadSha256(
					Member(expected, "sha256", "plan.expected_patched"),
					"plan.expected_patched.sha256");
				std::size_t insertedLength = 0;
				std::size_t expectedLength = 0;
				if (!CheckedAdd(
						plan.declaration.bytes.size(),
						plan.code.bytes.size(),
						insertedLength)
					|| !CheckedAdd(
						plan.stock.length,
						insertedLength,
						expectedLength)
					|| expectedLength != plan.expectedLength
					|| expectedLength > kMaximumShaderBytes) {
					throw ParseFailure("plan patched length is inconsistent");
				}

				const auto& registers =
					Member(rawPlan, "registers", "plan");
				RequireKeys(
					registers,
					{ "declaration", "input", "scratch", "shadow" },
					"plan.registers");
				RequireStringValue(
					Member(registers, "declaration", "plan.registers"),
					"t6",
					"plan.registers.declaration");
				RequireStringValue(
					Member(registers, "input", "plan.registers"),
					"v0.xy",
					"plan.registers.input");
				RequireStringValue(
					Member(registers, "shadow", "plan.registers"),
					"r1.w",
					"plan.registers.shadow");

				const auto& scratchProof =
					Member(rawPlan, "scratch_proof", "plan");
				RequireKeys(
					scratchProof,
					{
						"components",
						"first_overwrite_executable_dwords",
						"register"
					},
					"plan.scratch_proof");
				RequireStringValue(
					Member(scratchProof, "components", "plan.scratch_proof"),
					"xyzw",
					"plan.scratch_proof.components");
				const auto scratchRegister = ReadUnsigned(
					Member(scratchProof, "register", "plan.scratch_proof"),
					"plan.scratch_proof.register");
				if (scratchRegister > 255)
					throw ParseFailure("plan scratch register is out of range");
				RequireStringValue(
					Member(registers, "scratch", "plan.registers"),
					"r" + std::to_string(scratchRegister) + ".xyzw",
					"plan.registers.scratch");
				const auto& overwriteDwords = Member(
					scratchProof,
					"first_overwrite_executable_dwords",
					"plan.scratch_proof");
				if (!overwriteDwords.is_array())
					throw ParseFailure("scratch overwrite dwords must be an array");
				std::optional<std::uint64_t> previousOverwrite;
				for (const auto& overwrite : overwriteDwords) {
					const auto value = ReadUnsigned(
						overwrite,
						"scratch overwrite dword");
					if (previousOverwrite && *previousOverwrite >= value)
						throw ParseFailure("scratch overwrite dwords are unsorted");
					previousOverwrite = value;
				}

				(void)ReadSha256(
					Member(rawPlan, "static_observation_sha256", "plan"),
					"plan.static_observation_sha256");
				plan.receiptSha256 = ReadSha256(
					Member(rawPlan, "static_plan_receipt_sha256", "plan"),
					"plan.static_plan_receipt_sha256");
				Json receiptPlan = Json::object();
				for (const auto key : {
						"recipe_id",
						"stock",
						"opaque_psids",
						"fxp_ordinals",
						"executable_chunk_offset",
						"declaration_insertion",
						"code_insertion",
						"registers",
						"expected_patched",
						"scratch_proof",
						"static_observation_sha256" }) {
					receiptPlan[key] = Member(rawPlan, key, "plan");
				}
				Json receipt = Json::object();
				receipt["plan"] = std::move(receiptPlan);
				receipt["schema"] = "fo4re.sss-dxbc-static-plan";
				receipt["schema_version"] = 1;
				if (DigestJson(receipt) != plan.receiptSha256)
					throw ParseFailure("plan static receipt digest does not match");

				const auto& proof = Member(rawPlan, "proof", "plan");
				RequireKeys(
					proof,
					{
						"contract",
						"disassembly",
						"mutations",
						"numerical",
						"proof_receipt_sha256",
						"warp"
					},
					"plan.proof");
				(void)ReadSha256(
					Member(proof, "proof_receipt_sha256", "plan.proof"),
					"plan.proof.proof_receipt_sha256");
				for (const auto gate : {
						"contract",
						"disassembly",
						"mutations",
						"numerical",
						"warp" }) {
					const auto& gateValue = Member(proof, gate, "plan.proof");
					RequireKeys(
						gateValue,
						{ "sha256", "verdict" },
						"plan.proof.gate");
					RequireStringValue(
						Member(gateValue, "verdict", "plan.proof.gate"),
						"PASS",
						"plan.proof.gate.verdict");
					(void)ReadSha256(
						Member(gateValue, "sha256", "plan.proof.gate"),
						"plan.proof.gate.sha256");
				}
				Json proofEntry = Json::object();
				proofEntry["proof"] = proof;
				proofEntry["stock_sha256"] = stockHash;
				proofIndex["proofs"].push_back(std::move(proofEntry));

				for (auto& route : artifact.routes) {
					if (route.status == CandidateStatus::kPass
						&& route.stock.sha256 == plan.stock.sha256) {
						route.planIndex = planIndex;
					}
				}
				artifact.plans.push_back(std::move(plan));
			}
			for (const auto& route : artifact.routes) {
				if (route.status == CandidateStatus::kPass
					&& !route.planIndex) {
					throw ParseFailure("PASS route has no patch plan");
				}
			}

			const auto expectedProofIndex = ReadSha256(
				Member(evidence, "proof_index_sha256", "evidence"),
				"evidence.proof_index_sha256");
			if (DigestJson(proofIndex) != expectedProofIndex)
				throw ParseFailure("proof index digest does not match");

			const auto& coverage = Member(document, "coverage", "artifact");
			RequireKeys(
				coverage,
				{
					"candidate_blobs",
					"candidate_identities",
					"candidate_occurrences",
					"candidate_unique_blobs",
					"fail",
					"pass",
					"route_occurrences",
					"route_unique_blobs",
					"unproven"
				},
				"coverage");
			const auto requireCoverage =
				[&coverage](std::string_view a_key, std::size_t a_expected) {
					if (ReadUnsigned(
							Member(coverage, a_key, "coverage"),
							std::string("coverage.") + std::string(a_key))
						!= a_expected) {
						throw ParseFailure("coverage does not match artifact contents");
					}
				};
			requireCoverage("candidate_blobs", kExpectedCandidates);
			requireCoverage("candidate_identities", identityCount);
			requireCoverage("candidate_occurrences", 133);
			requireCoverage("candidate_unique_blobs", 64);
			requireCoverage("route_occurrences", 306);
			requireCoverage("route_unique_blobs", 166);
			requireCoverage("pass", passCount);
			requireCoverage("fail", failCount);
			requireCoverage("unproven", unprovenCount);
			if (failCount != 0
				|| passCount + unprovenCount != kExpectedCandidates) {
				throw ParseFailure("provisional coverage accounting is invalid");
			}
			artifact.coverage = {
				.candidates = kExpectedCandidates,
				.identities = identityCount,
				.pass = passCount,
				.fail = failCount,
				.unproven = unprovenCount
			};
			return { std::move(artifact), {} };
		} catch (const std::exception& error) {
			return { std::nullopt, error.what() };
		} catch (...) {
			return { std::nullopt, "artifact parsing failed" };
		}
	}

	ArtifactParseResult LoadPinnedArtifact(
		const std::filesystem::path& a_path,
		std::size_t a_expectedLength,
		const sha256::Sha256Result& a_expectedSha256)
	{
		try {
			if (a_expectedLength == 0
				|| a_expectedLength > kMaximumArtifactBytes) {
				return { std::nullopt, "pinned artifact length is invalid" };
			}
			std::ifstream input(a_path, std::ios::binary);
			if (!input.is_open())
				return { std::nullopt, "pinned artifact could not be opened" };
			std::string bytes(a_expectedLength, '\0');
			input.read(
				bytes.data(),
				static_cast<std::streamsize>(bytes.size()));
			if (input.gcount() != static_cast<std::streamsize>(bytes.size())
				|| input.bad()) {
				return { std::nullopt, "pinned artifact read was incomplete" };
			}
			char extra = 0;
			if (input.read(&extra, 1) || !input.eof())
				return { std::nullopt, "pinned artifact length does not match" };
			const auto digest =
				sha256::Sha256Compute(bytes.data(), bytes.size());
			if (digest != a_expectedSha256)
				return { std::nullopt, "pinned artifact SHA-256 does not match" };
			return ParseArtifact(bytes);
		} catch (const std::exception& error) {
			return { std::nullopt, error.what() };
		} catch (...) {
			return { std::nullopt, "pinned artifact load failed" };
		}
	}

	const CandidateRoute* FindCandidateRoute(
		const Artifact& a_artifact,
		ShaderVariantKeyView a_variant) noexcept
	{
		const auto route = std::ranges::find_if(
			a_artifact.routes,
			[a_variant](const CandidateRoute& a_candidate) {
				return ViewShaderVariantKey(a_candidate.variant) == a_variant;
			});
		return route == a_artifact.routes.end() ? nullptr : &*route;
	}

	bool RuntimeMatches(
		const Artifact& a_artifact,
		const RuntimeIdentity& a_runtime) noexcept
	{
		return a_artifact.runtime == a_runtime;
	}

	bool StockMatches(
		const StockIdentity& a_expected,
		std::size_t a_length,
		const sha1::Sha1Result& a_sha1,
		const sha256::Sha256Result& a_sha256) noexcept
	{
		return a_expected.length == a_length
			&& a_expected.sha1.bytes == a_sha1.bytes
			&& a_expected.sha256 == a_sha256;
	}

	std::optional<std::array<std::uint8_t, 16>> ComputeDxbcChecksum(
		std::span<const std::byte> a_bytecode) noexcept
	{
		if (a_bytecode.size() < 20
			|| std::memcmp(a_bytecode.data(), "DXBC", 4) != 0) {
			return std::nullopt;
		}

		const auto data = a_bytecode.subspan(20);
		const auto dataLength = data.size();
		const auto bitLength = static_cast<std::uint32_t>(
			(dataLength * 8) & 0xFFFFFFFFu);
		const auto terminator =
			static_cast<std::uint32_t>((bitLength >> 2) | 1);
		std::array<std::uint32_t, 4> state{
			0x67452301,
			0xEFCDAB89,
			0x98BADCFE,
			0x10325476
		};
		const auto fullLength = dataLength - dataLength % 64;
		for (std::size_t offset = 0; offset < fullLength; offset += 64) {
			state = CompressChecksum(
				state,
				std::span<const std::byte, 64>(
					data.data() + offset,
					64));
		}

		const auto tail = data.subspan(fullLength);
		if (tail.size() < 56) {
			std::array<std::byte, 64> block{};
			WriteU32(block, 0, bitLength);
			std::copy(tail.begin(), tail.end(), block.begin() + 4);
			block[4 + tail.size()] = std::byte{ 0x80 };
			WriteU32(block, 60, terminator);
			state = CompressChecksum(state, block);
		} else {
			std::array<std::byte, 64> first{};
			std::copy(tail.begin(), tail.end(), first.begin());
			first[tail.size()] = std::byte{ 0x80 };
			state = CompressChecksum(state, first);
			std::array<std::byte, 64> second{};
			WriteU32(second, 0, bitLength);
			WriteU32(second, 60, terminator);
			state = CompressChecksum(state, second);
		}

		std::array<std::uint8_t, 16> result{};
		for (std::size_t index = 0; index < state.size(); ++index) {
			for (unsigned shift = 0; shift < 32; shift += 8) {
				result[index * 4 + shift / 8] =
					static_cast<std::uint8_t>(
						(state[index] >> shift) & 0xFF);
			}
		}
		return result;
	}

	bool RewriteDxbcChecksum(std::span<std::byte> a_bytecode) noexcept
	{
		const auto checksum = ComputeDxbcChecksum(a_bytecode);
		if (!checksum)
			return false;
		for (std::size_t index = 0; index < checksum->size(); ++index)
			a_bytecode[4 + index] = static_cast<std::byte>((*checksum)[index]);
		return true;
	}

	bool ValidateDxbcChecksum(
		std::span<const std::byte> a_bytecode) noexcept
	{
		const auto checksum = ComputeDxbcChecksum(a_bytecode);
		if (!checksum)
			return false;
		for (std::size_t index = 0; index < checksum->size(); ++index) {
			if (std::to_integer<std::uint8_t>(a_bytecode[4 + index])
				!= (*checksum)[index]) {
				return false;
			}
		}
		return true;
	}

	PatchBuildResult BuildPatchedDxbc(
		std::span<const std::byte> a_stockBytecode,
		const Plan& a_plan) noexcept
	{
		sha1::Sha1InitOnce();
		const auto stockSha1 =
			sha1::Sha1Compute(a_stockBytecode.data(), a_stockBytecode.size());
		const auto stockSha256 =
			sha256::Sha256Compute(a_stockBytecode.data(), a_stockBytecode.size());
		if (!StockMatches(
				a_plan.stock,
				a_stockBytecode.size(),
				stockSha1,
				stockSha256)) {
			return { PatchBuildStatus::kStockMismatch, {} };
		}
		if (!ValidatePlanRanges(a_plan))
			return { PatchBuildStatus::kMalformedDxbc, {} };
		const auto layout = ParseDxbc(a_stockBytecode);
		if (!layout
			|| layout->chunks[layout->executableIndex].offset
				!= a_plan.executableChunkOffset) {
			return { PatchBuildStatus::kMalformedDxbc, {} };
		}
		const auto& stockExecutable =
			layout->chunks[layout->executableIndex];
		const auto executablePayloadBegin =
			stockExecutable.offset + 8;
		const auto executablePayloadEnd =
			executablePayloadBegin + stockExecutable.payloadSize;

		for (const auto* insertion :
			{ &a_plan.declaration, &a_plan.code }) {
			const auto preimageEnd =
				insertion->preimage.offset
				+ insertion->preimage.length;
			if (insertion->fileOffset < executablePayloadBegin
				|| insertion->fileOffset > executablePayloadEnd
				|| insertion->preimage.offset < executablePayloadBegin
				|| preimageEnd > executablePayloadEnd) {
				return { PatchBuildStatus::kMalformedDxbc, {} };
			}
			const auto preimage = a_stockBytecode.subspan(
				insertion->preimage.offset,
				insertion->preimage.length);
			const auto digest =
				sha256::Sha256Compute(preimage.data(), preimage.size());
			if (digest != insertion->preimage.sha256)
				return { PatchBuildStatus::kPreimageMismatch, {} };
		}

		try {
			std::vector<std::byte> patched(
				a_stockBytecode.begin(),
				a_stockBytecode.end());
			std::array<const Insertion*, 2> insertions{
				&a_plan.declaration,
				&a_plan.code
			};
			std::ranges::sort(
				insertions,
				[](const Insertion* a_left, const Insertion* a_right) {
					return a_left->fileOffset > a_right->fileOffset;
				});
			for (const auto* insertion : insertions) {
				patched.insert(
					patched.begin()
						+ static_cast<std::ptrdiff_t>(insertion->fileOffset),
					insertion->bytes.begin(),
					insertion->bytes.end());
			}

			if (patched.size() > std::numeric_limits<std::uint32_t>::max())
				return { PatchBuildStatus::kMalformedDxbc, {} };
			WriteU32(
				patched,
				24,
				static_cast<std::uint32_t>(patched.size()));
			for (std::size_t index = 0;
				index < layout->chunks.size();
				++index) {
				const auto shifted =
					ShiftedOffset(layout->chunks[index].offset, a_plan);
				if (shifted > std::numeric_limits<std::uint32_t>::max())
					return { PatchBuildStatus::kMalformedDxbc, {} };
				WriteU32(
					patched,
					32 + index * 4,
					static_cast<std::uint32_t>(shifted));
			}

			const auto& executable =
				layout->chunks[layout->executableIndex];
			const auto insertedBytes =
				a_plan.declaration.bytes.size() + a_plan.code.bytes.size();
			if (insertedBytes > std::numeric_limits<std::uint32_t>::max()
				|| executable.payloadSize
					> std::numeric_limits<std::uint32_t>::max()
						- insertedBytes) {
				return { PatchBuildStatus::kMalformedDxbc, {} };
			}
			WriteU32(
				patched,
				executable.offset + 4,
				static_cast<std::uint32_t>(
					executable.payloadSize + insertedBytes));
			const auto oldDwordLength =
				ReadU32(a_stockBytecode, executable.offset + 12);
			const auto insertedDwords = insertedBytes / 4;
			if (oldDwordLength
				> std::numeric_limits<std::uint32_t>::max()
					- insertedDwords) {
				return { PatchBuildStatus::kMalformedDxbc, {} };
			}
			WriteU32(
				patched,
				executable.offset + 12,
				static_cast<std::uint32_t>(
					oldDwordLength + insertedDwords));
			if (!RewriteDxbcChecksum(patched))
				return { PatchBuildStatus::kMalformedDxbc, {} };

			const bool expectedLength =
				patched.size() == a_plan.expectedLength;
			bool expectedChecksum = true;
			for (std::size_t index = 0;
				index < a_plan.expectedChecksum.size();
				++index) {
				expectedChecksum = expectedChecksum
					&& std::to_integer<std::uint8_t>(patched[4 + index])
						== a_plan.expectedChecksum[index];
			}
			const auto patchedSha1 =
				sha1::Sha1Compute(patched.data(), patched.size());
			const auto patchedSha256 =
				sha256::Sha256Compute(patched.data(), patched.size());
			if (!expectedLength
				|| !expectedChecksum
				|| patchedSha1.bytes != a_plan.expectedSha1.bytes
				|| patchedSha256 != a_plan.expectedSha256) {
				return {
					PatchBuildStatus::kOutputMismatch,
					std::move(patched)
				};
			}

			const auto patchedLayout = ParseDxbc(patched);
			if (!patchedLayout
				|| patchedLayout->chunks.size() != layout->chunks.size()) {
				return {
					PatchBuildStatus::kOutputMismatch,
					std::move(patched)
				};
			}
			for (std::size_t index = 0;
				index < layout->chunks.size();
				++index) {
				if (index == layout->executableIndex)
					continue;
				const auto& original = layout->chunks[index];
				const auto& replacement = patchedLayout->chunks[index];
				if (original.fourcc != replacement.fourcc
					|| original.payloadSize != replacement.payloadSize
					|| !std::equal(
						a_stockBytecode.begin()
							+ static_cast<std::ptrdiff_t>(original.offset),
						a_stockBytecode.begin()
							+ static_cast<std::ptrdiff_t>(
								original.offset + 8 + original.payloadSize),
						patched.begin()
							+ static_cast<std::ptrdiff_t>(replacement.offset))) {
					return {
						PatchBuildStatus::kOutputMismatch,
						std::move(patched)
					};
				}
			}
			return {
				PatchBuildStatus::kSuccess,
				std::move(patched)
			};
		} catch (const std::bad_alloc&) {
			return { PatchBuildStatus::kAllocationFailure, {} };
		} catch (...) {
			return { PatchBuildStatus::kMalformedDxbc, {} };
		}
	}
}
