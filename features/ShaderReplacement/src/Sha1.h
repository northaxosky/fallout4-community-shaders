#pragma once

#include "Utils/CSSha1.h"

namespace cs::features::replacement
{
	using Sha1Result = cs::sha1::Sha1Result;
	using cs::sha1::Sha1Compute;
	using cs::sha1::Sha1FromHex;
	using cs::sha1::Sha1InitOnce;
	using cs::sha1::Sha1IsZero;
	using cs::sha1::Sha1ToHex;
}
