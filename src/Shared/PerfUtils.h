#pragma once

#include <Windows.h>

// The shared vector mean converts size_t to float.
#pragma warning(push)
#pragma warning(disable: 4267)
#include "Utils/PerfUtils.h"
#pragma warning(pop)
