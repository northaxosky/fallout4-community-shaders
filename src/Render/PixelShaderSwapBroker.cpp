#include "Render/PixelShaderSwapBroker.h"

#include "Log.h"
#include "PCH.h"
#include "Render/ShaderSubclassContext.h"
#include "Render/ShaderVariantRuntimeResolver.h"

#include <atomic>
#include <algorithm>
#include <d3d11.h>
#include <memory>
#include <mutex>
#include <vector>

namespace cs::engine
{
	namespace
	{
		using ResolverList =
			std::vector<PixelShaderSwapResolverRegistration>;
		constexpr std::size_t kMaxResolvers = 8;

		auto* L = cs::log::Get("cs.render.pixelshaderswap");

		std::atomic<std::shared_ptr<const ResolverList>> g_resolvers;
		std::mutex g_resolverRegistrationMutex;
		PixelShaderResolverRegistryModel g_resolverRegistry;

		std::mutex g_installMutex;
		ID3D11Device* g_device = nullptr;
		bool g_installRequested = false;
		std::atomic<bool> g_hookInstalled{ false };

		struct CreatePixelShaderHook
		{
			static HRESULT STDMETHODCALLTYPE thunk(
				ID3D11Device* a_this,
				const void* a_bytecode,
				SIZE_T a_bytecodeLength,
				ID3D11ClassLinkage* a_linkage,
				ID3D11PixelShader** a_out)
			{
				std::optional<PixelShaderRuntimeRoute> route;
				std::optional<ShaderVariantKeyView> variant;
				const auto context = shader_context::Current();
				if (context.active
					&& context.techniqueKnown
					&& context.subclassName) {
					route = ResolvePixelShaderRuntimeRoute(
						context.subclassName,
						context.techniqueBits);
					if (route && route->pluginResolvedPsid) {
						variant = ShaderVariantKeyView{
							route->subclass,
							route->stage,
							*route->pluginResolvedPsid
						};
					}
				}
				const auto resolvers = g_resolvers.load(std::memory_order_acquire);
				const std::span<const PixelShaderSwapResolverRegistration>
					resolverSpan =
						resolvers
						? std::span<const PixelShaderSwapResolverRegistration>(
							*resolvers)
						: std::span<
							const PixelShaderSwapResolverRegistration>{};
				return ExecutePixelShaderSwapPipeline(
					func,
					resolverSpan,
					variant,
					PixelShaderBrokerBypassActive(),
					a_this,
					a_bytecode,
					a_bytecodeLength,
					a_linkage,
					a_out);
			}

			static inline CreatePixelShaderFunction func = nullptr;
		};

		void InstallHookIfReady()
		{
			if (!g_installRequested || !g_device || g_hookInstalled.load(std::memory_order_acquire))
				return;

			stl::detour_vfunc<15, CreatePixelShaderHook>(g_device);
			const bool installed = CreatePixelShaderHook::func != nullptr;
			g_hookInstalled.store(installed, std::memory_order_release);
			if (installed)
				L->info("Device-vtable hook installed (slot 15).");
			else
				L->error("Device-vtable hook slot 15 has no original.");
		}

		void RequestHookInstall()
		{
			sha1::Sha1InitOnce();
			std::scoped_lock lock(g_installMutex);
			g_installRequested = true;
			InstallHookIfReady();
		}
	}

	void SetPixelShaderSwapBrokerDevice(ID3D11Device* a_device)
	{
		if (!a_device)
			return;

		std::scoped_lock lock(g_installMutex);
		if (!g_device)
			g_device = a_device;
		InstallHookIfReady();
	}

	bool RegisterPixelShaderSwapResolver(PixelShaderSwapResolver a_resolver)
	{
		return RegisterPixelShaderSwapResolver({
			.resolver = a_resolver,
			.priority = kHlslReplacementResolverPriority
		});
	}

	bool RegisterPixelShaderSwapResolver(
		PixelShaderSwapResolverRegistration a_registration)
	{
		if (!a_registration.resolver)
			return false;
		std::size_t resolverCount = 0;
		{
			std::scoped_lock lock(g_resolverRegistrationMutex);
			const auto current = g_resolvers.load(std::memory_order_acquire);
			if (current) {
				if (current->size() >= kMaxResolvers
					|| std::ranges::any_of(
						*current,
						[&a_registration](
							const PixelShaderSwapResolverRegistration& a_existing) {
							return a_existing.resolver
								== a_registration.resolver;
						})) {
					return false;
				}
			}
			auto updated = current
				? std::make_shared<ResolverList>(*current)
				: std::make_shared<ResolverList>();
			updated->push_back(a_registration);
			std::stable_sort(
				updated->begin(),
				updated->end(),
				[](const auto& a_left, const auto& a_right) {
					return a_left.priority < a_right.priority;
				});
			resolverCount = updated->size();
			(void)g_resolverRegistry.Register(
				a_registration.priority);
			g_resolvers.store(std::move(updated), std::memory_order_release);
		}

		L->info(
			"Resolver registered (priority={}, resolvers={}).",
			a_registration.priority,
			resolverCount);

		RequestHookInstall();
		return true;
	}

	std::uintptr_t PixelShaderSwapBrokerCreateHookAddress() noexcept
	{
		return reinterpret_cast<std::uintptr_t>(
			&CreatePixelShaderHook::thunk);
	}

	bool PixelShaderSwapBrokerHooksInstalled() noexcept
	{
		std::scoped_lock lock(g_installMutex);
		InstallHookIfReady();
		return g_hookInstalled.load(std::memory_order_acquire);
	}

}
