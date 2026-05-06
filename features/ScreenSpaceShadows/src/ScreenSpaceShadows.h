#pragma once

#include "Feature.h"

namespace cs::features
{
	class ScreenSpaceShadows : public Feature
	{
	public:
		static ScreenSpaceShadows* GetSingleton();

		std::string_view GetName() const override { return "ScreenSpaceShadows"; }

		void Load() override;
		void DrawSettings() override;

	private:
		ScreenSpaceShadows() = default;
	};
}
