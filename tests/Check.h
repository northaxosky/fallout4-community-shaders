#pragma once

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace cs::tests
{
	inline int failures{};

	inline void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": " << a_expression << '\n';
			++failures;
		}
	}

	inline void Fail(std::string_view a_message, int a_line)
	{
		std::cerr << "FAIL at line " << a_line << ": " << a_message << '\n';
		++failures;
	}

	inline void CheckHr(long a_result, std::string_view a_expression, int a_line)
	{
		if (a_result < 0) {
			std::cerr << "CHECK_HR failed at line " << a_line << ": " << a_expression
					  << " hr=0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(a_result)
					  << std::dec << '\n';
			++failures;
		}
	}
}

#define CHECK(a_expression) ::cs::tests::Check(static_cast<bool>(a_expression), #a_expression, __LINE__)
#define CHECK_HR(a_expression) ::cs::tests::CheckHr(static_cast<long>(a_expression), #a_expression, __LINE__)
#define FAIL(a_message) ::cs::tests::Fail(a_message, __LINE__)
#define CHECK_MSG(a_condition, a_message) (static_cast<bool>(a_condition) ? void() : FAIL(a_message))
