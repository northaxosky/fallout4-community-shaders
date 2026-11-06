#include "Render/RenderTargetFormats.h"

#include "Log.h"
#include "Render/RenderHooks.h"

#include <array>
#include <atomic>
#include <mutex>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.rendertargetformats");

		using RE::BSGraphics::RenderTargetID;

		constexpr std::size_t kTrackedTargets = static_cast<std::size_t>(RenderTargetID::kCount);
		std::array<std::atomic<DXGI_FORMAT>, kTrackedTargets> g_formats{};
		std::once_flag g_installOnce;

		struct CreateRenderTarget_Hook
		{
			static void thunk(
				RE::BSGraphics::RenderTargetManager* a_this,
				RenderTargetID a_logicalID,
				const RE::BSGraphics::RenderTargetProperties* a_properties,
				RE::BSGraphics::RenderTargetCreationMode a_mode)
			{
				const auto index = static_cast<std::size_t>(a_logicalID);
				const auto format = index < kTrackedTargets ? g_formats[index].load(std::memory_order_relaxed) : DXGI_FORMAT_UNKNOWN;
				if (!a_properties || format == DXGI_FORMAT_UNKNOWN) {
					func(a_this, a_logicalID, a_properties, a_mode);
					return;
				}
				// Callers reuse one descriptor; edit in place to keep tail bytes, then restore.
				auto& properties = const_cast<RE::BSGraphics::RenderTargetProperties&>(*a_properties);
				const auto original = properties.format;
				properties.format = static_cast<RE::BSGraphics::Format>(format);
				func(a_this, a_logicalID, a_properties, a_mode);
				properties.format = original;
				L->info("Created render target {} as format {}.", index, static_cast<int>(format));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	bool RequestRenderTargetFormat(RenderTargetID a_target, DXGI_FORMAT a_format)
	{
		if (!RenderHookRegistrationAllowed("RequestRenderTargetFormat"))
			return false;
		const auto index = static_cast<std::size_t>(a_target);
		if (index >= kTrackedTargets || a_format == DXGI_FORMAT_UNKNOWN)
			return false;
		std::call_once(g_installOnce, [] {
			stl::detour_thunk<CreateRenderTarget_Hook>(RE::ID::BSGraphics::RenderTargetManager::CreateRenderTarget);
		});
		auto expected = DXGI_FORMAT_UNKNOWN;
		if (!g_formats[index].compare_exchange_strong(expected, a_format) && expected != a_format) {
			L->warn("Render target {} already requested as format {}; {} rejected.", index,
				static_cast<int>(expected), static_cast<int>(a_format));
			return false;
		}
		L->info("Render target {} will be created as format {}.", index, static_cast<int>(a_format));
		return true;
	}
}
