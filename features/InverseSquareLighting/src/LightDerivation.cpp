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
		constexpr int kIntervals = 128;
		constexpr float kMinSize = 0.01f;
		// 50 is upstream's "use default size" sentinel and must never be produced.
		const float kMaxSize = std::nextafter(50.0f, 0.0f);
		constexpr int kBisections = 32;

		// Smallest x in (0, 1] where the native curve falls to half its center value.
		template <class Curve>
		double HalfValuePoint(Curve a_curve)
		{
			const double half = 0.5 * a_curve(0.0);
			if (a_curve(1.0) >= half)
				return 1.0;
			double lo = 0.0, hi = 1.0;
			for (int i = 0; i < kBisections; ++i) {
				const double mid = 0.5 * (lo + hi);
				(a_curve(mid) > half ? lo : hi) = mid;
			}
			return hi;
		}

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
		const auto nativeCurve = [&](double a_x) {
			return static_cast<double>(native::RadialFalloff(static_cast<float>(a_x), a_light.a, a_light.b, a_light.c));
		};
		const double nativeEnergy = WeightedEnergy(nativeCurve);
		if (!(a_light.fade * nativeEnergy > 0))
			return std::nullopt;
		// ISL halves at d = sqrt(K * size^2 / 2); match the native half-value distance.
		const float size = std::clamp(
			static_cast<float>(HalfValuePoint(nativeCurve) * a_light.radius * std::sqrt(2.0 / kScaledUnitsSq)),
			kMinSize, kMaxSize);
		const double islEnergy = WeightedEnergy([&](double a_x) {
			return GetAttenuation(static_cast<float>(a_x * a_light.radius), a_light.radius, size);
		});
		const double gain = nativeEnergy / (4.0 * islEnergy);
		if (!std::isfinite(gain))
			return std::nullopt;
		DerivedLight derived;
		derived.intensityScale = static_cast<float>(gain);
		derived.authored.inverseSquare = true;
		derived.authored.size = size;
		return derived;
	}
}
