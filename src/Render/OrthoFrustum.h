#pragma once

#include <array>

namespace cs::engine
{
	// Orthographic view volume in the axis convention of NiFrustumPlanes::Set.
	struct OrthoFrustum
	{
		using Vec = std::array<float, 3>;

		Vec depthAxis{};
		Vec upAxis{};
		Vec rightAxis{};
		Vec origin{};
		float left = 0.0f;
		float right = 0.0f;
		float top = 0.0f;
		float bottom = 0.0f;
		float nearPlane = 0.0f;
		float farPlane = 0.0f;
	};

	// True only when the sphere lies wholly outside one of the six planes.
	[[nodiscard]] constexpr bool SphereOutside(
		const OrthoFrustum& a_frustum,
		const OrthoFrustum::Vec& a_center,
		float a_radius) noexcept
	{
		const auto project = [&](const OrthoFrustum::Vec& a_axis) {
			return a_axis[0] * (a_center[0] - a_frustum.origin[0]) +
			       a_axis[1] * (a_center[1] - a_frustum.origin[1]) +
			       a_axis[2] * (a_center[2] - a_frustum.origin[2]);
		};
		const float depth = project(a_frustum.depthAxis);
		const float right = project(a_frustum.rightAxis);
		const float up = project(a_frustum.upAxis);
		return depth + a_radius < a_frustum.nearPlane || depth - a_radius > a_frustum.farPlane ||
		       right + a_radius < a_frustum.left || right - a_radius > a_frustum.right ||
		       up + a_radius < a_frustum.bottom || up - a_radius > a_frustum.top;
	}
}
