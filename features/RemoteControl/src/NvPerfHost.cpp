#include "Features/RemoteControl/NvPerfHost.h"

#include "Render/Engine.h"
#include "Render/FrameAnnotations.h"
#include "Render/FrameProfiler.h"
#include "Utils/F4SEUserDirectory.h"

#include <optional>

namespace NvPerf::Host
{
	namespace
	{
		// Render thread only: a capture requests it at start and releases it at finish.
		std::optional<cs::render::frame_annotations::ScopedForce> g_force;
	}

	ID3D11Device* Device()
	{
		return cs::engine::GetDevice();
	}

	ID3D11DeviceContext* Context()
	{
		return cs::engine::GetImmediateContext();
	}

	std::uint64_t FrameCount()
	{
		return cs::render::profiling::FrameSequence();
	}

	bool FrameAnnotations()
	{
		return cs::render::frame_annotations::Enabled();
	}

	void SetFrameAnnotations(bool enabled)
	{
		if (!enabled)
			g_force.reset();
		else if (!g_force)
			g_force.emplace();
	}

	std::filesystem::path LogDirectory()
	{
		return cs::util::F4SEUserDirectory();
	}

	std::filesystem::path DataDirectory()
	{
		return std::filesystem::path("Data") / "F4SE" / "Plugins" / "FO4CommunityShaders";
	}
}
