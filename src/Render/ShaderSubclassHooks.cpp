#include "Render/ShaderSubclassHooks.h"

#include "Log.h"
#include "PCH.h"
#include "Render/EngineCurrentPixelShader.h"
#include "Render/EnginePixelShaderLookup.h"
#include "Render/EnginePixelShaderIdentity.h"
#include "Render/ShaderSubclassContext.h"
#include "Render/ShaderVariantRuntimeResolver.h"

#include <Windows.h>

#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <vector>

#include "RE/B/BSBloodSplatterShader.h"
#include "RE/B/BSDFCompositeShader.h"
#include "RE/B/BSDFLightShader.h"
#include "RE/B/BSDFPrePassShader.h"
#include "RE/B/BSDistantTreeShader.h"
#include "RE/B/BSEffectShader.h"
#include "RE/B/BSFaceCustomizationShader.h"
#include "RE/B/BSLightingShader.h"
#include "RE/B/BSParticleShader.h"
#include "RE/B/BSSkyShader.h"
#include "RE/B/BSUtilityShader.h"
#include "RE/B/BSWaterShader.h"

#pragma comment(lib, "version.lib")

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shadersubclass");

		std::atomic<std::uint64_t> g_setupCalls{ 0 };
		std::atomic<ShaderSubclassSetupObserver> g_setupObserver{ nullptr };
		ShaderSubclassHookInstallStats g_reloadStats{};
		ShaderSubclassHookInstallStats g_setupStats{};
		ShaderSubclassRuntimeLayout g_runtimeLayout{};
		std::once_flag g_installOnce;
		std::once_flag g_layoutOnce;

		struct RuntimeVersion
		{
			std::uint16_t major = 0;
			std::uint16_t minor = 0;
			std::uint16_t build = 0;
			bool valid = false;
		};

		RuntimeVersion GetRuntimeVersion()
		{
			RuntimeVersion version;
			const HMODULE module = ::GetModuleHandleW(L"Fallout4.exe");
			if (!module)
				return version;

			wchar_t path[MAX_PATH]{};
			if (!::GetModuleFileNameW(module, path, MAX_PATH))
				return version;

			DWORD unused = 0;
			const DWORD size = ::GetFileVersionInfoSizeW(path, &unused);
			if (!size)
				return version;

			std::vector<unsigned char> buffer(size);
			if (!::GetFileVersionInfoW(path, 0, size, buffer.data()))
				return version;

			VS_FIXEDFILEINFO* info = nullptr;
			UINT infoLength = 0;
			if (!::VerQueryValueW(
					buffer.data(),
					L"\\",
					reinterpret_cast<LPVOID*>(&info),
					&infoLength)
				|| !info) {
				return version;
			}

			version.major = HIWORD(info->dwFileVersionMS);
			version.minor = LOWORD(info->dwFileVersionMS);
			version.build = HIWORD(info->dwFileVersionLS);
			version.valid = true;
			return version;
		}

		ShaderSubclassRuntimeLayout DetectRuntimeLayout()
		{
			const auto version = GetRuntimeVersion();
			if (!version.valid || version.major != 1)
				return {};
			if (version.minor == 10
				&& (version.build == 163
					|| version.build == 980
					|| version.build == 984)) {
				return { true, 0xB0 };
			}
			if (version.minor == 11 && version.build == 221)
				return { true, 0x128 };
			return {};
		}

		template <class Tag>
		struct ReloadHook
		{
			static constexpr std::size_t size = 0x0B;

			static void thunk(void* a_self, bool a_clear)
			{
				shader_context::Scope scope(
					a_self, Tag::Name(), std::nullopt);
				func(a_self, a_clear);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <class Tag>
		struct SetupTechniqueHook
		{
			static constexpr std::size_t size = 0x02;

			static bool thunk(
				RE::BSShader* a_self,
				std::uint32_t a_techniqueBits)
			{
				g_setupCalls.fetch_add(1, std::memory_order_relaxed);
				shader_context::SetSticky(Tag::Name(), a_techniqueBits);
				bool result = false;
				std::optional<EnginePixelShaderIdentityResult>
					enginePixelShader;
				{
					shader_context::Scope scope(
						a_self, Tag::Name(), a_techniqueBits);
					EnginePixelShaderLookupScope lookupScope(
						a_self, Tag::Name(), a_techniqueBits);
					result = func(a_self, a_techniqueBits);
					if (result) {
						const auto lookup = lookupScope.Snapshot(
							a_self, Tag::Name(), a_techniqueBits);
						enginePixelShader = ReconcileEnginePixelShaderId(
							lookup, ReadEngineCurrentPixelShader());
					}
				}
				if (!result) {
					shader_context::ClearSticky();
					return false;
				}
				std::optional<ShaderVariantId> pluginResolvedPsid;
				std::optional<bool> tiledLighting;
				if (const auto route = ResolvePixelShaderRuntimeRoute(
						Tag::Name(), a_techniqueBits)) {
					pluginResolvedPsid = route->pluginResolvedPsid;
					tiledLighting = route->tiledLighting;
				}
				if (const auto observer =
						g_setupObserver.load(std::memory_order_acquire)) {
					const ShaderSubclassSetupObservation observation{
						.shader = a_self,
						.subclass = Tag::Name(),
						.rawTechnique = a_techniqueBits,
						.enginePixelShader = *enginePixelShader,
						.pluginResolvedPsid = pluginResolvedPsid,
						.tiledLighting = tiledLighting
					};
					observer(observation);
				}
				return true;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <class Subclass, class Tag>
		void TryInstallReload()
		{
			++g_reloadStats.attempted;
			try {
				stl::write_vfunc<Subclass, 0, ReloadHook<Tag>>();
				++g_reloadStats.succeeded;
				L->info("Patched ReloadShaders for {}", Tag::Name());
			} catch (const std::exception& e) {
				++g_reloadStats.failed;
				L->warn(
					"Failed to patch ReloadShaders for {}: {}",
					Tag::Name(),
					e.what());
			} catch (...) {
				++g_reloadStats.failed;
				L->warn(
					"Failed to patch ReloadShaders for {}: unknown exception",
					Tag::Name());
			}
		}

		template <class Subclass, class Tag>
		void TryInstallSetupTechnique()
		{
			++g_setupStats.attempted;
			try {
				stl::write_vfunc<Subclass, 0, SetupTechniqueHook<Tag>>();
				++g_setupStats.succeeded;
				L->info("Patched SetupTechnique for {}", Tag::Name());
			} catch (const std::exception& e) {
				++g_setupStats.failed;
				L->warn(
					"Failed to patch SetupTechnique for {}: {}",
					Tag::Name(),
					e.what());
			} catch (...) {
				++g_setupStats.failed;
				L->warn(
					"Failed to patch SetupTechnique for {}: unknown exception",
					Tag::Name());
			}
		}

#define CS_HOOK_SHADER_SUBCLASS(klass)                                      \
	struct Tag_##klass { static const char* Name() { return #klass; } };    \
	TryInstallReload<RE::klass, Tag_##klass>();                             \
	TryInstallSetupTechnique<RE::klass, Tag_##klass>()
	}

	void InstallShaderSubclassHooks()
	{
		std::call_once(g_installOnce, [] {
			g_runtimeLayout = GetShaderSubclassRuntimeLayout();
			if (!g_runtimeLayout.verified) {
				L->warn(
					"Shader injection dispatch mode: exact-hash fallback "
					"(unverified BSShader layout).");
				return;
			}

			CS_HOOK_SHADER_SUBCLASS(BSBloodSplatterShader);
			CS_HOOK_SHADER_SUBCLASS(BSDFCompositeShader);
			CS_HOOK_SHADER_SUBCLASS(BSDFLightShader);
			CS_HOOK_SHADER_SUBCLASS(BSDFPrePassShader);
			CS_HOOK_SHADER_SUBCLASS(BSDistantTreeShader);
			CS_HOOK_SHADER_SUBCLASS(BSEffectShader);
			CS_HOOK_SHADER_SUBCLASS(BSFaceCustomizationShader);
			CS_HOOK_SHADER_SUBCLASS(BSLightingShader);
			CS_HOOK_SHADER_SUBCLASS(BSParticleShader);
			CS_HOOK_SHADER_SUBCLASS(BSSkyShader);
			CS_HOOK_SHADER_SUBCLASS(BSUtilityShader);
			CS_HOOK_SHADER_SUBCLASS(BSWaterShader);

			L->info(
				"Subclass ReloadShaders hooks: {}/{} patched ({} failed)",
				g_reloadStats.succeeded,
				g_reloadStats.attempted,
				g_reloadStats.failed);
			L->info(
				"Subclass SetupTechnique hooks: {}/{} patched ({} failed)",
				g_setupStats.succeeded,
				g_setupStats.attempted,
				g_setupStats.failed);
			if (g_setupStats.succeeded == 0) {
				L->error(
					"Shader injection dispatch mode: exact-hash fallback "
					"(no SetupTechnique hooks installed).");
			} else if (!IsPixelShaderVariantResolutionAvailable()) {
				L->info(
					"Shader injection dispatch mode: exact-hash fallback "
					"(resolved variant keys unavailable for this runtime).");
			} else {
				L->info(
					"Shader injection dispatch mode: resolved variant key "
					"with exact-hash fallback.");
			}
		});
	}

	bool RegisterShaderSubclassSetupObserver(
		ShaderSubclassSetupObserver a_observer) noexcept
	{
		if (!a_observer)
			return false;
		ShaderSubclassSetupObserver expected = nullptr;
		return g_setupObserver.compare_exchange_strong(
			expected,
			a_observer,
			std::memory_order_release,
			std::memory_order_acquire);
	}

	ShaderSubclassRuntimeLayout GetShaderSubclassRuntimeLayout() noexcept
	{
		std::call_once(g_layoutOnce, [] {
			try {
				g_runtimeLayout = DetectRuntimeLayout();
			} catch (...) {
				g_runtimeLayout = {};
				L->error(
					"BSShader runtime layout detection failed; "
					"using exact-hash fallback.");
			}
		});
		return g_runtimeLayout;
	}

	ShaderSubclassHookInstallStats
	GetReloadShaderHookInstallStats() noexcept
	{
		return g_reloadStats;
	}

	ShaderSubclassHookInstallStats
	GetSetupTechniqueHookInstallStats() noexcept
	{
		return g_setupStats;
	}

	ShaderSubclassHookRuntimeStats
	GetShaderSubclassHookRuntimeStats() noexcept
	{
		return {
			g_setupCalls.load(std::memory_order_relaxed)
		};
	}
}
