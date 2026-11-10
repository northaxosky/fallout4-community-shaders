#pragma once

#include "Compat/Compat.h"

#include <span>

namespace cs::compat
{
	std::span<const Rule> Rules() noexcept;
}
