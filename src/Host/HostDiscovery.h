#pragma once

#include "Host/HostDiscoveryModel.h"

#include <optional>
#include <string>

#include <DearModdingUI/API.h>

namespace cs::host
{
	struct DiscoveredHost
	{
		const DMUI_HostAPI* api{ nullptr };
		std::string modulePath;
	};

	std::optional<DiscoveredHost> DiscoverHost(const DMUI_ImGuiFingerprint& a_expected) noexcept;
}
