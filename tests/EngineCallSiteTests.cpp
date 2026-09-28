#include "Render/CallSiteDecode.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace
{
	using namespace cs::engine;
	int failures = 0;

	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	constexpr std::uintptr_t kSite = 0x140D36D6E;

	constexpr std::array<std::uint8_t, 5> Call(std::uintptr_t a_site, std::uintptr_t a_target)
	{
		const auto displacement = static_cast<std::uint32_t>(
			static_cast<std::int32_t>(static_cast<std::intptr_t>(a_target) -
									  static_cast<std::intptr_t>(a_site + 5)));
		return { 0xE8,
			static_cast<std::uint8_t>(displacement),
			static_cast<std::uint8_t>(displacement >> 8),
			static_cast<std::uint8_t>(displacement >> 16),
			static_cast<std::uint8_t>(displacement >> 24) };
	}

	void TestClassification()
	{
		constexpr std::uintptr_t expected = 0x140D38B40;

		const auto forward = ClassifyCallSite(Call(kSite, expected), kSite, expected);
		Check(forward.kind == CallSiteKind::kExpected && forward.target == expected,
			"a forward call to the proven target must be accepted");

		constexpr std::uintptr_t backward = 0x140001000;
		Check(ClassifyCallSite(Call(kSite, backward), kSite, backward).kind == CallSiteKind::kExpected,
			"a negative rel32 displacement must decode to the proven target");

		Check(ClassifyCallSite(Call(kSite, 0x141000000), kSite, expected).kind == CallSiteKind::kWrongTarget,
			"a call to different code proves the offset is wrong and must be rejected");

		constexpr std::array<std::uint8_t, 5> jump{ 0xE9, 0, 0, 0, 0 };
		Check(ClassifyCallSite(jump, kSite, expected).kind == CallSiteKind::kNotACall,
			"a site without a rel32 call must be rejected");
	}
}

int main()
{
	TestClassification();
	if (failures == 0)
		std::cout << "EngineCallSite: all checks passed\n";
	return failures == 0 ? 0 : 1;
}
