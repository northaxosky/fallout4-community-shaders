#include "Render/PrepassInstrumentationModel.h"

#include <iostream>

namespace
{
	int failures = 0;

	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}
}

int main()
{
	using namespace cs::engine::prepass_instrumentation;

	Check(!IsObjectLodTechnique(0), "zero technique decoded as object LOD");
	Check(IsObjectLodTechnique(0x00000800), "object LOD bit was not decoded");
	Check(
		IsObjectLodTechnique(0xF1234A00),
		"object LOD bit was lost among unrelated bits");
	Check(
		!IsObjectLodTechnique(0xFFFFF7FF),
		"bit-clear technique decoded as object LOD");

	TechniqueState state;
	Check(!RecordDraw(state), "inactive technique accepted a draw");
	Check(
		!BeginTechnique(state, 0xA5A50800, 41),
		"first technique reported an overwrite");

	const auto first = RecordDraw(state);
	const auto second = RecordDraw(state);
	Check(
		first == DrawCorrelation{ 41, 1, 0xA5A50800, true },
		"first draw correlation was incorrect");
	Check(
		second == DrawCorrelation{ 41, 2, 0xA5A50800, true },
		"multiple draws did not retain the technique serial");
	Check(EndTechnique(state) == 2, "completed draw count was incorrect");
	Check(!RecordDraw(state), "ended technique accepted a draw");

	Check(
		!BeginTechnique(state, 0x12340000, 42),
		"ended technique reported an overwrite");
	const auto control = RecordDraw(state);
	Check(
		control == DrawCorrelation{ 42, 1, 0x12340000, false },
		"bit-clear control correlation was incorrect");
	Check(
		BeginTechnique(state, 0x00000800, 43),
		"active technique replacement was not reported");
	Check(
		RecordDraw(state) == DrawCorrelation{ 43, 1, 0x00000800, true },
		"replacement technique did not reset draw correlation");

	if (failures == 0)
		std::cout << "Prepass instrumentation tests passed\n";
	return failures == 0 ? 0 : 1;
}
