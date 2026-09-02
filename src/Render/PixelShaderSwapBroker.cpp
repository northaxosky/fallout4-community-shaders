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
		ShaderStageMask g_installRequestedStages = 0;
		std::atomic<ShaderStageMask> g_hookInstalledStages{ 0 };

		using CreatePixelShaderFunction = HRESULT (STDMETHODCALLTYPE *)(
			ID3D11Device*,
			const void*,
			SIZE_T,
			ID3D11ClassLinkage*,
			ID3D11PixelShader**);
		using CreateVertexShaderFunction = HRESULT (STDMETHODCALLTYPE *)(
			ID3D11Device*,
			const void*,
			SIZE_T,
			ID3D11ClassLinkage*,
			ID3D11VertexShader**);
		using CreateComputeShaderFunction = HRESULT (STDMETHODCALLTYPE *)(
			ID3D11Device*,
			const void*,
			SIZE_T,
			ID3D11ClassLinkage*,
			ID3D11ComputeShader**);

		std::optional<ShaderVariantKeyView> ResolveRuntimeVariant(
			ShaderStage a_stage)
		{
			if (a_stage != ShaderStage::kPixel)
				return std::nullopt;

			const auto context = shader_context::Current();
			if (!context.active
				|| !context.techniqueKnown
				|| !context.subclassName) {
				return std::nullopt;
			}
			const auto route = ResolvePixelShaderRuntimeRoute(
				context.subclassName,
				context.techniqueBits);
			if (!route || !route->pluginResolvedPsid)
				return std::nullopt;
			return ShaderVariantKeyView{
				route->subclass,
				route->stage,
				*route->pluginResolvedPsid
			};
		}

		std::span<const PixelShaderSwapResolverRegistration>
			GetResolverSpan(
				const std::shared_ptr<const ResolverList>& a_resolvers)
		{
			return a_resolvers
				? std::span<const PixelShaderSwapResolverRegistration>(
					*a_resolvers)
				: std::span<const PixelShaderSwapResolverRegistration>{};
		}

		struct CreatePixelShaderHook
		{
			static HRESULT STDMETHODCALLTYPE CallOriginal(
				ID3D11Device* a_this,
				const void* a_bytecode,
				SIZE_T a_bytecodeLength,
				ID3D11ClassLinkage* a_linkage,
				ID3D11DeviceChild** a_out)
			{
				if (!func)
					return E_POINTER;
				return func(
					a_this,
					a_bytecode,
					a_bytecodeLength,
					a_linkage,
					reinterpret_cast<ID3D11PixelShader**>(a_out));
			}

			static HRESULT STDMETHODCALLTYPE thunk(
				ID3D11Device* a_this,
				const void* a_bytecode,
				SIZE_T a_bytecodeLength,
				ID3D11ClassLinkage* a_linkage,
				ID3D11PixelShader** a_out)
			{
				const auto resolvers = g_resolvers.load(std::memory_order_acquire);
				return ExecuteShaderSwapPipeline(
					&CallOriginal,
					GetResolverSpan(resolvers),
					ResolveRuntimeVariant(ShaderStage::kPixel),
					PixelShaderBrokerBypassActive(),
					ShaderStage::kPixel,
					a_this,
					a_bytecode,
					a_bytecodeLength,
					a_linkage,
					reinterpret_cast<ID3D11DeviceChild**>(a_out));
			}

			static inline CreatePixelShaderFunction func = nullptr;
		};

		struct CreateVertexShaderHook
		{
			static HRESULT STDMETHODCALLTYPE CallOriginal(
				ID3D11Device* a_this,
				const void* a_bytecode,
				SIZE_T a_bytecodeLength,
				ID3D11ClassLinkage* a_linkage,
				ID3D11DeviceChild** a_out)
			{
				if (!func)
					return E_POINTER;
				return func(
					a_this,
					a_bytecode,
					a_bytecodeLength,
					a_linkage,
					reinterpret_cast<ID3D11VertexShader**>(a_out));
			}

			static HRESULT STDMETHODCALLTYPE thunk(
				ID3D11Device* a_this,
				const void* a_bytecode,
				SIZE_T a_bytecodeLength,
				ID3D11ClassLinkage* a_linkage,
				ID3D11VertexShader** a_out)
			{
				const auto resolvers = g_resolvers.load(std::memory_order_acquire);
				return ExecuteShaderSwapPipeline(
					&CallOriginal,
					GetResolverSpan(resolvers),
					ResolveRuntimeVariant(ShaderStage::kVertex),
					PixelShaderBrokerBypassActive(),
					ShaderStage::kVertex,
					a_this,
					a_bytecode,
					a_bytecodeLength,
					a_linkage,
					reinterpret_cast<ID3D11DeviceChild**>(a_out));
			}

			static inline CreateVertexShaderFunction func = nullptr;
		};

		struct CreateComputeShaderHook
		{
			static HRESULT STDMETHODCALLTYPE CallOriginal(
				ID3D11Device* a_this,
				const void* a_bytecode,
				SIZE_T a_bytecodeLength,
				ID3D11ClassLinkage* a_linkage,
				ID3D11DeviceChild** a_out)
			{
				if (!func)
					return E_POINTER;
				return func(
					a_this,
					a_bytecode,
					a_bytecodeLength,
					a_linkage,
					reinterpret_cast<ID3D11ComputeShader**>(a_out));
			}

			static HRESULT STDMETHODCALLTYPE thunk(
				ID3D11Device* a_this,
				const void* a_bytecode,
				SIZE_T a_bytecodeLength,
				ID3D11ClassLinkage* a_linkage,
				ID3D11ComputeShader** a_out)
			{
				const auto resolvers = g_resolvers.load(std::memory_order_acquire);
				return ExecuteShaderSwapPipeline(
					&CallOriginal,
					GetResolverSpan(resolvers),
					ResolveRuntimeVariant(ShaderStage::kCompute),
					PixelShaderBrokerBypassActive(),
					ShaderStage::kCompute,
					a_this,
					a_bytecode,
					a_bytecodeLength,
					a_linkage,
					reinterpret_cast<ID3D11DeviceChild**>(a_out));
			}

			static inline CreateComputeShaderFunction func = nullptr;
		};

		void InstallHookIfReady()
		{
			if (!g_device)
				return;

			auto installedStages =
				g_hookInstalledStages.load(std::memory_order_acquire);
			const auto pixelBit = ShaderStageBit(ShaderStage::kPixel);
			if ((g_installRequestedStages & pixelBit) != 0
				&& (installedStages & pixelBit) == 0) {
				stl::detour_vfunc<15, CreatePixelShaderHook>(g_device);
				if (CreatePixelShaderHook::func) {
					installedStages |= pixelBit;
					L->info("Device-vtable hook installed (slot 15).");
				} else {
					L->error("Device-vtable hook slot 15 has no original.");
				}
			}

			const auto vertexBit = ShaderStageBit(ShaderStage::kVertex);
			if ((g_installRequestedStages & vertexBit) != 0
				&& (installedStages & vertexBit) == 0) {
				stl::detour_vfunc<12, CreateVertexShaderHook>(g_device);
				if (CreateVertexShaderHook::func) {
					installedStages |= vertexBit;
					L->info("Device-vtable hook installed (slot 12).");
				} else {
					L->error("Device-vtable hook slot 12 has no original.");
				}
			}

			const auto computeBit = ShaderStageBit(ShaderStage::kCompute);
			if ((g_installRequestedStages & computeBit) != 0
				&& (installedStages & computeBit) == 0) {
				stl::detour_vfunc<18, CreateComputeShaderHook>(g_device);
				if (CreateComputeShaderHook::func) {
					installedStages |= computeBit;
					L->info("Device-vtable hook installed (slot 18).");
				} else {
					L->error("Device-vtable hook slot 18 has no original.");
				}
			}
			g_hookInstalledStages.store(
				installedStages,
				std::memory_order_release);
		}

		void RequestHookInstall(ShaderStageMask a_stages)
		{
			sha1::Sha1InitOnce();
			std::scoped_lock lock(g_installMutex);
			g_installRequestedStages |= a_stages;
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

	bool RegisterPixelShaderSwapResolver(ShaderSwapResolver a_resolver)
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

		RequestHookInstall(a_registration.stages);
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
		return g_installRequestedStages != 0
			&& (
			g_hookInstalledStages.load(std::memory_order_acquire)
			& g_installRequestedStages)
			== g_installRequestedStages;
	}

}
