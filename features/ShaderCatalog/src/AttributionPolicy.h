#pragma once

#include "Provenance.h"

namespace cs::features::catalog
{
	inline AttributionObjectKind CreationAttributionObjectKind(
		bool a_finalIsStock,
		bool a_finalIsReplacement) noexcept
	{
		if (a_finalIsReplacement)
			return AttributionObjectKind::kOriginatingStock;
		if (a_finalIsStock)
			return AttributionObjectKind::kStock;
		return AttributionObjectKind::kSubmissionNoObject;
	}
}
