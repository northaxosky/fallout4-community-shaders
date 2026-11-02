#pragma once

#include "Host/HostClient.h"

#include <DearModdingUI/Client.h>

#include <span>
#include <string>
#include <string_view>

namespace cs::ui
{
	struct EditResult
	{
		bool changed = false;
		// True once the edit is finished: a toggle, or a released slider or input.
		bool completed = false;
	};

	// Marks a page whose settings draw as host fields, not raw widgets.
	class FieldPage
	{
	public:
		// a_restart lists the descriptions of settings that need a restart.
		explicit FieldPage(std::span<const std::string_view> a_restart) noexcept :
			_restart(a_restart), _previous(Slot())
		{
			Slot() = this;
		}

		~FieldPage() { Slot() = _previous; }

		FieldPage(const FieldPage&) = delete;
		FieldPage& operator=(const FieldPage&) = delete;

		[[nodiscard]] static const FieldPage* Current() noexcept { return Slot(); }

		[[nodiscard]] bool NeedsRestart(std::string_view a_label) const noexcept
		{
			for (const auto restart : _restart)
				if (restart == a_label)
					return true;
			return false;
		}

	private:
		static const FieldPage*& Slot() noexcept
		{
			static thread_local const FieldPage* current = nullptr;
			return current;
		}

		std::span<const std::string_view> _restart;
		const FieldPage* _previous;
	};

	struct FieldText
	{
		std::string_view id;
		std::string_view label;
		// Long-form explanation; a host description in a field, else a tooltip.
		const char* help = nullptr;
	};

	// a_draw(widgetLabel) draws the control; a_reset restores its default.
	// Outside a FieldPage, or if the host refuses the field, the control draws raw.
	template <class Draw, class Reset>
	EditResult DrawField(const FieldText& a_text, bool a_modified, Draw&& a_draw, Reset&& a_reset)
	{
		const auto drawRaw = [&] {
			const std::string label = std::string(a_text.label) + "##" + std::string(a_text.id);
			const EditResult result = a_draw(label.c_str());
			if (a_text.help && dmui::ui::IsItemHovered())
				dmui::ui::SetTooltip("%s", a_text.help);
			return result;
		};
		const auto* page = FieldPage::Current();
		if (!page)
			return drawRaw();

		const std::string id(a_text.id);
		const std::string label(a_text.label);
		dmui::FieldScope field{ host::HostClient::Get().Client(), id.c_str(), label.c_str(), a_text.help };
		if (field.Result() != DMUI_RESULT_OK)
			return drawRaw();
		if (!field.Visible())
			return {};

		const std::string hidden = "##" + id;
		EditResult result = a_draw(hidden.c_str());
		if (page->NeedsRestart(a_text.label))
			static_cast<void>(field.SetFeedback(dmui::FieldFeedbackSeverity::kWarning, "Restart required"));
		if (const auto pressed = field.End(true, a_modified); pressed && *pressed) {
			a_reset();
			result.changed = result.completed = true;
		}
		return result;
	}
}
