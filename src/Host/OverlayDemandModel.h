#pragma once

#include <cstdint>

namespace cs::host
{
	class OverlayDemandModel
	{
	public:
		enum class Action : std::uint8_t
		{
			kNone,
			kRequest,
			kRelease
		};

		Action Plan(bool a_wanted) const noexcept
		{
			if (a_wanted == _held)
				return Action::kNone;
			return a_wanted ? Action::kRequest : Action::kRelease;
		}

		void Confirm(Action a_action) noexcept
		{
			if (a_action == Action::kRequest)
				_held = true;
			else if (a_action == Action::kRelease)
				_held = false;
		}

		bool Held() const noexcept { return _held; }

	private:
		bool _held{ false };
	};
}
