#include "Render/StockShaderVariants.h"

#include "Log.h"
#include "Render/StockShaderTablesResource.h"

#include "Utils/ModuleResource.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.stockshadervariants");

		StockShaderTable LoadTable(int a_resourceId)
		{
			const auto text = cs::LoadRcData(a_resourceId);
			if (!text)
				throw std::runtime_error(std::format("stock shader table: {}", text.error()));
			return ParseStockShaderTable(*text);
		}

		bool SameFamily(const StockShaderRow& a_left, const StockShaderRow& a_right)
		{
			const auto key = [](const StockShaderRow& a_row) {
				return std::tie(a_row.targetName, a_row.stage, a_row.descriptor, a_row.nativeName,
					a_row.nativeClassName, a_row.nativeSourceGroup, a_row.nativeMacros);
			};
			return key(a_left) == key(a_right);
		}

		// OG rows replace their AE families; NG's shaders match AE's.
		std::vector<StockShaderRow> Build(GameRuntime a_runtime)
		{
			auto rows = LoadTable(IDR_STOCK_SHADERS_AE).rows;
			if (a_runtime != GameRuntime::kOG)
				return rows;
			auto overrides = LoadTable(IDR_STOCK_SHADERS_OG).rows;
			std::erase_if(rows, [&](const StockShaderRow& a_row) {
				return std::ranges::any_of(overrides, [&](const StockShaderRow& a_override) {
					return SameFamily(a_override, a_row);
				});
			});
			rows.insert(
				rows.end(),
				std::make_move_iterator(overrides.begin()),
				std::make_move_iterator(overrides.end()));
			return rows;
		}
	}

	std::vector<StockShaderRow> GetStockShaderVariants(GameRuntime a_runtime)
	{
		try {
			return Build(a_runtime);
		} catch (const std::exception& e) {
			L->error("Stock shader variants unavailable: {}", e.what());
			return {};
		}
	}
}
