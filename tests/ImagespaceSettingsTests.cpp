#include "ImagespaceSettings.h"

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
	using namespace cs::features::imagespace;

	Check(ClampDofQuality(-1) == kDofQualityMin, "quality below range was not clamped");
	Check(ClampDofQuality(kDofQualityMin) == kDofQualityMin, "minimum quality changed");
	Check(ClampDofQuality(kDofQualityMax) == kDofQualityMax, "maximum quality changed");
	Check(ClampDofQuality(2) == kDofQualityMax, "quality above range was not clamped");

	if (failures != 0) {
		return 1;
	}

	std::cout << "Imagespace settings tests passed\n";
	return 0;
}
