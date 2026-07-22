#include "Render/PixelShaderSwapBroker.h"

#include "Log.h"
#include "PCH.h"

#include <atomic>
#include <d3d11.h>
#include <memory>
#include <mutex>
#include <vector>

namespace cs::engine
{
	namespace
	{
		using ObserverList = std::vector<PixelShaderSwapObserver>;

		auto* L = cs::log::Get("cs.render.pixelshaderswap");

		std::atomic<PixelShaderSwapResolver> g_resolver{ nullptr };
		std::atomic<std::shared_ptr<const ObserverList>> g_observers;
		std::mutex g_observerRegistrationMutex;

		std::mutex g_installMutex;
		ID3D11Device* g_device = nullptr;
		bool g_installRequested = false;
		std::atomic<bool> g_hookInstalled{ false };

		thread_local unsigned g_bypassDepth = 0;

		struct CreatePixelShaderHook
		{
			static HRESULT STDMETHODCALLTYPE thunk(
				ID3D11Device* a_this,
				const void* a_bytecode,
				SIZE_T a_bytecodeLength,
				ID3D11ClassLinkage* a_linkage,
				ID3D11PixelShader** a_out)
			{
				if (g_bypassDepth != 0)
					return func(a_this, a_bytecode, a_bytecodeLength, a_linkage, a_out);

				const auto observers = g_observers.load(std::memory_order_acquire);
				const bool validBytecode = a_bytecode && a_bytecodeLength != 0;
				if (validBytecode && observers) {
					for (const auto& observer : *observers) {
						if (observer.observeBytecode)
							observer.observeBytecode(a_bytecode, a_bytecodeLength);
					}
				}

				const HRESULT hr = func(a_this, a_bytecode, a_bytecodeLength, a_linkage, a_out);
				if (FAILED(hr) || !a_out || !*a_out || !validBytecode)
					return hr;

				const auto hash = sha1::Sha1Compute(a_bytecode, a_bytecodeLength);
				if (observers) {
					for (const auto& observer : *observers) {
						if (observer.observeOriginal)
							observer.observeOriginal(hash, *a_out);
					}
				}

				if (const auto resolver = g_resolver.load(std::memory_order_acquire)) {
					(void)resolver(a_bytecode, a_bytecodeLength, hash, a_out);
					if (observers) {
						for (const auto& observer : *observers) {
							if (observer.observeResolved)
								observer.observeResolved(hash, *a_out);
						}
					}
				}
				return hr;
			}

			static inline HRESULT (STDMETHODCALLTYPE *func)(
				ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**) = nullptr;
		};

		void InstallHookIfReady()
		{
			if (!g_installRequested || !g_device || g_hookInstalled.load(std::memory_order_acquire))
				return;

			stl::detour_vfunc<15, CreatePixelShaderHook>(g_device);
			g_hookInstalled.store(true, std::memory_order_release);
			L->info("Device-vtable hook installed (slot 15).");
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
		if (!a_resolver)
			return false;

		PixelShaderSwapResolver expected = nullptr;
		if (!g_resolver.compare_exchange_strong(
				expected, a_resolver, std::memory_order_release, std::memory_order_acquire))
			return false;

		// Diagnostic snapshot: with ShaderCatalog enabled, a nonzero count confirms observer-before-resolver on this boot; a zero means either ordering inverted or ShaderCatalog was inactive.
		const auto observers = g_observers.load(std::memory_order_acquire);
		const auto observerCount = observers ? observers->size() : 0;
		L->info("Resolver registered; observers_at_resolver_register={}.", observerCount);

		RequestHookInstall();
		return true;
	}

	bool RegisterPixelShaderSwapObserver(PixelShaderSwapObserver a_observer)
	{
		if (!a_observer.observeBytecode && !a_observer.observeOriginal && !a_observer.observeResolved)
			return false;

		{
			std::scoped_lock lock(g_observerRegistrationMutex);
			const auto current = g_observers.load(std::memory_order_acquire);
			if (current) {
				for (const auto& observer : *current) {
					if (observer.observeBytecode == a_observer.observeBytecode
						&& observer.observeOriginal == a_observer.observeOriginal
						&& observer.observeResolved == a_observer.observeResolved)
						return false;
				}
			}

			auto updated = current
				? std::make_shared<ObserverList>(*current)
				: std::make_shared<ObserverList>();
			updated->push_back(a_observer);
			g_observers.store(std::move(updated), std::memory_order_release);
		}

		RequestHookInstall();
		return true;
	}

	bool PixelShaderSwapBrokerHooksInstalled() noexcept
	{
		return g_hookInstalled.load(std::memory_order_acquire);
	}

	ScopedPixelShaderBrokerBypass::ScopedPixelShaderBrokerBypass() noexcept
	{
		++g_bypassDepth;
	}

	ScopedPixelShaderBrokerBypass::~ScopedPixelShaderBrokerBypass() noexcept
	{
		--g_bypassDepth;
	}
}
