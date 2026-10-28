#pragma once

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
}

#define CHECK(a_expression) ::cs::tests::Check(static_cast<bool>(a_expression), #a_expression, __LINE__)
