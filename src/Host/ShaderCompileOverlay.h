#pragma once

#include "Render/ShaderCompileGate.h"

#include <DearModdingUI/API.h>

#include <chrono>
#include <cstdint>
#include <optional>

namespace cs::host
{
	enum class ShaderCompilePhase : std::uint8_t
	{
		kHidden,
		kCompiling,
		kReady
	};

	struct ShaderCompilePresentation
	{
		ShaderCompilePhase phase{ ShaderCompilePhase::kHidden };
		engine::ShaderCompileBatch batch;
	};

	class ShaderCompileOverlayModel
	{
	public:
		using Clock = std::chrono::steady_clock;

		[[nodiscard]] ShaderCompilePresentation Update(
			const engine::ShaderCompileBatch& a_batch,
			Clock::time_point a_now) noexcept;

	private:
		bool _revealed{};
		std::optional<Clock::time_point> _readyUntil;
		engine::ShaderCompileBatch _finished;
	};

	// Live shader-compile progress window; frame thread only.
	class ShaderCompileOverlay
	{
	public:
		[[nodiscard]] bool Observe() noexcept;
		void Draw() const;
		[[nodiscard]] static DMUI_ManagedOverlayOptions ManagedOverlayOptions() noexcept;

	private:
		ShaderCompileOverlayModel _model;
		ShaderCompilePresentation _presentation;
	};
}
