#include "InverseSquareLightingMath.h"
#include "LightAuthoring.h"

#include <cmath>
#include <iostream>

int main()
{
	using namespace cs::features::inverse_square_lighting;
	std::vector<LightDefinition> definitions;
	std::string error;
	if (!ParseLightDefinitions(toml::parse(R"(
[[lights]]
plugin = "Test.esm"
form_id = 0x800
inverse_square = true
linear = true
cutoff = 0.1
size = 2.0
[[references]]
plugin = "Test.esm"
form_id = 0x801
inverse_square = false
)"),
			definitions, error) ||
		definitions.size() != 2) {
		std::cerr << "Authored light identities did not parse: " << error << '\n';
		return 1;
	}
	auto inherited = definitions[0].data;
	inherited.Apply(definitions[1].data);
	if (inherited.inverseSquare != false || inherited.linear != true ||
		inherited.cutoff != 0.1f || inherited.size != 2.0f) {
		std::cerr << "Reference opt-out lost its explicit false or inherited fields\n";
		return 1;
	}
	for (const char* invalid : {
			 R"([[lights]]
plugin = "Test.esm"
form_id = 0x1000800
)",
			 R"([[lights]]
plugin = "Test.esm"
form_id = 0x800
inverse_square = "true"
)",
			 R"([[lights]]
plugin = "Test.esm"
form_id = 0x800
size = nan
)" }) {
		if (ParseLightDefinitions(toml::parse(invalid), definitions, error) || definitions.size() != 2) {
			std::cerr << "Invalid authoring was accepted or partially replaced valid definitions\n";
			return 1;
		}
	}
	for (const bool shadow : { false, true }) {
		for (const float cutoffOverride : { 1.0f, 0.1f }) {
			constexpr float intensity = 4.0f, size = 2.0f;
			const float radius = CalculateRadius(intensity, shadow, cutoffOverride, size);
			const float cutoff = cutoffOverride == 1.0f ?
			                         (shadow ? 0.022f : 0.05f) :
			                         cutoffOverride;
			const float atRadius = intensity * 3920.0f /
			                       (radius * radius + 3920.0f * size * size * 0.5f);
			if (std::abs(atRadius - cutoff) > 1e-6f) {
				std::cerr << "Radius does not meet the authored intensity cutoff\n";
				return 1;
			}
		}
	}
	if (CalculateRadius(0.01f, false, 1.0f, 50.0f) != 1.0f ||
		CalculateRadius(1.0f, false, 0.5f, 2.0f) != 0.0f) {
		std::cerr << "Radius differs from the upstream NaN fallback or balanced zero-radius edge\n";
		return 1;
	}
	const float radius = CalculateRadius(4.0f, false, 1.0f, 2.0f);
	if (std::abs(GetAttenuation(0, radius, 2) - 0.5f) > 1e-6f ||
		GetAttenuation(radius, radius, 2) != 0 ||
		GetAttenuation(radius + 1, radius, 2) != 0 ||
		GetAttenuation(radius - 1, radius, 2) <= 0) {
		std::cerr << "Gameplay attenuation disagrees with source-size or fade boundary\n";
		return 1;
	}
	return 0;
}
