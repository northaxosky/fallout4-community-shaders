#include "Render/D3D11Bootstrap.h"

#include <atomic>
#include <exception>
#include <string>
#include <string_view>

#include "Feature.h"
#include "Host/HostClient.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Render/FrameBuffer.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Utils/CSUtil.h"
#include "Utils/ShaderCache/CacheStorage.h"
#include "Utils/ShaderCache/CompilerIdentity.h"
#include "Utils/ShaderCache/ShaderCache.h"

namespace cs::d3d11
{
	namespace
	{
		auto* L = cs::log::Get("cs.d3d11.bootstrap");
		auto* CacheL = cs::log::Get("cs.shadercache");
		std::atomic<bool> ready{ false };

		template <class Callback>
		void InvokeOwner(std::string_view a_name, Callback&& a_callback) noexcept
		{
			try {
				a_callback();
			} catch (const std::exception& e) {
				try {
					L->error("{} failed: {}", a_name, e.what());
				} catch (...) {
				}
			} catch (...) {
				try {
					L->error("{} failed: unknown exception", a_name);
				} catch (...) {
				}
			}
		}

		void InitializeShaderCache()
		{
			shader_cache::ResetShaderCacheCounters();
			const auto root = shader_cache::DefaultCacheRoot();
			const auto& identity =
				shader_cache::GetD3DCompilerIdentity();
			CacheL->info(
				"shader cache compiler identity: {}, method={}, size={}, path={}",
				shader_cache::DescribeCompilerIdentity(identity),
				shader_cache::DescribeCompilerIdentityMechanism(
					identity.mechanism),
				identity.moduleLength,
				identity.modulePath.string());

			if (!identity.established)
				return;
			const auto synchronized =
				shader_cache::SynchronizeCacheIdentity(
					root,
					identity,
					shader_cache::kRecordSchemaVersion);
			if (!synchronized.resetMessage.empty())
				CacheL->info("{}", synchronized.resetMessage);
			if (!synchronized.error.empty())
				CacheL->warn("Shader cache: {}", synchronized.error);
		}

		void LogShaderCacheSummary()
		{
			const auto counters =
				shader_cache::GetShaderCacheCounters();
			CacheL->info(
				"shader cache: {} hit / {} absent / {} stale / {} rejected, {} written, root={}, compiler={}",
				counters.hit,
				counters.absent,
				counters.stale,
				counters.rejected,
				counters.written,
				shader_cache::DefaultCacheRoot().string(),
				shader_cache::DescribeCompilerIdentity(
					shader_cache::GetD3DCompilerIdentity()));
		}
	}

	void RunBootstrapPostCreate(
		HRESULT a_result,
		IDXGIAdapter* a_adapter,
		const DXGI_SWAP_CHAIN_DESC* a_swapChainDesc,
		IDXGISwapChain** a_swapChain,
		ID3D11Device** a_device,
		ID3D11DeviceContext** a_immediateContext)
	{
		const bool complete =
			SUCCEEDED(a_result)
			&& a_swapChainDesc
			&& a_swapChainDesc->OutputWindow
			&& a_swapChain
			&& *a_swapChain
			&& a_device
			&& *a_device
			&& a_immediateContext
			&& *a_immediateContext;
		bool expected = false;
		if (complete && ready.compare_exchange_strong(expected, true)) {
			InvokeOwner("Shader cache initialization", [] {
				InitializeShaderCache();
			});
			util::ShaderCompilationBatch shaderCompilationBatch;
			// The engine b12 snapshot must be live before any feature reads a camera.
			InvokeOwner("FrameBuffer snapshot hooks", [&] {
				engine::OnFrameBufferD3D11Ready(*a_immediateContext);
			});
			// Register injections before the registry freezes.
			InvokeOwner("PixelShaderSwapBroker D3D11 readiness", [&] {
				engine::SetPixelShaderSwapBrokerDevice(*a_device);
			});
			InvokeOwner("FeatureManager D3D11 readiness", [&] {
				FeatureManager::Get().OnD3D11ReadyAll(a_adapter, *a_device);
			});
			// substrate must precede injected shader compilation
			InvokeOwner("SharedData D3D11 initialization", [&] {
				render::InitializeSharedData(*a_device, *a_immediateContext);
			});
			InvokeOwner("ShaderInjection freeze and compile", [&] {
				engine::FreezeAndCompileShaderInjections(*a_device);
			});
			// features must see the frozen delivery path before the first frame
			InvokeOwner("FeatureManager shader-injection validation", [&] {
				FeatureManager::Get().ValidateShaderInjectionsAll();
			});
			// Defer menu ownership to a registered host.
			bool standalone = true;
			InvokeOwner("Dear-Modding UI bootstrap", [&] {
				standalone = host::HostClient::Get().OnD3D11Bootstrap(
					*a_device,
					*a_immediateContext,
					*a_swapChain,
					a_swapChainDesc->OutputWindow);
			});
			if (standalone) {
				InvokeOwner("Menu D3D11 initialization", [&] {
					Menu::Get().OnD3D11Ready(*a_device, *a_immediateContext, a_swapChainDesc->OutputWindow);
				});
				InvokeOwner("Menu Present hook", [&] {
					Menu::Get().HookPresentOn(*a_swapChain);
				});
			}
			InvokeOwner("Shader cache startup summary", [] {
				LogShaderCacheSummary();
			});
		}
	}
}
