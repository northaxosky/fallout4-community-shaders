#pragma once

#include <DearModdingUI/Client.h>

namespace cs::host
{
	inline constexpr dmui::ClientOptions kClientOptions{
		.capabilities = DMUI_CLIENT_CAPABILITY_RENDERER_REPLACEMENT
	};
}
