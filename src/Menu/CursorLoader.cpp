#include "Menu/CursorLoader.h"

#include "Log.h"
#include "Menu/Menu.h"
#include "Utils/UI.h"

#include <array>
#include <filesystem>

#include <imgui.h>

namespace
{
	auto* L = cs::log::Get("menu");

	struct LoadedCursor
	{
		ID3D11ShaderResourceView* texture = nullptr;
		ImVec2 size{};

		void Release()
		{
			if (texture) {
				texture->Release();
				texture = nullptr;
			}
		}
	};

	std::array<LoadedCursor, ImGuiMouseCursor_COUNT> g_cursors{};
	int g_loadedCount = 0;
}

namespace cs::CursorLoader
{
	bool Reload(Menu* a_menu)
	{
		if (!a_menu)
			return false;

		auto* device = a_menu->GetDevice();
		if (!device)
			return false;

		Shutdown();

		const auto& cursor = a_menu->GetTheme().Cursor;
		if (!a_menu->GetTheme().UseCustomCursor)
			return true;

		const auto cursorsRoot = ui::paths::GetPluginPath() / "Cursors";
		for (int i = 0; i < ImGuiMouseCursor_COUNT; ++i) {
			const auto& image = cursor.Types[static_cast<std::size_t>(i)];
			if (image.File.empty())
				continue;

			const auto path = cursorsRoot / image.File;
			if (!ui::paths::IsPathWithinDirectory(cursorsRoot, path)) {
				L->error("Cursor path traversal attempt: {}", image.File);
				continue;
			}

			std::error_code ec;
			if (!std::filesystem::exists(path, ec))
				continue;

			auto& slot = g_cursors[static_cast<std::size_t>(i)];
			if (ui::LoadTextureFromFile(device, path.string().c_str(), &slot.texture, slot.size))
				++g_loadedCount;
		}

		return true;
	}

	int GetLoadedCount()
	{
		return g_loadedCount;
	}

	void Shutdown()
	{
		for (auto& cursor : g_cursors)
			cursor.Release();
		g_loadedCount = 0;
	}

	void DrawCustomCursor(const Menu& a_menu)
	{
		const auto& theme = a_menu.GetTheme();
		if (!theme.UseCustomCursor || g_loadedCount == 0)
			return;

		const ImGuiMouseCursor active = ImGui::GetMouseCursor();
		if (active < 0 || active >= ImGuiMouseCursor_COUNT)
			return;

		const auto& slot = g_cursors[static_cast<std::size_t>(active)];
		if (!slot.texture)
			return;

		// Suppress the built-in cursor only when a themed image actually covers this shape.
		ImGui::GetIO().MouseDrawCursor = false;

		const auto& image = theme.Cursor.Types[static_cast<std::size_t>(active)];
		const float scale = theme.Cursor.Scale > 0.0f ? theme.Cursor.Scale : 1.0f;
		const ImVec2 mouse = ImGui::GetMousePos();
		const ImVec2 min(mouse.x - image.HotspotX * scale, mouse.y - image.HotspotY * scale);
		const ImVec2 max(min.x + slot.size.x * scale, min.y + slot.size.y * scale);

		ImGui::GetForegroundDrawList()->AddImage(slot.texture, min, max);
	}
}
