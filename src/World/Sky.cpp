#include "World/Sky.h"

#include <algorithm>
#include <cmath>

#include "RE/B/BSGeometry.h"
#include "RE/B/BSSkyShaderProperty.h"
#include "RE/I/ImageSpaceManager.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiLight.h"
#include "RE/N/NiNode.h"
#include "RE/S/Sky.h"
#include "RE/S/Sun.h"
#include "RE/T/TES.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWorldSpace.h"

namespace cs::engine
{
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept
	{
		auto* sky = RE::Sky::GetSingleton();
		if (!sky || !sky->sun || !sky->sun->light)
			return false;

		// Cast through NiAVObject because NiDirectionalLight is incomplete.
		auto* lightObj = reinterpret_cast<RE::NiAVObject*>(sky->sun->light.get());
		auto& rot = lightObj->world.rotate;

		// World rotation row zero is the light's propagation direction.
		float x = rot.entry[0].x;
		float y = rot.entry[0].y;
		float z = rot.entry[0].z;
		const float invLen = 1.0f / std::max(std::sqrt(x * x + y * y + z * z), 1e-6f);
		outX = x * invLen;
		outY = y * invLen;
		outZ = z * invLen;
		return true;
	}

	RE::BSSkyShaderProperty* GetSkyShaderProperty(RE::BSGeometry* a_geometry)
	{
		return a_geometry ? netimmerse_cast<RE::BSSkyShaderProperty*>(a_geometry->properties[1].get()) : nullptr;
	}

	const RE::BSSkyShaderProperty* GetSkyShaderProperty(const RE::BSGeometry* a_geometry)
	{
		return GetSkyShaderProperty(const_cast<RE::BSGeometry*>(a_geometry));
	}

	bool TryGetSunDiscDirectionWS(float& outX, float& outY, float& outZ) noexcept
	{
		const auto* sky = RE::Sky::GetSingleton();
		if (!sky || !sky->root || !sky->sun || !sky->sun->root)
			return false;
		// The sky root sits at the camera; the sun root's offset is the disc.
		const auto offset = sky->sun->root->world.translate - sky->root->world.translate;
		const float length = std::sqrt(offset.SqrLength());
		if (!(length > 0.0f) || !std::isfinite(length))
			return false;
		outX = offset.x / length;
		outY = offset.y / length;
		outZ = offset.z / length;
		return true;
	}

	namespace
	{
		[[nodiscard]] bool TryGetLinearSunColor(float a_scale, DirectX::XMFLOAT3& a_color) noexcept
		{
			auto* sky = RE::Sky::GetSingleton();
			if (!sky || !sky->sun || !sky->sun->light)
				return false;

			const auto* light = reinterpret_cast<const RE::NiLight*>(sky->sun->light.get());
			const float scale = light->dimmer * a_scale;
			const auto linear = [scale](float a_value) {
				return std::pow(std::max(a_value, 0.0f), 2.2f) * scale;
			};
			a_color = { linear(light->diff.r), linear(light->diff.g), linear(light->diff.b) };
			return std::isfinite(a_color.x) && std::isfinite(a_color.y) && std::isfinite(a_color.z);
		}
	}

	bool TryGetSunLightColor(DirectX::XMFLOAT3& a_color) noexcept
	{
		// BSDFLightShader::SetupGeometry: pow(diffuse, 2.2) * dimmer * HDR sunlight scale.
		float scale = 1.0f;
		if (const auto* imageSpace = RE::ImageSpaceManager::GetSingleton())
			scale = imageSpace->currentEOFData.baseData.hdrData.sunlightScale;
		return TryGetLinearSunColor(scale, a_color);
	}

	SunGlare GetSunGlare() noexcept
	{
		// BSDFCompositeShader::SetupGeometry: the sun color without the HDR scale; exponent is a constant.
		SunGlare glare{};
		const auto* sky = RE::Sky::GetSingleton();
		if (!sky || !sky->sun || !IsFullSky() || !TryGetLinearSunColor(1.0f, glare.color))
			return {};
		// Sun::Update stores the same fade in the disc property's blend alpha.
		const auto* property = GetSkyShaderProperty(sky->sun->sunBase.get());
		if (!property)
			return {};
		glare.exponent = 8.0f;
		glare.intensity = property->kBlendColor.a;
		return glare;
	}

	bool IsSkyHidden() noexcept
	{
		const auto* sky = RE::Sky::GetSingleton();
		return sky && sky->flags.all(RE::Sky::Flags::kHideSky);
	}

	bool IsFullSky() noexcept
	{
		const auto* sky = RE::Sky::GetSingleton();
		return sky && sky->mode.get() == RE::Sky::Mode::kFull;
	}

	namespace
	{
		[[nodiscard]] bool ExteriorWorldspaceHasSky(const RE::TES& a_tes) noexcept
		{
			return !a_tes.interiorCell && a_tes.worldSpace && !a_tes.worldSpace->flags.any(RE::TESWorldSpace::FLAG::kNoSky);
		}
	}

	bool IsInterior() noexcept
	{
		const auto* tes = RE::TES::GetSingleton();
		// FO4: kFixedDimensions marks open-sky worlds; Sky::mode replaces it.
		return !(tes && ExteriorWorldspaceHasSky(*tes) && IsFullSky());
	}

	bool InteriorRendersSunShadows(const RE::TESObjectCELL& a_cell) noexcept
	{
		return a_cell.cellFlags.all(RE::TESObjectCELL::Flag::kInteriorSunShadowsA, RE::TESObjectCELL::Flag::kInteriorSunShadowsB);
	}

	bool HasSunShadows() noexcept
	{
		const auto* tes = RE::TES::GetSingleton();
		if (!tes)
			return false;
		if (const auto* cell = tes->interiorCell)
			return InteriorRendersSunShadows(*cell);
		const auto* sky = RE::Sky::GetSingleton();
		return ExteriorWorldspaceHasSky(*tes) && sky && sky->mode.get() != RE::Sky::Mode::kNone;
	}

	bool IsMainOrLoadingMenuOpen() noexcept
	{
		const auto* ui = RE::UI::GetSingleton();
		return ui && (ui->GetMenuOpen<RE::MainMenu>() || ui->GetMenuOpen<RE::LoadingMenu>());
	}
}
