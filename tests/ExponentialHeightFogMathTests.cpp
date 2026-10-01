#include "ExponentialHeightFogMath.h"
#include "ExponentialHeightFogSettings.h"
#include "World/WeatherVariableRegistry.h"

#include <iostream>

namespace
{
	int failures = 0;
	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << a_message << '\n';
			++failures;
		}
	}

	void TestVolumetricCoordinates()
	{
		using namespace cs::features::exponential_height_fog;
		Check(Halton(1, 2) == 0.5f && Halton(2, 2) == 0.25f &&
				  Halton(1, 3) == 1.0f / 3.0f && Halton(1, 5) == 0.2f,
			"Halton bases diverge from the upstream volume sequence");
		const auto parameters = GridZParameters(5.0f, 0.0f, 60000.0f, 8.0f, 64);
		const auto depth = [&](float a_slice) { return (std::exp2(a_slice / parameters[2]) - parameters[1]) / parameters[0]; };
		Check(std::abs(depth(0) - 14.5f) < 0.001f && std::abs(depth(64) - 60000.0f) < 0.1f,
			"Froxel near-offset/far boundary no longer match the upstream distribution");
		const auto stretched = GridZParameters(5.0f, 1200.0f, 200000.0f, 1.0f, 160);
		Check(std::isfinite(stretched[0]) && std::isfinite(stretched[1]) &&
				  stretched[2] == 160.0f / 120.0f,
			"High slice counts must bound the depth exponent");
	}

	void TestWeatherTransitions()
	{
		using namespace cs::features::exponential_height_fog;
		cs::weather::VariableRegistry<std::remove_cv_t<decltype(kSchema)>> registry;
		auto config = toml::parse(R"(
["0x123~Example.esp"]
__enabled = true
fogDensity = 0.2
fogInscatteringColor = [0.4, 0.5, 0.6, 1.0]
volumetricFogEnabled = 1
["0x456~Example.esp"]
__enabled = true
fogDensity = 0.6
fogInscatteringColor = [0.8, 0.9, 1.0, 1.0]
volumetricFogEnabled = 0
)");
		std::string error;
		Check(registry.Configure(kSchema, kWeatherVariables, &config, error), "Valid fog weather profile rejected");
		Settings base;
		const auto from = decltype(registry)::Key(0x123, "Example.esp");
		const auto to = decltype(registry)::Key(0x456, "Example.esp");
		const auto blend = registry.Evaluate(kSchema, base, from, to, 0.5f);
		Check(std::abs(blend.fogDensity - 0.4f) < 0.0001f && blend.volumetricFogEnabled == 1 &&
				  std::abs(blend.fogInscatteringColor[0] - 0.6f) < 0.0001f,
			"Weather values must interpolate; integer toggles switch strictly above 0.5");
		Check(registry.Evaluate(kSchema, base, from, to, 0.51f).volumetricFogEnabled == 0, "Weather toggle did not switch");
		const auto restored = registry.Evaluate(kSchema, base, to, "missing", 1.0f);
		Check(restored.fogDensity == base.fogDensity && restored.fogInscatteringColor == base.fogInscatteringColor,
			"Transition into an unconfigured weather must restore user settings");
		auto invalid = toml::parse(R"(["0x123~Example.esp"]
__enabled = true
volumetricGridSizeZ = 32)");
		Check(!registry.Configure(kSchema, kWeatherVariables, &invalid, error),
			"Grid sizing is not an upstream weather variable");
		invalid = toml::parse(R"(["0x123~Example.esp"]
__enabled = true
fogInscatteringColor = [0.1, nan, 0.3, 1.0])");
		Check(!registry.Configure(kSchema, kWeatherVariables, &invalid, error),
			"Nonfinite weather color must not reach the shader buffers");
	}
}

int main()
{
	TestVolumetricCoordinates();
	TestWeatherTransitions();
	return failures ? 1 : 0;
}
