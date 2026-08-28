#pragma once

#include <cstdint>

namespace cs
{
	// Landing page: welcome blurb, quick links and FAQ.
	class HomePageRenderer
	{
	public:
		static void RenderHomePage();

		static bool ShouldShowFirstTimeSetup();
		static void RenderFirstTimeSetupDialog();
		static void MarkFirstTimeSetupComplete();

	private:
		static void RenderWelcomeSection();
		static void RenderQuickLinksSection();
		static void RenderFAQSection();
	};
}
