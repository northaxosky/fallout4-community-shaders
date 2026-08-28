#include "Menu/FontSelector.h"

#include "Menu/Fonts.h"
#include "Utils/UI.h"

#include <string>
#include <vector>

#include <imgui.h>

namespace cs::FontSelector
{
	EditResult DrawFontRoleSelector(Menu::FontRole a_role)
	{
		auto& menu = Menu::Get();
		auto& roleSettings = menu.GetFontRoleSettings(a_role);

		const auto catalog = fonts::DiscoverFontCatalog();
		if (catalog.families.empty()) {
			ImGui::TextDisabled("No fonts found in the Fonts directory.");
			return {};
		}

		EditResult result;
		const auto roleKey = std::string(Menu::GetFontRoleKey(a_role));

		ImGui::PushID(roleKey.c_str());

		const int familyIndex = fonts::FindFamilyIndex(catalog, roleSettings.Family);
		const char* familyPreview = familyIndex >= 0 ?
		                                catalog.families[static_cast<std::size_t>(familyIndex)].displayName.c_str() :
		                                roleSettings.Family.c_str();

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
		if (ImGui::BeginCombo("Family", familyPreview)) {
			const auto search = ui::DrawComboSearchInput("font_family");
			for (const auto& family : catalog.families) {
				if (!ui::StringMatchesSearch(family.displayName, search))
					continue;

				const bool selected = ui::IEquals(family.name, roleSettings.Family);
				if (ImGui::Selectable(family.displayName.c_str(), selected)) {
					roleSettings.Family = family.name;
					if (const auto* style = fonts::FindRegularStyle(family)) {
						roleSettings.Style = style->style;
						roleSettings.File = style->file;
					}
					result.changed = true;
					result.commit = true;
					ui::ClearComboSearch("font_family");
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();

		const auto* family = catalog.FindFamily(roleSettings.Family);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
		if (ImGui::BeginCombo("Style", roleSettings.Style.c_str())) {
			if (family) {
				for (const auto& style : family->styles) {
					const bool selected = ui::IEquals(style.style, roleSettings.Style);
					if (ImGui::Selectable(style.displayName.c_str(), selected)) {
						roleSettings.Style = style.style;
						roleSettings.File = style.file;
						result.changed = true;
						result.commit = true;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
		if (ImGui::SliderFloat("Size scale", &roleSettings.SizeScale, 0.5f, 2.0f, "%.2f"))
			result.changed = true;
		result.commit |= ImGui::IsItemDeactivatedAfterEdit();
		result.active |= ImGui::IsItemActive();

		if (ImFont* preview = MenuFonts::GetPreviewFont(roleSettings.File)) {
			MenuFonts::ImFontGuard guard(preview);
			ImGui::TextUnformatted("The quick brown fox jumps over the lazy dog");
		}

		ImGui::PopID();
		return result;
	}
}
