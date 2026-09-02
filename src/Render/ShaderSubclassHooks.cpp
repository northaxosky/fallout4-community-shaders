#include "Render/ShaderSubclassHooks.h"

#include "Log.h"
#include "PCH.h"
#include "Render/PrepassInstrumentation.h"
#include "Render/ShaderSubclassContext.h"
#include "Render/ShaderVariantRuntimeResolver.h"

#include <Windows.h>

#include <exception>
#include <mutex>
#include <optional>

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

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.render.shadersubclass");

		ShaderSubclassHookInstallStats g_reloadStats{};
		ShaderSubclassHookInstallStats g_setupStats{};
		std::once_flag g_installOnce;

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
				shader_context::Scope scope(
					a_self, Tag::Name(), a_techniqueBits);
				const bool succeeded = func(a_self, a_techniqueBits);
				if constexpr (requires {
						Tag::OnSetupTechnique(
							a_self,
							a_techniqueBits,
							succeeded);
					}) {
					Tag::OnSetupTechnique(
						a_self,
						a_techniqueBits,
						succeeded);
				}
				return succeeded;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct PrepassRestoreTechniqueHook
		{
			static constexpr std::size_t size = 0x03;

			static void thunk(
				RE::BSShader* a_self,
				std::uint32_t a_techniqueBits)
			{
				func(a_self, a_techniqueBits);
				prepass_instrumentation::OnRestoreTechnique(a_self);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct PrepassSetupGeometryHook
		{
			static constexpr std::size_t size = 0x07;

			static void thunk(
				RE::BSShader* a_self,
				RE::BSRenderPass* a_pass)
			{
				func(a_self, a_pass);
				prepass_instrumentation::OnSetupGeometry(a_self, a_pass);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct CompleteObjectLocator
		{
			std::uint32_t signature;
			std::uint32_t offset;
			std::uint32_t constructorDisplacementOffset;
			std::int32_t typeDescriptorOffset;
			std::int32_t classDescriptorOffset;
			std::int32_t selfOffset;
		};

		struct VtableIdentity
		{
			REL::ID id;
			std::uint32_t objectOffset = 0;
			bool typeMatches = false;
			bool valid = false;
		};

		VtableIdentity InspectPrepassVtable(REL::ID a_id) noexcept
		{
			const auto address = a_id.address();
			if (address < sizeof(std::uintptr_t))
				return {};
			const auto locator = *reinterpret_cast<
				const CompleteObjectLocator* const*>(
					address - sizeof(std::uintptr_t));
			if (!locator || locator->signature != 1)
				return {};
			const auto base =
				REX::FModule::GetExecutingModule().GetBaseAddress();
			const auto typeDescriptor = base
				+ static_cast<std::uint32_t>(
					locator->typeDescriptorOffset);
			return {
				.id = a_id,
				.objectOffset = locator->offset,
				.typeMatches =
					typeDescriptor == RE::RTTI::BSDFPrePassShader.address(),
				.valid = true
			};
		}

		std::optional<REL::ID> ResolvePrepassShaderVtable()
		{
			std::optional<REL::ID> result;
			std::size_t primaryCount = 0;
			for (std::size_t index = 0;
				index < RE::VTABLE::BSDFPrePassShader.size();
				++index) {
				const auto identity = InspectPrepassVtable(
					RE::VTABLE::BSDFPrePassShader[index]);
				L->info(
					"BSDFPrePassShader vtable[{}]: valid={} object_offset=0x{:X} rtti_match={}",
					index,
					identity.valid,
					identity.objectOffset,
					identity.typeMatches);
				if (identity.valid
					&& identity.typeMatches
					&& identity.objectOffset == 0) {
					result = identity.id;
					++primaryCount;
				}
			}
			if (primaryCount != 1) {
				L->error(
					"BSDFPrePassShader primary BSShader vtable verification failed.");
				return std::nullopt;
			}
			return result;
		}

		struct Tag_BSDFPrePassShader
		{
			static const char* Name() { return "BSDFPrePassShader"; }

			static void OnSetupTechnique(
				RE::BSShader* a_shader,
				std::uint32_t a_techniqueBits,
				bool a_succeeded) noexcept
			{
				prepass_instrumentation::OnSetupTechnique(
					a_shader,
					a_techniqueBits,
					a_succeeded);
			}
		};

		struct Tag_BSDFPrePassShaderPassive
		{
			static const char* Name() { return "BSDFPrePassShader"; }
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

		template <class Tag>
		bool TryInstallPrepassSetupTechnique(REL::ID a_vtable)
		{
			++g_setupStats.attempted;
			try {
				stl::write_vfunc<0x02, SetupTechniqueHook<Tag>>(a_vtable);
				++g_setupStats.succeeded;
				L->info("Patched SetupTechnique for {}", Tag::Name());
				return true;
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
			return false;
		}

		template <class Tag>
		void TryInstallPrepassReload(REL::ID a_vtable)
		{
			++g_reloadStats.attempted;
			try {
				stl::write_vfunc<0x0B, ReloadHook<Tag>>(a_vtable);
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

		bool InstallPrepassInstrumentationHooks(REL::ID a_vtable)
		{
			if (!cs::log::PrepassTechniqueInstrumentationEnabled())
				return false;
			try {
				stl::write_vfunc<0x03, PrepassRestoreTechniqueHook>(a_vtable);
				stl::write_vfunc<0x07, PrepassSetupGeometryHook>(a_vtable);
				L->info(
					"Patched BSDFPrePassShader RestoreTechnique and SetupGeometry instrumentation hooks.");
				return true;
			} catch (const std::exception& e) {
				L->error(
					"Failed to patch BSDFPrePassShader instrumentation hooks: {}",
					e.what());
			} catch (...) {
				L->error(
					"Failed to patch BSDFPrePassShader instrumentation hooks: unknown exception");
			}
			return false;
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
			CS_HOOK_SHADER_SUBCLASS(BSBloodSplatterShader);
			CS_HOOK_SHADER_SUBCLASS(BSDFCompositeShader);
			CS_HOOK_SHADER_SUBCLASS(BSDFLightShader);
			if (const auto prepassVtable = ResolvePrepassShaderVtable()) {
				TryInstallPrepassReload<Tag_BSDFPrePassShader>(
					*prepassVtable);
				const bool instrumentationEnabled =
					cs::log::PrepassTechniqueInstrumentationEnabled();
				const bool setupInstalled = instrumentationEnabled
					? TryInstallPrepassSetupTechnique<
						Tag_BSDFPrePassShader>(*prepassVtable)
					: TryInstallPrepassSetupTechnique<
						Tag_BSDFPrePassShaderPassive>(*prepassVtable);
				const bool shaderTrackingInstalled =
					prepass_instrumentation::InstallShaderTracking();
				const bool instrumentationHooksInstalled =
					shaderTrackingInstalled
					&& InstallPrepassInstrumentationHooks(*prepassVtable);
				prepass_instrumentation::SetHooksInstalled(
					setupInstalled
					&& instrumentationHooksInstalled
					&& shaderTrackingInstalled);
			} else {
				++g_reloadStats.attempted;
				++g_reloadStats.failed;
				++g_setupStats.attempted;
				++g_setupStats.failed;
			}
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
}
