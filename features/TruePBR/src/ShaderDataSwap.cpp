#include "ShaderDataSwap.h"

#include "RE/B/BSShaderData.h"

namespace cs::features::true_pbr
{
	void ShaderDataSwap(RE::BSShaderData& a_data, const MaterialRecord& a_record)
	{
		const auto& m = a_record.material;

		a_data.clampMode = static_cast<RE::BSGraphics::TextureAddressMode>(m.tileFlags);
		a_data.uvOffset = { m.uvOffset[0], m.uvOffset[1] };
		a_data.uvScale = { m.uvScale[0], m.uvScale[1] };

		a_data.alpha = m.alpha;
		a_data.alphaBlend = m.alphaBlend != 0;
		a_data.alphaSrcBlendMode = static_cast<RE::NiAlphaProperty::AlphaFunction>(m.blendSource);
		a_data.alphaDestBlendMode = static_cast<RE::NiAlphaProperty::AlphaFunction>(m.blendDestination);
		a_data.alphaTestRef = static_cast<std::int8_t>(m.alphaTestRef);
		a_data.alphaTest = m.alphaTest;

		a_data.refraction = m.refraction;
		a_data.refractionFalloff = m.refractionFalloff;
		a_data.refractionPower = m.refractionPower;
		a_data.modelSpaceNormals = m.modelSpaceNormals;
		a_data.externalEmittance = m.externalEmittance;

		a_data.emitEnabled = m.emitEnabled;
		a_data.emittanceMult = m.emittanceMult;
		// The engine never tests emitEnabled; Part 1 encodes off as a black color.
		a_data.emittanceColor = m.emitEnabled ? RE::NiColor{ m.emittanceColor[0], m.emittanceColor[1], m.emittanceColor[2] } : RE::NiColor{ 0.0f, 0.0f, 0.0f };

		// Clearing every slot first empties greyscale (3), envmap (4) and the unused 9.
		auto& names = a_data.textureSet->textureNames;
		for (auto& name : names)
			name = "";
		for (std::size_t slot = 0; slot < kTextureSlots.size(); ++slot)
			names[kTextureSlots[slot].engineIndex] = m.textures[slot].c_str();

		// Glow is bound only for the glowmap class, so it follows the emissive texture.
		a_data.glowmap = !m.Texture(PbrTexture::kEmissive).empty();

		a_data.specularEnabled = false;
		a_data.environmentMapping = false;
		a_data.rimLighting = false;
		a_data.backLighting = false;
		a_data.subSurfaceLighting = false;
		a_data.grayscaleToPaletteColor = false;

		a_data.materialPath = a_record.name.c_str();
	}
}
