#pragma once

#include "../../package/Shaders/FO4/PrepassLane.h"
#include "Render/ObjectLOD.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace RE
{
	class BSLightingShaderProperty;
	class BSRenderPass;
	class NiTexture;
}

namespace cs
{
	class Feature;
}

namespace cs::telemetry
{
	class Sink;
}

namespace cs::engine
{
	enum class PrepassBakePath
	{
		kCommandBuffer,  // once, when the pass is created
		kImmediate       // every frame, from BSDFPrePassShader::SetupGeometry
	};

	enum class PrepassDrawFlag : std::uint32_t
	{
		kObjectLOD = PrepassLaneFlag::ObjectLOD,
		kTerrainVariation = PrepassLaneFlag::TerrainVariation,
		kExtendedMaterials = PrepassLaneFlag::ExtendedMaterials,
		kHumanSkin = PrepassLaneFlag::HumanSkin
	};

	struct PrepassLanePayload
	{
		std::uint32_t flags = 0;
		std::uint32_t recordId = 0;
		// Float bits of the live emissive multiplier; set with a record id.
		std::uint32_t emissiveScaleBits = 0;
		// PrepassLaneTexture bits of the record textures bound for this draw.
		std::uint32_t texturesBound = 0;
	};
	static_assert(sizeof(PrepassLanePayload) == 16);

	// Computes each shared draw fact at most once.
	class PrepassDrawContext
	{
	public:
		PrepassDrawContext(RE::BSRenderPass* a_pass, PrepassBakePath a_path) noexcept :
			pass(a_pass), path(a_path) {}

		[[nodiscard]] bool InObjectLOD() noexcept
		{
			if (!_inObjectLOD)
				_inObjectLOD = IsObjectLODShape(pass);
			return *_inObjectLOD;
		}

		[[nodiscard]] RE::BSLightingShaderProperty* LightingProperty() noexcept;

		// Interface3D has no extra targets; baked draws count as world.
		[[nodiscard]] bool InWorldPrepass() const noexcept;

		RE::BSRenderPass* pass;
		PrepassBakePath path;

	private:
		std::optional<bool> _inObjectLOD;
		std::optional<RE::BSLightingShaderProperty*> _lightingProperty;
	};

	enum class PrepassRecordTexture : std::size_t
	{
		kDisplacement,
		kFeatures0,
		kFeatures1,
		kCount
	};

	// Record textures keep a draw immediate; null entries are not bound.
	struct PrepassRecord
	{
		std::uint16_t id = 0;  // 0 is none
		std::array<RE::NiTexture*, static_cast<std::size_t>(PrepassRecordTexture::kCount)> textures{};

		[[nodiscard]] bool HasTextures() const noexcept
		{
			for (const auto* texture : textures) {
				if (texture)
					return true;
			}
			return false;
		}
	};

	using PrepassDrawClassifier = bool (*)(PrepassDrawContext& a_draw) noexcept;
	using PrepassRecordClassifier = PrepassRecord (*)(PrepassDrawContext& a_draw) noexcept;

	struct PrepassLaneStats
	{
		std::uint64_t baked = 0;          // draws whose lane was written
		std::uint64_t unavailable = 0;    // landscape excluded
		std::uint64_t textured = 0;       // immediate draws that bound a record texture
		std::uint64_t keptImmediate = 0;  // bakes skipped for record textures
	};

	struct PrepassClassifierStats
	{
		std::uint64_t classified = 0;
		std::uint64_t flagged = 0;
	};

	// Baked records ignore enablement; immediate draws skip disabled owners.
	[[nodiscard]] bool RegisterPrepassDrawClassifier(Feature& a_owner, PrepassDrawFlag a_flag, PrepassDrawClassifier a_classifier);
	[[nodiscard]] bool RegisterPrepassRecordClassifier(Feature& a_owner, PrepassRecordClassifier a_classifier);
	[[nodiscard]] PrepassLaneStats GetPrepassLaneStats() noexcept;
	[[nodiscard]] PrepassClassifierStats GetPrepassClassifierStats(PrepassDrawFlag a_flag, PrepassBakePath a_path) noexcept;

	void WritePrepassLaneTelemetry(telemetry::Sink& a_sink, PrepassDrawFlag a_flag, std::string_view a_flagged);
}
