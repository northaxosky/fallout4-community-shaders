#include "PrepassClassifier.h"

#include "MaterialTags.h"
#include "Render/PrepassLane.h"

#include "RE/B/BSLightingShaderProperty.h"

namespace cs::features::true_pbr
{
	namespace
	{
		std::uint16_t ClassifyRecord(engine::PrepassDrawContext& a_draw) noexcept
		{
			if (!a_draw.InWorldPrepass())
				return RecordId{ 0 };
			const auto* property = a_draw.LightingProperty();
			return property && property->material ? Tags().Find(property->material) : RecordId{ 0 };
		}
	}

	bool RegisterPrepassClassifier(Feature& a_owner)
	{
		return engine::RegisterPrepassRecordClassifier(a_owner, &ClassifyRecord);
	}
}
