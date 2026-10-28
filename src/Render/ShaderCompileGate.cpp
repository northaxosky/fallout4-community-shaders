#include "Render/ShaderCompileGate.h"

#include "Log.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderVariantCompilation.h"
#include "Settings/FeatureConfig.h"
#include "Settings/SettingsRegistry.h"

#include <RE/M/Main.h>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.shadercache");

		// Smaller batches are incremental discoveries, not the startup compile.
		constexpr std::uint32_t kStartupBatchMinimum = 100;

		void LogSummary(const ShaderCompileBatch& a_batch)
		{
			const auto level = a_batch.totalTasks >= kStartupBatchMinimum ?
			                       spdlog::level::info :
			                       spdlog::level::debug;
			L->log(
				level,
				"Shader compilation finished: {} variants, {} compiled, {} cache hits, "
				"{} failed, {:.1f} s",
				a_batch.totalTasks,
				a_batch.CompiledTasks(),
				a_batch.cacheHits,
				a_batch.failedTasks,
				std::chrono::duration<double>(a_batch.duration).count());
		}
	}

	namespace
	{
		// MainMenu keeps Continue/Load/New queued behind its spinner while false.
		struct GetCanLoad_Hook
		{
			static bool thunk()
			{
				// Menu advance runs on the main thread only.
				static std::optional<std::chrono::steady_clock::time_point> heldSince;
				if (ShaderCompileHoldsLoads()) {
					if (!heldSince) {
						heldSince = std::chrono::steady_clock::now();
						L->info("Deferring menu loads until shader compilation finishes");
					}
					return false;
				}
				if (heldSince) {
					L->info(
						"Releasing menu loads after {:.1f} s",
						std::chrono::duration<double>(std::chrono::steady_clock::now() - *heldSince).count());
					heldSince.reset();
				}
				return func();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void InstallShaderCompileLoadGate()
	{
		if (!ShaderCompileBlocksPlay())
			return;
		stl::detour_thunk<GetCanLoad_Hook>(RE::ID::Main::GetCanLoad);
		if (GetCanLoad_Hook::func.address() == 0)
			L->error("Hook failed on Main::GetCanLoad; loads will not wait for shaders");
		else
			L->info("Hook installed on Main::GetCanLoad at {:#x}", GetCanLoad_Hook::func.address());
	}

	void InstallShaderCompileSummaryLog()
	{
		AddShaderCompileBatchObserver(&LogSummary);
	}

	bool ShaderCompileBlocksPlay()
	{
		// The setting is file-only, so one read serves the session.
		static const bool blocks = [] {
			std::string error;
			const bool background = settings::ParseCoreSection(
				settings::core::kShaderCache, feature_config::GetRoot(), "shader_cache", error)
			                            .backgroundCompilation;
			if (!error.empty())
				L->warn("{}; using the default", error);
			return !background;
		}();
		return blocks;
	}

	bool ShaderCompileHoldsLoads()
	{
		// Quitting or a stopped batch fails open so the game never hangs.
		const auto* main = RE::Main::GetSingleton();
		if (!ShaderCompileBlocksPlay() || (main && main->quitGame))
			return false;
		return BatchHoldsLoads(GetShaderCompileBatch());
	}

	void StopShaderCompileOnQuit() noexcept
	{
		const auto* main = RE::Main::GetSingleton();
		if (!main || !main->quitGame)
			return;
		static const bool stopped = [] {
			L->info("Game is quitting; stopping shader compilation");
			StopNativeShaderVariantCompilations();
			return true;
		}();
		(void)stopped;
	}
}
