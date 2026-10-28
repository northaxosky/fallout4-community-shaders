#pragma once

#include "Render/GameRuntime.h"
#include "Render/StockShaderTable.h"

#include <vector>

namespace cs::engine
{
	// Every stock variant of a runtime, from the tables the identity tests gate; callers own the rows.
	[[nodiscard]] std::vector<StockShaderRow> GetStockShaderVariants(GameRuntime a_runtime);
}
