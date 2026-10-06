#include "LightDerivation.h"

#include "InverseSquareLightingMath.h"
#include "Render/LocalLights.h"

#include <algorithm>
#include <cmath>

namespace cs::features::inverse_square_lighting
{
	namespace
	{
		constexpr float kMinRadius = 0.1f;
		// 1.0 is the "use default" cutoff sentinel and must never be produced.
		const float kMaxCutoff = std::nextafter(1.0f, 0.0f);
		constexpr int kIntervals = 128;

		template <class Curve>
		double WeightedEnergy(Curve a_curve)
		{
			const auto integrand = [&](int a_index) {
				const double x = static_cast<double>(a_index) / kIntervals;
				return x * x * a_curve(x);
			};
			double sum = integrand(0) + integrand(kIntervals);
			for (int i = 1; i < kIntervals; ++i)
				sum += (i % 2 ? 4.0 : 2.0) * integrand(i);
			return sum / (3.0 * kIntervals);
		}
	}

	std::optional<DerivedLight> DeriveLight(const NativeLight& a_light)
	{
		namespace native = engine::local_lights;
		if (a_light.negatedColor || a_light.b == 0 || !(a_light.radius > kMinRadius))
			return std::nullopt;
		const double nativeEnergy = WeightedEnergy([&](double a_x) {
			return native::RadialFalloff(static_cast<float>(a_x), a_light.a, a_light.b, a_light.c);
		});
		if (!(a_light.fade * nativeEnergy > 0))
			return std::nullopt;
		const double islEnergy = WeightedEnergy([&](double a_x) {
			return GetAttenuation(static_cast<float>(a_x * a_light.radius), a_light.radius, kDefaultSize);
		});
		const double gain = nativeEnergy / (4.0 * islEnergy);
		const double intensity = 4.0 * a_light.fade * gain;
		const float cutoff = CalculateCutoff(static_cast<float>(intensity), a_light.radius, kDefaultSize);
		if (!std::isfinite(gain) || !std::isfinite(cutoff))
			return std::nullopt;
		DerivedLight derived;
		derived.intensityScale = static_cast<float>(gain);
		derived.authored.inverseSquare = true;
		derived.authored.cutoff = std::clamp(cutoff, kMinCutoff, kMaxCutoff);
		derived.clamped = *derived.authored.cutoff != cutoff;
		return derived;
	}
}
