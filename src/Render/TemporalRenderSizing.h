#pragma once

#include "Render/TemporalProvider.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace cs::render::temporal
{
	struct RenderExtent
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		[[nodiscard]] bool operator==(const RenderExtent&) const noexcept = default;
	};

	[[nodiscard]] inline float RatioForExactExtent(
		std::uint32_t a_renderExtent,
		std::uint32_t a_outputExtent) noexcept
	{
		if (!a_renderExtent || !a_outputExtent ||
			a_renderExtent > a_outputExtent) {
			return 1.0f;
		}

		float ratio = static_cast<float>(
			static_cast<double>(a_renderExtent) /
			static_cast<double>(a_outputExtent));
		while (static_cast<std::uint32_t>(
				   static_cast<float>(a_outputExtent) * ratio) <
			   a_renderExtent) {
			ratio = std::nextafter(
				ratio, std::numeric_limits<float>::infinity());
		}
		while (static_cast<std::uint32_t>(
				   static_cast<float>(a_outputExtent) * ratio) >
			   a_renderExtent) {
			ratio = std::nextafter(ratio, 0.0f);
		}
		return ratio;
	}

	class RenderSizeState
	{
	public:
		void SetNative(
			std::uint32_t a_outputWidth,
			std::uint32_t a_outputHeight) noexcept
		{
			_output = { a_outputWidth, a_outputHeight };
			_requested = _output;
			_committed = _output;
		}

		[[nodiscard]] bool SetRequested(
			const SuperResolutionSizeRequest& a_request,
			const SuperResolutionSizeResult& a_result) noexcept
		{
			_output = { a_request.outputWidth, a_request.outputHeight };
			if (!a_result.Succeeded() ||
				a_result.renderWidth > a_request.outputWidth ||
				a_result.renderHeight > a_request.outputHeight) {
				_requested = _output;
				_committed = _output;
				return false;
			}
			_requested = {
				a_result.renderWidth,
				a_result.renderHeight
			};
			return true;
		}

		void CommitRequested() noexcept
		{
			_committed = _requested;
		}

		[[nodiscard]] RenderExtent Requested() const noexcept
		{
			return _requested;
		}

		[[nodiscard]] RenderExtent Committed() const noexcept
		{
			return _committed;
		}

		[[nodiscard]] float WidthRatio() const noexcept
		{
			return RatioForExactExtent(_committed.width, _output.width);
		}

		[[nodiscard]] float HeightRatio() const noexcept
		{
			return RatioForExactExtent(_committed.height, _output.height);
		}

	private:
		RenderExtent _output;
		RenderExtent _requested;
		RenderExtent _committed;
	};
}
