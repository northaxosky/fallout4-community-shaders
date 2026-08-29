#pragma once

#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace cs
{
	class Feature;

	// Two-column layout with searchable categorized navigation.
	class FeatureListRenderer
	{
	public:
		struct BuiltInMenu
		{
			std::string name;
			std::function<void()> func;
		};

		struct CategoryHeader
		{
			std::string name;
		};

		using MenuFuncInfo = std::variant<BuiltInMenu, std::string, CategoryHeader, Feature*>;

		static void RenderFeatureList(
			float a_footerHeight,
			std::size_t& a_selectedMenu,
			std::string& a_featureSearch,
			std::string& a_pendingFeatureSelection,
			std::map<std::string, bool>& a_categoryExpansionStates,
			const std::function<void()>& a_drawGeneralSettings,
			const std::function<void()>& a_drawAdvancedSettings,
			const std::function<void()>& a_drawPresets);

		// Feature content without menu chrome.
		static void RenderFeatureContent(Feature& a_feature);

	private:
		struct ListMenuVisitor
		{
			std::size_t listId;
			std::size_t& selectedMenuRef;
			std::map<std::string, bool>& categoryExpansionStates;

			void operator()(const BuiltInMenu& a_menu);
			void operator()(const std::string& a_label);
			void operator()(const CategoryHeader& a_header);
			void operator()(Feature* a_feature);
		};

		struct DrawMenuVisitor
		{
			explicit DrawMenuVisitor(std::string& a_pendingFeatureSelection) :
				pendingFeatureSelection(a_pendingFeatureSelection) {}

			void operator()(const BuiltInMenu& a_menu);
			void operator()(const std::string&);
			void operator()(const CategoryHeader&);
			void operator()(Feature* a_feature);

		private:
			std::string& pendingFeatureSelection;
		};

		static void RenderFeatureHeader(Feature* a_feature, bool a_isDisabled);
		static void RenderFeatureSettings(Feature* a_feature, bool a_isDisabled, bool a_isActive);
		static void RenderRestartSettings(Feature* a_feature);
		static void RenderRestoreDefaultsButton(Feature* a_feature, bool a_isDisabled, bool a_isActive);

		static std::vector<MenuFuncInfo> BuildMenuList(
			const std::string& a_featureSearch,
			std::map<std::string, bool>& a_categoryExpansionStates,
			const std::function<void()>& a_drawGeneralSettings,
			const std::function<void()>& a_drawAdvancedSettings,
			const std::function<void()>& a_drawPresets);

		static void HandlePendingFeatureSelection(
			std::string& a_pendingFeatureSelection,
			const std::vector<MenuFuncInfo>& a_menuList,
			std::size_t& a_selectedMenu);

		static void RenderLeftColumn(
			const std::vector<MenuFuncInfo>& a_menuList,
			std::size_t& a_selectedMenu,
			std::string& a_featureSearch,
			std::map<std::string, bool>& a_categoryExpansionStates);

		static void RenderRightColumn(
			const std::vector<MenuFuncInfo>& a_menuList,
			std::size_t a_selectedMenu,
			std::string& a_pendingFeatureSelection);
	};
}
