#pragma once

#include "SubsurfaceScatteringConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace cs::features::subsurface_scattering
{
	// Upstream SubsurfaceScattering.cpp Gaussian, Profile and CalculateKernel.
	namespace kernel_detail
	{
		using Float3 = std::array<float, 3>;

		inline Float3 Gaussian(const Float3& a_falloff, float a_variance, float a_r)
		{
			Float3 g{};
			for (std::size_t i = 0; i < 3; ++i) {
				const float rr = a_r / (0.001f + a_falloff[i]);
				g[i] = std::exp((-(rr * rr)) / (2.0f * a_variance)) / (2.0f * 3.14f * a_variance);
			}
			return g;
		}

		inline Float3 Profile(const Float3& a_falloff, float a_r)
		{
			constexpr std::array<std::array<float, 2>, 5> terms{ { { 0.100f, 0.0484f }, { 0.118f, 0.187f }, { 0.113f, 0.567f }, { 0.358f, 1.99f }, { 0.078f, 7.41f } } };
			Float3 sum{};
			for (const auto& [weight, variance] : terms) {
				const auto g = Gaussian(a_falloff, variance, a_r);
				for (std::size_t i = 0; i < 3; ++i)
					sum[i] += weight * g[i];
			}
			return sum;
		}
	}

	inline void CalculateKernel(const std::array<float, 3>& a_strength, const std::array<float, 3>& a_falloff,
		std::span<DirectX::XMFLOAT4, kKernelSamples> a_kernel)
	{
		constexpr auto nSamples = static_cast<unsigned>(kKernelSamples);
		const float RANGE = nSamples > 20 ? 3.0f : 2.0f;
		const float EXPONENT = 2.0f;

		const float step = 2.0f * RANGE / (nSamples - 1);
		for (unsigned i = 0; i < nSamples; i++) {
			const float o = -RANGE + float(i) * step;
			const float sign = o < 0.0f ? -1.0f : 1.0f;
			a_kernel[i].w = RANGE * sign * std::abs(std::pow(o, EXPONENT)) / std::pow(RANGE, EXPONENT);
		}

		for (unsigned i = 0; i < nSamples; i++) {
			const float w0 = i > 0 ? std::abs(a_kernel[i].w - a_kernel[i - 1].w) : 0.0f;
			const float w1 = i < nSamples - 1 ? std::abs(a_kernel[i].w - a_kernel[i + 1].w) : 0.0f;
			const float area = (w0 + w1) / 2.0f;
			const auto t = kernel_detail::Profile(a_falloff, a_kernel[i].w);
			a_kernel[i].x = area * t[0];
			a_kernel[i].y = area * t[1];
			a_kernel[i].z = area * t[2];
		}

		// The zero offset comes first.
		const DirectX::XMFLOAT4 center = a_kernel[nSamples / 2];
		for (unsigned i = nSamples / 2; i > 0; i--)
			a_kernel[i] = a_kernel[i - 1];
		a_kernel[0] = center;

		float sum[3]{};
		for (unsigned i = 0; i < nSamples; i++) {
			sum[0] += a_kernel[i].x;
			sum[1] += a_kernel[i].y;
			sum[2] += a_kernel[i].z;
		}
		for (unsigned i = 0; i < nSamples; i++) {
			a_kernel[i].x /= sum[0];
			a_kernel[i].y /= sum[1];
			a_kernel[i].z /= sum[2];
		}

		a_kernel[0].x = (1.0f - a_strength[0]) * 1.0f + a_strength[0] * a_kernel[0].x;
		a_kernel[0].y = (1.0f - a_strength[1]) * 1.0f + a_strength[1] * a_kernel[0].y;
		a_kernel[0].z = (1.0f - a_strength[2]) * 1.0f + a_strength[2] * a_kernel[0].z;
		for (unsigned i = 1; i < nSamples; i++) {
			a_kernel[i].x *= a_strength[0];
			a_kernel[i].y *= a_strength[1];
			a_kernel[i].z *= a_strength[2];
		}
	}
}
