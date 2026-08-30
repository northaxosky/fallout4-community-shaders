#pragma once

#include <cmath>
#include <numbers>
#include <optional>

#include "Render/FrameBuffer.h"

namespace cs::features
{
	enum class SuperResolutionFovSource
	{
		kUnavailable,
		kPublished,
		kCached
	};

	[[nodiscard]] inline bool TryGetPublishedVerticalFov(
		const cs::engine::FrameBufferSnapshot& a_frameBuffer,
		float& a_verticalFov) noexcept
	{
		if (!a_frameBuffer.valid ||
			!cs::engine::HasFiniteWorldToClip(a_frameBuffer.data)) {
			return false;
		}
		const float verticalFov = cs::engine::VerticalFieldOfViewFromWorldToClip(
			a_frameBuffer.data.CurrFrameWorldToClip);
		if (!std::isfinite(verticalFov) || verticalFov <= 0.0f ||
			verticalFov >= std::numbers::pi_v<float>) {
			return false;
		}
		a_verticalFov = verticalFov;
		return true;
	}

	class SuperResolutionFovCache
	{
	public:
		[[nodiscard]] SuperResolutionFovSource Resolve(
			const cs::engine::FrameBufferSnapshot& a_frameBuffer,
			float& a_verticalFov) noexcept
		{
			float publishedFov = 0.0f;
			if (TryGetPublishedVerticalFov(a_frameBuffer, publishedFov)) {
				_lastValidFov = publishedFov;
				a_verticalFov = publishedFov;
				return SuperResolutionFovSource::kPublished;
			}
			if (!_lastValidFov.has_value()) {
				return SuperResolutionFovSource::kUnavailable;
			}
			a_verticalFov = _lastValidFov.value();
			return SuperResolutionFovSource::kCached;
		}

	private:
		std::optional<float> _lastValidFov;
	};
}
