#pragma once

#include "Host/HostClient.h"

#include <DearModdingUI/UI.h>

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

namespace cs::ui
{
	// Collapsible sections show the host's item count beside the title.
	struct Collapsible
	{
		std::size_t count{};
		bool expandedByDefault{};
		// Unframed sections suit callers already inside a panel or overlay.
		bool framed{ true };
	};

	// Host-styled titled panel for grouping controls on a feature page.
	class Section
	{
	public:
		Section(const char* a_id, const char* a_title, char32_t a_glyph = 0) :
			_panel(std::in_place, a_id)
		{
			if (*_panel) {
				static_cast<void>(
					host::HostClient::Get().Client().DrawSectionHeader(a_title, a_glyph));
				_open = true;
			}
		}

		Section(const char* a_id, const char* a_title, Collapsible a_collapsible, char32_t a_glyph = 0)
		{
			if (a_collapsible.framed) {
				_panel.emplace(a_id);
				if (!*_panel)
					return;
			}
			// The host owns no expanded state, so the section keeps it per session.
			auto& expanded = ExpandedStates().try_emplace(a_id, a_collapsible.expandedByDefault).first->second;
			static_cast<void>(host::HostClient::Get().Client().DrawCollapsingSectionHeader(
				a_id, a_title, a_glyph, expanded, a_collapsible.count));
			_open = expanded;
		}

		[[nodiscard]] explicit operator bool() const noexcept { return _open; }

	private:
		static std::unordered_map<std::string, bool>& ExpandedStates()
		{
			static std::unordered_map<std::string, bool> states;
			return states;
		}

		std::optional<dmui::ui::PanelScope> _panel;
		bool _open{};
	};
}
