#pragma once

#include "Format/PbrSchema.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace cs::features::true_pbr
{
	using PbrParameters = std::array<float, kParameterCount>;

	// Upstream PBRThinSurface; replaces four parameters.
	struct ThinSurfaceOverride
	{
		float subsurfaceBalance = 0.0f;
		float frontSpecularWeight = 0.0f;
		float backSpecularWeight = 0.0f;
		float backRoughnessScale = 0.0f;

		bool operator==(const ThinSurfaceOverride&) const = default;
	};

	// Upstream PBRFuzz; its values are not among the 25 parameters.
	struct FuzzOverride
	{
		float roughness = -1.0f;  // negative keeps the base roughness
		bool roughnessFromDisplacementGreen = false;

		bool operator==(const FuzzOverride&) const = default;
	};

	constexpr PbrParameters DefaultParameters()
	{
		PbrParameters values{};
		std::size_t index = 0;
		for (const auto& parameter : kParameters)
			for (std::uint8_t component = 0; component < parameter.count; ++component)
				values[index++] = parameter.defaultValue;
		return values;
	}

	struct PbrMaterial
	{
		std::uint32_t tileFlags = 0;
		std::array<float, 2> uvOffset{};
		std::array<float, 2> uvScale{};
		float alpha = 1.0f;
		std::uint8_t alphaBlend = 0;
		std::uint32_t blendSource = 0;
		std::uint32_t blendDestination = 0;
		std::uint8_t alphaTestRef = 0;
		bool alphaTest = false;
		bool refraction = false;
		bool refractionFalloff = false;
		float refractionPower = 0.0f;
		bool modelSpaceNormals = false;
		bool emitEnabled = false;
		std::array<float, 3> emittanceColor{};
		float emittanceMult = 1.0f;
		bool externalEmittance = false;
		std::array<std::string, kTextureSlots.size()> textures;
		std::uint32_t payloadVersion = kPayloadVersion;
		std::uint32_t featureFlags = 0;
		PbrParameters parameters = DefaultParameters();
		std::optional<ThinSurfaceOverride> thinSurface;
		std::optional<FuzzOverride> fuzz;

		[[nodiscard]] const std::string& Texture(PbrTexture a_slot) const noexcept
		{
			return textures[static_cast<std::size_t>(a_slot)];
		}

		// TruePBR 2.0 order: the block's values, then the overrides.
		[[nodiscard]] PbrParameters ResolvedParameters() const noexcept
		{
			auto resolved = parameters;
			if (thinSurface) {
				constexpr auto first = ParameterIndex("subsurfaceBalance");
				static_assert(ParameterIndex("thinSurfaceBackRoughnessScale") == first + 3);
				resolved[first] = thinSurface->subsurfaceBalance;
				resolved[first + 1] = thinSurface->frontSpecularWeight;
				resolved[first + 2] = thinSurface->backSpecularWeight;
				resolved[first + 3] = thinSurface->backRoughnessScale;
			}
			return resolved;
		}

		bool operator==(const PbrMaterial&) const = default;
	};
}
