#pragma once

#include <cstddef>
#include <cstdint>

#include <string_view>

namespace RE
{
	class BSRenderPass;
}

namespace cs::telemetry
{
	class Sink;
}

// Shader subclasses with their own vtable, for hooks wrapping a virtual.
#define CS_SHADER_SUBCLASSES(X)               \
	X(BSBloodSplatterShader, kBloodSplatter); \
	X(BSDFCompositeShader, kBsdfComposite);   \
	X(BSDFLightShader, kBsdfLight);           \
	X(BSDFPrePassShader, kDeferredPrepass);   \
	X(BSDistantTreeShader, kDistantTree);     \
	X(BSEffectShader, kEffect);               \
	X(BSLightingShader, kBsLighting);         \
	X(BSParticleShader, kParticle);           \
	X(BSSkyShader, kBsSky);                   \
	X(BSUtilityShader, kUtility);             \
	X(BSWaterShader, kBsWater)

namespace cs::engine
{
	void InstallShaderSubclassHooks();

	// The engine path that baked a prepass draw's per-draw constants.
	enum class PrepassBakePath
	{
		kCommandBuffer,  // once, when the pass is created
		kImmediate       // every frame, from BSDFPrePassShader::SetupGeometry
	};

	// One classifier owns each PrepassFeatureLane component.
	enum class PrepassLaneComponent : std::uint8_t
	{
		kX,
		kY,
		kZ,
		kW
	};

	using PrepassDrawClassifier = bool (*)(RE::BSRenderPass* a_pass, PrepassBakePath a_path) noexcept;

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

	// Load/OnPostPostLoad only; false if patching failed or the component is taken.
	[[nodiscard]] bool RegisterPrepassDrawClassifier(PrepassLaneComponent a_component, PrepassDrawClassifier a_classifier);
	[[nodiscard]] PrepassLaneStats GetPrepassLaneStats() noexcept;
	[[nodiscard]] PrepassClassifierStats GetPrepassClassifierStats(PrepassLaneComponent a_component, PrepassBakePath a_path) noexcept;

	void WritePrepassLaneTelemetry(telemetry::Sink& a_sink, PrepassLaneComponent a_component, std::string_view a_flagged);
}
