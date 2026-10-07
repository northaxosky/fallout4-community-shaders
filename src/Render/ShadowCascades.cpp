#include "Render/ShadowCascades.h"

#include "Render/Engine.h"

#include <cmath>
#include <cstring>

namespace cs::engine
{
	namespace
	{
		[[nodiscard]] bool IsFinite(const DirectX::XMFLOAT4X4& a_matrix) noexcept
		{
			for (const auto& row : a_matrix.m) {
				for (const float value : row) {
					if (!std::isfinite(value))
						return false;
				}
			}
			return true;
		}
	}

	SunCascadeStatus TryGetSunCascades(SunCascadeSnapshot& a_snapshot) noexcept
	{
		a_snapshot = {};
		const auto* scene = GetWorldShadowSceneNode();
		const auto* light = scene ? scene->directionalShadowLight : nullptr;
		if (!light || !light->shadowMapData)
			return SunCascadeStatus::kNoLight;
		const auto count = light->shadowMapCount;
		if (count == 0 || count > kMaxSunCascades)
			return SunCascadeStatus::kUnsupportedCount;

		// Stock binds the descriptor-selected target at both PS shadow slots, so resolve the same field.
		const auto target = light->shadowMapData[0].depthStencilTarget;
		if (target >= static_cast<std::uint32_t>(DepthStencilTarget::kCount))
			return SunCascadeStatus::kNoTarget;
		for (std::uint32_t i = 1; i < count; ++i) {
			if (light->shadowMapData[i].depthStencilTarget != target)
				return SunCascadeStatus::kInvalid;
		}
		auto* texture = GetDepthStencilTexture(static_cast<DepthStencilTarget>(target));
		if (!texture)
			return SunCascadeStatus::kNoTarget;

		float previousEnd = 0.0f;
		for (std::uint32_t i = 0; i < count; ++i) {
			const auto& entry = light->shadowMapData[i];
			static_assert(sizeof(entry.worldToShadow) == sizeof(DirectX::XMFLOAT4X4));
			std::memcpy(&a_snapshot.worldToShadow[i], entry.worldToShadow, sizeof(entry.worldToShadow));
			a_snapshot.splitEnd[i] = light->splitDistances[i];
			a_snapshot.worldUnitsPerTexel[i] = entry.worldUnitsPerTexel;
			std::memcpy(a_snapshot.viewport[i].data(), entry.viewport, sizeof(entry.viewport));
			if (!IsFinite(a_snapshot.worldToShadow[i]) || !std::isfinite(a_snapshot.splitEnd[i]) || a_snapshot.splitEnd[i] <= previousEnd)
				return SunCascadeStatus::kInvalid;
			previousEnd = a_snapshot.splitEnd[i];
		}
		a_snapshot.count = count;
		a_snapshot.texture = texture;
		return SunCascadeStatus::kOk;
	}
}
