#include "PrepassClassifier.h"

#include "MaterialTags.h"
#include "Render/PrepassLane.h"

#include "RE/B/BSLightingShaderProperty.h"

namespace cs::features::true_pbr
{
	namespace
	{
		engine::PrepassRecord ClassifyRecord(engine::PrepassDrawContext& a_draw) noexcept
		{
			if (!a_draw.InWorldPrepass())
				return {};
			const auto* property = a_draw.LightingProperty();
			return property && property->material ? engine::PrepassRecord{ .id = Tags().Find(property->material) } : engine::PrepassRecord{};
		}
	}

	bool RegisterPrepassClassifier(Feature& a_owner)
	{
		return engine::RegisterPrepassRecordClassifier(a_owner, &ClassifyRecord);
	}
}
