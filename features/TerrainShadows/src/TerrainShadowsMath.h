#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cs::features::terrain_shadows
{
	inline constexpr float kCellSize = 4096.0f;
	inline constexpr float kHeightQuantum = 8.0f;
	// xLODGen stores game Z as (sample - 32767) * 8.
	inline constexpr float kXLodGenMidpoint = 32767.0f;

	inline constexpr std::uint32_t kUpdateLength = 128;

	inline constexpr float kGameHourJumpThreshold = 0.25f;

	inline constexpr std::array<std::uint32_t, 3> kDownsampleFactors{ 1u, 2u, 4u };

	enum class HeightMapSource : std::uint8_t
	{
		kXLodGen,
		kCustom
	};

	[[nodiscard]] inline std::string_view SourceName(HeightMapSource a_source) noexcept
	{
		return a_source == HeightMapSource::kXLodGen ? "xlodgen" : "custom";
	}

	struct HeightMapMetadata
	{
		std::string     worldspace;
		HeightMapSource source = HeightMapSource::kXLodGen;
		std::array<float, 3> pos0{};
		std::array<float, 3> pos1{};
		std::array<float, 2> zRange{};
	};

	[[nodiscard]] inline std::vector<std::string_view> SplitFields(
		std::string_view a_stem,
		char a_separator = '.')
	{
		std::vector<std::string_view> fields;
		std::size_t cursor = 0;
		while (true) {
			const auto next = a_stem.find(a_separator, cursor);
			if (next == std::string_view::npos) {
				fields.push_back(a_stem.substr(cursor));
				break;
			}
			fields.push_back(a_stem.substr(cursor, next - cursor));
			cursor = next + 1;
		}
		return fields;
	}

	[[nodiscard]] inline bool EqualsIgnoreCase(
		std::string_view a_left,
		std::string_view a_right) noexcept
	{
		if (a_left.size() != a_right.size())
			return false;
		for (std::size_t index = 0; index < a_left.size(); ++index) {
			const auto left = static_cast<unsigned char>(a_left[index]);
			const auto right = static_cast<unsigned char>(a_right[index]);
			const unsigned lowerLeft =
				left >= 'A' && left <= 'Z' ? left + 32u : left;
			const unsigned lowerRight =
				right >= 'A' && right <= 'Z' ? right + 32u : right;
			if (lowerLeft != lowerRight)
				return false;
		}
		return true;
	}

	[[nodiscard]] inline bool ParseInteger(
		std::string_view a_field,
		std::int32_t& a_value) noexcept
	{
		if (a_field.empty())
			return false;
		const auto* first = a_field.data();
		const auto* last = first + a_field.size();
		const auto result = std::from_chars(first, last, a_value);
		return result.ec == std::errc{} && result.ptr == last;
	}

	[[nodiscard]] inline bool IsMetadataConsistent(
		const HeightMapMetadata& a_metadata) noexcept
	{
		const bool finite =
			std::isfinite(a_metadata.pos0[0]) && std::isfinite(a_metadata.pos0[1])
			&& std::isfinite(a_metadata.pos0[2]) && std::isfinite(a_metadata.pos1[0])
			&& std::isfinite(a_metadata.pos1[1]) && std::isfinite(a_metadata.pos1[2])
			&& std::isfinite(a_metadata.zRange[0])
			&& std::isfinite(a_metadata.zRange[1]);
		return finite
			&& !a_metadata.worldspace.empty()
			&& a_metadata.pos1[0] > a_metadata.pos0[0]
			&& a_metadata.pos0[1] > a_metadata.pos1[1]
			&& a_metadata.pos1[2] > a_metadata.pos0[2]
			&& a_metadata.zRange[1] > a_metadata.zRange[0];
	}

	[[nodiscard]] inline std::optional<HeightMapMetadata> ParseHeightMapStem(
		std::string_view a_stem,
		HeightMapSource a_source)
	{
		const auto fields = SplitFields(a_stem);
		const bool xlodgen = a_source == HeightMapSource::kXLodGen;
		if (fields.size() != (xlodgen ? 9u : 10u))
			return std::nullopt;
		if (xlodgen) {
			if (!EqualsIgnoreCase(fields[1], "Terrain")
				|| !EqualsIgnoreCase(fields[2], "HeightMap")) {
				return std::nullopt;
			}
		} else if (!EqualsIgnoreCase(fields[1], "HeightMap")) {
			return std::nullopt;
		}

		const std::size_t base = xlodgen ? 3u : 2u;
		std::int32_t west = 0;
		std::int32_t south = 0;
		std::int32_t east = 0;
		std::int32_t north = 0;
		// Current xLODGen filenames use W, S, E, N.
		if (!ParseInteger(fields[base], west)
			|| !ParseInteger(fields[base + 1], south)
			|| !ParseInteger(fields[base + 2], east)
			|| !ParseInteger(fields[base + 3], north)) {
			return std::nullopt;
		}

		HeightMapMetadata metadata;
		metadata.worldspace = std::string(fields[0]);
		metadata.source = a_source;
		metadata.pos0[0] = static_cast<float>(west) * kCellSize;
		metadata.pos1[1] = static_cast<float>(south) * kCellSize;
		metadata.pos1[0] = static_cast<float>(east + 1) * kCellSize;
		metadata.pos0[1] = static_cast<float>(north + 1) * kCellSize;

		if (xlodgen) {
			std::int32_t minZ = 0;
			std::int32_t maxZ = 0;
			if (!ParseInteger(fields[7], minZ) || !ParseInteger(fields[8], maxZ))
				return std::nullopt;
			metadata.pos0[2] = -kXLodGenMidpoint * kHeightQuantum;
			metadata.pos1[2] = kXLodGenMidpoint * kHeightQuantum;
			metadata.zRange[0] = static_cast<float>(minZ) * kHeightQuantum;
			metadata.zRange[1] = static_cast<float>(maxZ) * kHeightQuantum;
		} else {
			std::int32_t zBlack = 0;
			std::int32_t zWhite = 0;
			std::int32_t minZ = 0;
			std::int32_t maxZ = 0;
			if (!ParseInteger(fields[6], zBlack)
				|| !ParseInteger(fields[7], zWhite)
				|| !ParseInteger(fields[8], minZ)
				|| !ParseInteger(fields[9], maxZ)) {
				return std::nullopt;
			}
			metadata.pos0[2] = static_cast<float>(zBlack) * kHeightQuantum;
			metadata.pos1[2] = static_cast<float>(zWhite) * kHeightQuantum;
			metadata.zRange[0] = static_cast<float>(minZ) * kHeightQuantum;
			metadata.zRange[1] = static_cast<float>(maxZ) * kHeightQuantum;
		}

		if (!IsMetadataConsistent(metadata))
			return std::nullopt;
		return metadata;
	}

	[[nodiscard]] inline bool IsValidDownsampleFactor(std::uint64_t a_factor) noexcept
	{
		return std::ranges::any_of(
			kDownsampleFactors,
			[a_factor](std::uint32_t a_candidate) {
				return static_cast<std::uint64_t>(a_candidate) == a_factor;
			});
	}

	[[nodiscard]] inline std::uint32_t ApplyDownsample(
		std::uint32_t a_dimension,
		std::uint32_t a_factor) noexcept
	{
		if (a_factor <= 1 || a_dimension == 0)
			return a_dimension;
		return std::max(1u, a_dimension / a_factor);
	}

	struct VramCost
	{
		std::uint64_t heightBytes = 0;
		std::uint64_t shadowBytes = 0;
		std::uint64_t totalBytes = 0;
	};

	[[nodiscard]] inline VramCost ComputeVramCost(
		std::uint32_t a_width,
		std::uint32_t a_height) noexcept
	{
		const auto pixels =
			static_cast<std::uint64_t>(a_width) * static_cast<std::uint64_t>(a_height);
		VramCost cost;
		cost.heightBytes = pixels * 2u;
		cost.shadowBytes = pixels * 4u;
		cost.totalBytes = cost.heightBytes + cost.shadowBytes;
		return cost;
	}

	[[nodiscard]] inline double BytesToMiB(std::uint64_t a_bytes) noexcept
	{
		return static_cast<double>(a_bytes) / (1024.0 * 1024.0);
	}

	struct FeatureBlock
	{
		std::uint32_t        enableTerrainShadow = 0;
		std::array<float, 3> scale{};
		std::array<float, 2> zRange{};
		std::array<float, 2> offset{};
	};

	[[nodiscard]] inline FeatureBlock BuildFeatureBlock(
		const HeightMapMetadata& a_metadata,
		bool a_enabled) noexcept
	{
		FeatureBlock block;
		block.enableTerrainShadow = a_enabled ? 1u : 0u;
		const std::array<float, 3> invScale{
			a_metadata.pos1[0] - a_metadata.pos0[0],
			a_metadata.pos1[1] - a_metadata.pos0[1],
			a_metadata.pos1[2] - a_metadata.pos0[2]
		};
		for (std::size_t axis = 0; axis < 3; ++axis)
			block.scale[axis] = invScale[axis] != 0.0f ? 1.0f / invScale[axis] : 0.0f;
		block.offset[0] = -a_metadata.pos0[0] * block.scale[0];
		block.offset[1] = -a_metadata.pos0[1] * block.scale[1];
		block.zRange = a_metadata.zRange;
		return block;
	}

	struct DdaPlan
	{
		bool                 valid = false;
		bool                 vertical = false;
		std::array<float, 2> lightPxDir{};
		std::array<float, 2> lightDeltaZ{};
		std::uint32_t        edgePxCoord = 0;
		std::int32_t         signDir = 1;
		std::uint32_t        maxUpdates = 1;
		std::uint32_t        dispatchCount = 0;
		std::uint32_t        majorDimension = 0;
	};

	inline constexpr float kPi = 3.14159265358979323846f;
	inline constexpr float kShadowSofteningRadians = kPi / 180.0f;
	inline constexpr float kHalfPi = kPi / 2.0f;
	inline constexpr float kMinHorizontalLength = 1e-4f;

	[[nodiscard]] inline DdaPlan BuildDdaPlan(
		const std::array<float, 3>& a_sunDirection,
		const HeightMapMetadata& a_metadata,
		std::uint32_t a_width,
		std::uint32_t a_height) noexcept
	{
		DdaPlan plan;
		if (a_width == 0 || a_height == 0 || !IsMetadataConsistent(a_metadata))
			return plan;
		if (!std::isfinite(a_sunDirection[0]) || !std::isfinite(a_sunDirection[1])
			|| !std::isfinite(a_sunDirection[2])) {
			return plan;
		}

		const float horizontalLength = std::sqrt(
			a_sunDirection[0] * a_sunDirection[0]
			+ a_sunDirection[1] * a_sunDirection[1]);
		if (horizontalLength < kMinHorizontalLength)
			return plan;

		const std::array<float, 3> invScale{
			a_metadata.pos1[0] - a_metadata.pos0[0],
			a_metadata.pos1[1] - a_metadata.pos0[1],
			a_metadata.zRange[1] - a_metadata.zRange[0]
		};
		const std::array<float, 2> pixelDir{
			a_sunDirection[0] / invScale[0] * static_cast<float>(a_width),
			a_sunDirection[1] / invScale[1] * static_cast<float>(a_height)
		};

		const bool horizontalMajor =
			std::abs(pixelDir[0]) >= std::abs(pixelDir[1]);
		const float major = horizontalMajor ? pixelDir[0] : pixelDir[1];
		if (!(std::abs(major) > 0.0f))
			return plan;

		const float stepMult = 1.0f / std::abs(major);
		const std::uint32_t majorDimension = horizontalMajor ? a_width : a_height;
		plan.vertical = !horizontalMajor;
		plan.edgePxCoord = major > 0.0f ? 0u : majorDimension - 1u;
		plan.signDir = major > 0.0f ? 1 : -1;
		plan.maxUpdates = std::max(
			1u,
			(majorDimension + kUpdateLength - 1u) / kUpdateLength);
		plan.dispatchCount = horizontalMajor ? a_height : a_width;
		plan.majorDimension = majorDimension;
		plan.lightPxDir = { pixelDir[0] * stepMult, pixelDir[1] * stepMult };

		const float lightAngle = std::atan2(-a_sunDirection[2], horizontalLength);
		const float upperAngle = std::max(0.0f, lightAngle - kShadowSofteningRadians);
		const float lowerAngle = std::min(
			kHalfPi - 1e-2f,
			lightAngle + kShadowSofteningRadians);
		const float deltaScale = -(horizontalLength / invScale[2]) * stepMult;
		plan.lightDeltaZ = {
			deltaScale * std::tan(upperAngle),
			deltaScale * std::tan(lowerAngle)
		};
		if (!std::isfinite(plan.lightPxDir[0]) || !std::isfinite(plan.lightPxDir[1])
			|| !std::isfinite(plan.lightDeltaZ[0])
			|| !std::isfinite(plan.lightDeltaZ[1])) {
			return DdaPlan{};
		}
		plan.valid = true;
		return plan;
	}

	[[nodiscard]] inline std::uint32_t SliceStartCoord(
		const DdaPlan& a_plan,
		std::uint32_t a_updateIndex) noexcept
	{
		if (a_plan.majorDimension == 0)
			return 0;
		const auto start = static_cast<std::int64_t>(a_plan.edgePxCoord)
			+ static_cast<std::int64_t>(a_plan.signDir)
				* static_cast<std::int64_t>(a_updateIndex)
				* static_cast<std::int64_t>(kUpdateLength);
		const auto clamped = std::clamp<std::int64_t>(
			start,
			0,
			static_cast<std::int64_t>(a_plan.majorDimension) - 1);
		return static_cast<std::uint32_t>(clamped);
	}

	[[nodiscard]] inline bool IsGameHourJump(
		float a_previous,
		float a_current,
		float a_threshold = kGameHourJumpThreshold) noexcept
	{
		if (!std::isfinite(a_previous) || !std::isfinite(a_current))
			return false;
		const float delta = a_current - a_previous;
		if (delta >= 0.0f)
			return delta > a_threshold;
		return (delta + 24.0f) > a_threshold;
	}

	struct BootstrapReadiness
	{
		bool registrationsInstalled = false;
		bool renderCallbacksInstalled = false;
		bool computeShaderReady = false;
		bool samplerReady = false;
		bool constantBufferReady = false;
	};

	[[nodiscard]] constexpr bool IsReadyForInjectionFreeze(
		const BootstrapReadiness& a_readiness) noexcept
	{
		return a_readiness.registrationsInstalled
			&& a_readiness.renderCallbacksInstalled
			&& a_readiness.computeShaderReady
			&& a_readiness.samplerReady
			&& a_readiness.constantBufferReady;
	}

	[[nodiscard]] inline std::string MissingBootstrapPrerequisites(
		const BootstrapReadiness& a_readiness)
	{
		std::string missing;
		const auto append = [&missing](bool a_ready, std::string_view a_name) {
			if (a_ready)
				return;
			if (!missing.empty())
				missing += ", ";
			missing += a_name;
		};
		append(a_readiness.registrationsInstalled, "shader replacement registration");
		append(a_readiness.renderCallbacksInstalled, "render callbacks");
		append(a_readiness.computeShaderReady, "shadow update compute shader");
		append(a_readiness.samplerReady, "dedicated sampler");
		append(a_readiness.constantBufferReady, "shadow update constant buffer");
		return missing;
	}
}
