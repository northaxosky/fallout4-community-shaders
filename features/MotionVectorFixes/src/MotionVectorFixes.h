#pragma once

#include "Feature.h"

namespace cs::features
{
	class MotionVectorFixes : public Feature
	{
	public:
		static MotionVectorFixes* Get();

		std::string_view GetName() const override { return "MotionVectorFixes"; }

		void Load() override;
		void OnDataLoaded() override;
	};
}
