#pragma once

#include "Host/HostClient.h"

#include <DearModdingUI/Client.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace cs::ui
{
	struct EditResult
	{
		bool changed = false;
		// True once the edit is finished: a toggle, or a released slider or input.
		bool completed = false;
	};

	// Identity and presentation of one host field row.
	struct FieldSpec
	{
		// Unique on the page; also the widget id behind the hidden label.
		std::string_view id;
		std::string_view label;
		// Long-form explanation shown as the host field description.
		std::string_view help;
		// Names the setting in GetRestartSettings(); the label when empty.
		std::string_view restart;
		// Slider display format; null keeps the type's default.
		const char* format = nullptr;
		dmui::ui::SliderFlags flags = dmui::ui::SliderFlags::kNone;
		// Sliders show the stored value times this, for units like percent.
		float scale = 1.0f;
	};

	// Carries the restart-pending settings of the page being drawn.
	class FieldPage
	{
	public:
		// a_restart lists the settings (GetRestartSettings) that need a restart.
		explicit FieldPage(std::span<const std::string_view> a_restart) noexcept :
			_restart(a_restart), _previous(Slot())
		{
			Slot() = this;
		}

		~FieldPage() { Slot() = _previous; }

		FieldPage(const FieldPage&) = delete;
		FieldPage& operator=(const FieldPage&) = delete;

		[[nodiscard]] static const FieldPage* Current() noexcept { return Slot(); }

		[[nodiscard]] bool NeedsRestart(std::string_view a_name) const noexcept
		{
			for (const auto restart : _restart)
				if (restart == a_name)
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

	namespace detail
	{
		struct FieldOutcome
		{
			EditResult edit;
			bool resetPressed = false;
		};

		// a_reset: no Reset when empty, else whether it is enabled.
		template <class Draw>
		FieldOutcome DrawFieldRow(const FieldSpec& a_spec, std::optional<bool> a_reset, Draw&& a_draw)
		{
			const std::string id(a_spec.id);
			const std::string label(a_spec.label);
			const std::string help(a_spec.help);
			dmui::FieldScope field{ host::HostClient::Get().Client(), id.c_str(), label.c_str(),
				help.empty() ? nullptr : help.c_str() };
			if (field.Result() != DMUI_RESULT_OK || !field.Visible())
				return {};

			const std::string hidden = "##" + id;
			FieldOutcome outcome{ a_draw(hidden.c_str()) };
			const auto* page = FieldPage::Current();
			if (page && page->NeedsRestart(a_spec.restart.empty() ? a_spec.label : a_spec.restart))
				static_cast<void>(field.SetFeedback(dmui::FieldFeedbackSeverity::kWarning, "Restart required"));
			const auto pressed = field.End(a_reset.has_value(), a_reset.value_or(false));
			outcome.resetPressed = pressed.value_or(false);
			return outcome;
		}
	}

	// A labeled field row with a Reset enabled while a_modified.
	// a_draw(hiddenLabel) draws the control; a_reset restores the default.
	template <class Draw, class Reset>
	EditResult DrawField(const FieldSpec& a_spec, bool a_modified, Draw&& a_draw, Reset&& a_reset)
	{
		auto outcome = detail::DrawFieldRow(a_spec, a_modified, std::forward<Draw>(a_draw));
		if (outcome.resetPressed) {
			a_reset();
			outcome.edit.changed = outcome.edit.completed = true;
		}
		return outcome.edit;
	}

	// A labeled field row for an action control, without a Reset.
	template <class Draw>
	EditResult DrawActionField(const FieldSpec& a_spec, Draw&& a_draw)
	{
		return detail::DrawFieldRow(a_spec, std::nullopt, std::forward<Draw>(a_draw)).edit;
	}
}
