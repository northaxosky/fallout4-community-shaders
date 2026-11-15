#pragma once

namespace RE
{
	class BSRenderPass;
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

	// The last BSSkyShader::SetupGeometry pass on this thread.
	[[nodiscard]] const RE::BSRenderPass* GetCurrentSkyPass() noexcept;
}
