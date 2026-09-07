#pragma once

#include <DearModdingUI/Client.h>

#include <cstdarg>
#include <cstdio>

namespace cs::ui
{
	inline ImVec4 ToImVec4(const DMUI_Vec4& a_color) noexcept
	{
		return { a_color.x, a_color.y, a_color.z, a_color.w };
	}

	class SettingsTableScope
	{
	public:
		SettingsTableScope(dmui::Client& a_client, const char* a_id) noexcept :
			_client(a_client)
		{
			const auto result = _client.BeginSettingsTable(a_id);
			_valid = result.has_value();
			_active = result && *result;
		}

		~SettingsTableScope() noexcept
		{
			if (_active)
				(void)_client.EndSettingsTable();
		}

		SettingsTableScope(const SettingsTableScope&) = delete;
		SettingsTableScope& operator=(const SettingsTableScope&) = delete;

		[[nodiscard]] bool Valid() const noexcept { return _valid; }
		[[nodiscard]] bool Visible() const noexcept { return _active; }

	private:
		dmui::Client& _client;
		bool _valid{};
		bool _active{};
	};

	class SettingsRowScope
	{
	public:
		SettingsRowScope(
			dmui::Client& a_client,
			const char* a_id,
			const char* a_label,
			const char* a_description = "",
			dmui::RowPresentation::Layout a_layout =
				dmui::RowPresentation::Layout::kLabelValue) noexcept :
			_client(a_client)
		{
			const auto result =
				_client.BeginSettingsRow(a_id, a_label, a_description, a_layout);
			_valid = result.has_value();
			_active = result && *result;
		}

		~SettingsRowScope() noexcept
		{
			if (_active)
				(void)_client.EndSettingsRow(false, false);
		}

		SettingsRowScope(const SettingsRowScope&) = delete;
		SettingsRowScope& operator=(const SettingsRowScope&) = delete;

		[[nodiscard]] bool Valid() const noexcept { return _valid; }
		[[nodiscard]] bool Visible() const noexcept { return _active; }

		[[nodiscard]] std::optional<bool> End(
			bool a_resetVisible = false,
			bool a_resetEnabled = false) noexcept
		{
			if (!_active)
				return false;
			_active = false;
			return _client.EndSettingsRow(a_resetVisible, a_resetEnabled);
		}

	private:
		dmui::Client& _client;
		bool _valid{};
		bool _active{};
	};

	class HoverTooltipWrapper
	{
	public:
		HoverTooltipWrapper() noexcept :
			hovered(ImGui::IsItemHovered())
		{
			if (hovered)
				(void)ImGui::BeginTooltip();
		}

		~HoverTooltipWrapper() noexcept
		{
			if (hovered)
				ImGui::EndTooltip();
		}

		HoverTooltipWrapper(const HoverTooltipWrapper&) = delete;
		HoverTooltipWrapper& operator=(const HoverTooltipWrapper&) = delete;
		explicit operator bool() const noexcept { return hovered; }

	private:
		bool hovered{};
	};

	namespace Text
	{
		inline void WrappedWarning(const char* a_format, ...)
		{
			char buffer[1024]{};
			std::va_list args;
			va_start(args, a_format);
			std::vsnprintf(buffer, sizeof(buffer), a_format, args);
			va_end(args);
			ImGui::TextWrapped("Warning: %s", buffer);
		}
	}
}
