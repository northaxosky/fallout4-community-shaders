#pragma once

#include "Feature.h"

namespace cs::features
{
	class MotionVectorFixes : public Feature
	{
	public:
		static MotionVectorFixes* GetSingleton();

		std::string_view GetName() const override { return "MotionVectorFixes"; }

		void Load() override;
		void OnDataLoaded() override;
		void DrawSettings() override;
	};
}
