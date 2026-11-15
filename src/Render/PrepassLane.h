#pragma once

#include "../../package/Shaders/FO4/PrepassLane.h"
#include "Render/ObjectLOD.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace RE
{
	class BSLightingShaderProperty;
	class BSRenderPass;
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
	// The engine path that baked a prepass draw's per-draw constants.
	enum class PrepassBakePath
	{
		kCommandBuffer,  // once, when the pass is created
		kImmediate       // every frame, from BSDFPrePassShader::SetupGeometry
	};

	// One classifier owns each flag bit of PrepassFeatureLane.x.
	enum class PrepassDrawFlag : std::uint32_t
	{
		kObjectLOD = PrepassLaneFlag::ObjectLOD,
		kTerrainVariation = PrepassLaneFlag::TerrainVariation,
		kExtendedMaterials = PrepassLaneFlag::ExtendedMaterials,
		kHumanSkin = PrepassLaneFlag::HumanSkin
	};

	// The 16 bytes written over PrepassFeatureLane.
	struct PrepassLanePayload
	{
		std::uint32_t flags = 0;
		std::uint32_t recordId = 0;
		std::uint32_t reserved[2]{};
	};
	static_assert(sizeof(PrepassLanePayload) == 16);

	// Per-draw facts shared by every classifier; each is computed at most once.
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

		// The pass's lighting shader property; null for other shader properties.
		[[nodiscard]] RE::BSLightingShaderProperty* LightingProperty() noexcept;

		RE::BSRenderPass* pass;
		PrepassBakePath path;

	private:
		std::optional<bool> _inObjectLOD;
		std::optional<RE::BSLightingShaderProperty*> _lightingProperty;
	};

	using PrepassDrawClassifier = bool (*)(PrepassDrawContext& a_draw) noexcept;
	// Returns the draw's material record id; 0 is none.
	using PrepassRecordClassifier = std::uint16_t (*)(PrepassDrawContext& a_draw) noexcept;

	struct PrepassLaneStats
	{
		std::uint64_t baked = 0;        // draws whose lane was written
		std::uint64_t unavailable = 0;  // non-landscape draws whose pixel shader has no lane
	};

	struct PrepassClassifierStats
	{
		std::uint64_t classified = 0;
		std::uint64_t flagged = 0;
	};

	// Load/OnPostPostLoad only; false if patching failed or the flag is taken.
	// Immediate draws skip a disabled owner; baked records always classify.
	[[nodiscard]] bool RegisterPrepassDrawClassifier(Feature& a_owner, PrepassDrawFlag a_flag, PrepassDrawClassifier a_classifier);
	// The single owner of PrepassFeatureLane.y; same rules as the flag classifiers.
	[[nodiscard]] bool RegisterPrepassRecordClassifier(Feature& a_owner, PrepassRecordClassifier a_classifier);
	[[nodiscard]] PrepassLaneStats GetPrepassLaneStats() noexcept;
	[[nodiscard]] PrepassClassifierStats GetPrepassClassifierStats(PrepassDrawFlag a_flag, PrepassBakePath a_path) noexcept;

	void WritePrepassLaneTelemetry(telemetry::Sink& a_sink, PrepassDrawFlag a_flag, std::string_view a_flagged);
}
