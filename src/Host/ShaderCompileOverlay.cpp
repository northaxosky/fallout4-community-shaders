#include "Host/ShaderCompileOverlay.h"

#include "Render/Engine.h"
#include "Render/ShaderVariantCompilation.h"

#include <DearModdingUI/UI.h>

#include <algorithm>
#include <cfloat>
#include <format>

namespace cs::host
{
	ShaderCompilePresentation ShaderCompileOverlayModel::Update(
		const engine::ShaderCompileBatch& a_batch,
		Clock::time_point a_now) noexcept
	{
		if (a_batch.active) {
			_revealed = _revealed || engine::BatchHoldsLoads(a_batch);
			if (_revealed) {
				_readyUntil.reset();
				return { ShaderCompilePhase::kCompiling, a_batch };
			}
		} else if (_revealed) {
			_revealed = false;
			_readyUntil = a_now + std::chrono::milliseconds{ 4000 };
			_finished = a_batch;
		}
		if (_readyUntil && a_now < *_readyUntil)
			return { ShaderCompilePhase::kReady, _finished };
		_readyUntil.reset();
		return {};
	}

	bool ShaderCompileOverlay::Observe() noexcept
	{
		// Idle and hidden: no batch can have changed the presentation.
		if (_presentation.phase == ShaderCompilePhase::kHidden && !engine::IsShaderCompileBatchActive())
			return false;
		const auto batch = engine::GetShaderCompileBatch();
		_presentation = _model.Update(batch, ShaderCompileOverlayModel::Clock::now());
		return _presentation.phase != ShaderCompilePhase::kHidden;
	}

	void ShaderCompileOverlay::Draw() const
	{
		const auto& batch = _presentation.batch;
		const auto theme = dmui::ui::GetThemeColors();
		const double seconds = std::chrono::duration<double>(batch.duration).count();
		const bool ready = _presentation.phase == ShaderCompilePhase::kReady;
		const auto done = batch.FinishedTasks();
		float fraction = 1.0f;
		if (!ready)
			fraction = batch.totalTasks ? std::min(1.0f, static_cast<float>(done) / static_cast<float>(batch.totalTasks)) : 0.0f;

		if (ready) {
			dmui::ui::TextColored(theme.success, "Shaders ready:");
			dmui::ui::TextDisabled("%u compiled, %u cache hits, %.1f s",
				batch.CompiledTasks(), batch.cacheHits, seconds);
		} else {
			dmui::ui::Text("Compiling shaders:");
			dmui::ui::TextDisabled("%u / %u, %u cache hits, %.1f s",
				done, batch.totalTasks, batch.cacheHits, seconds);
		}
		if (batch.failedTasks) {
			dmui::ui::SameLine();
			dmui::ui::TextColored(theme.warning, "%u failed", batch.failedTasks);
		}
		const auto label = std::format("{:.0f}%", fraction * 100.0f);
		dmui::ui::PushStyleColor(dmui::ui::Color::kPlotHistogram, theme.success);
		dmui::ui::ProgressBar(fraction, { -FLT_MIN, 0.0f }, label.c_str());
		dmui::ui::PopStyleColor();
	}

	DMUI_ManagedOverlayOptions ShaderCompileOverlay::ManagedOverlayOptions() noexcept
	{
		constexpr float kWidthFraction = 0.4f;
		constexpr float kMinWidth = 640.0f;
		constexpr float kMargin = 20.0f;

		// Rebuilt each frame, so a resolution change re-centers via the offset.
		const auto* state = engine::GetGraphicsState();
		const float display = state ? static_cast<float>(state->screenWidth) : 0.0f;
		const float width = display > 0.0f ?
		                        std::min(std::max(display * kWidthFraction, kMinWidth), display - 2.0f * kMargin) :
		                        kMinWidth;
		const float left = display > 0.0f ? std::floor((display - width) * 0.5f) : kMargin;
		return {
			.anchor = DMUI_OVERLAY_ANCHOR_SCREEN_POSITION,
			.offset = { left, kMargin },
			.size = { width, 0.0f },
			.minimumSize = { 0.0f, 0.0f },
			.maximumSize = { 0.0f, 0.0f },
			.opacity = 0.85f,
			.contentScale = 1.0f,
			.backgroundVisible = 1u,
			.borderVisible = 1u,
			.allowArrangement = 0u
		};
	}
}
