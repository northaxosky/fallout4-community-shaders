#pragma once

#include "DebugView.h"

#include <map>
#include <string>
#include <string_view>

namespace cs::debug_view
{
	struct Selection
	{
		std::string feature;
		std::string view;

		[[nodiscard]] bool Empty() const noexcept
		{
			return feature.empty() || view.empty();
		}
	};

	class SelectionState
	{
	public:
		using PreviewMap =
			std::map<std::string, std::string, std::less<>>;

		void Clear() noexcept
		{
			_fullscreen = {};
			_previews.clear();
		}

		void ClearFeature(std::string_view a_feature)
		{
			if (_fullscreen.feature == a_feature)
				_fullscreen = {};
			_previews.erase(std::string(a_feature));
		}

		void Select(
			std::string a_feature,
			std::string a_view,
			FeatureDebugViewKind a_kind)
		{
			ClearFeature(a_feature);
			if (a_feature.empty() || a_view.empty())
				return;

			if (a_kind == FeatureDebugViewKind::kFullscreen) {
				_fullscreen = {
					.feature = std::move(a_feature),
					.view = std::move(a_view)
				};
			} else {
				_previews.insert_or_assign(
					std::move(a_feature),
					std::move(a_view));
			}
		}

		[[nodiscard]] const Selection& Fullscreen() const noexcept
		{
			return _fullscreen;
		}

		[[nodiscard]] const PreviewMap& Previews() const noexcept
		{
			return _previews;
		}

		[[nodiscard]] std::string_view SelectedView(
			std::string_view a_feature) const noexcept
		{
			if (_fullscreen.feature == a_feature)
				return _fullscreen.view;
			const auto preview = _previews.find(a_feature);
			return preview == _previews.end() ?
				std::string_view{} :
				std::string_view(preview->second);
		}

	private:
		Selection _fullscreen;
		PreviewMap _previews;
	};
}
