#include "OracleProjectionEmbed.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace
{
	constexpr std::array<double, 16> kWorldToCam{
		-0.560657442, -1.05163693, 0.000136087925, 42857.2617,
		0.12817952, -0.0680625811, 2.11369681, -507.560883,
		0.880389154, -0.469369173, -0.0685028806, 87977.7266,
		0.880351782, -0.469349265, -0.0684999749, 87988.9922
	};
	constexpr std::array<double, 9> kRotationRows{
		0.880351782, -0.469349265, -0.0684999749,
		0.0604998954, -0.032125093, 0.9976511,
		-0.470447421, -0.882428169, 0.000114191324
	};
	constexpr std::array<double, 3> kCameraWorld{
		-60528.7617, 73023.3125, 6262.13867
	};
	constexpr std::array<double, 16> kExpected{
		1.1917536010581513, 5.264359745570718e-10, -1.4511346350614334e-08, -1.7735462936736878e-17,
		-2.6292827523608147e-09, 2.1186733621288516, 1.6734624916531013e-09, -9.754293922593949e-19,
		-3.834337514664126e-08, 3.921271801776969e-09, 1.000042443342189, 1.0,
		0.002427676612569485, -0.0011025656332321887, -15.005041187818279, -0.005416304004029371
	};

	void EmbedWithoutAxisSwap(double a_output[16])
	{
		const std::array<double, 9> preSwappedRotation{
			kRotationRows[6], kRotationRows[7], kRotationRows[8],
			kRotationRows[3], kRotationRows[4], kRotationRows[5],
			kRotationRows[0], kRotationRows[1], kRotationRows[2]
		};
		cs::ssgi::dev::EmbedProjectionFromWorldToCam(
			kWorldToCam.data(),
			preSwappedRotation.data(),
			kCameraWorld.data(),
			a_output);
	}
}

int main()
{
	std::array<double, 16> actual{};
	cs::ssgi::dev::EmbedProjectionFromWorldToCam(
		kWorldToCam.data(),
		kRotationRows.data(),
		kCameraWorld.data(),
		actual.data());

	double maxElementDiff = 0.0;
	int failures = 0;
	for (std::size_t index = 0; index < actual.size(); ++index) {
		const double difference = std::fabs(actual[index] - kExpected[index]);
		if (difference > maxElementDiff) {
			maxElementDiff = difference;
		}
		if (!std::isfinite(actual[index]) || difference > 1.0e-6) {
			std::printf(
				"FAIL: element %zu actual=%.17g expected=%.17g diff=%.17g\n",
				index,
				actual[index],
				kExpected[index],
				difference);
			++failures;
		}
	}

	std::array<double, 16> withoutAxisSwap{};
	EmbedWithoutAxisSwap(withoutAxisSwap.data());
	const double negativeP00Diff = std::fabs(withoutAxisSwap[0] - kExpected[0]);
	if (!std::isfinite(withoutAxisSwap[0]) || negativeP00Diff < 1.0e-3) {
		std::printf(
			"FAIL: identity-swap P00 actual=%.17g expected=%.17g diff=%.17g\n",
			withoutAxisSwap[0],
			kExpected[0],
			negativeP00Diff);
		++failures;
	}

	std::printf(
		"FrustumEmbed max element diff: %.17g; identity-swap P00 diff: %.17g\n",
		maxElementDiff,
		negativeP00Diff);
	return failures == 0 ? 0 : 1;
}
