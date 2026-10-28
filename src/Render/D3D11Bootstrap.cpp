#include "Render/D3D11Bootstrap.h"

#include <atomic>
#include <exception>
#include <string>
#include <string_view>

#include "Feature.h"
#include "Host/HostClient.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Render/Annotation.h"
#include "Render/FrameProfiler.h"
#include "Render/ShaderCompileGate.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Render/TemporalPipeline.h"
#include "Settings/FeatureConfig.h"
#include "Settings/SettingsRegistry.h"
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

		// Resources report the wrapped device, so a proxy shows as a mismatch.
		void LogDeviceIdentity(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
		{
			const D3D11_BUFFER_DESC desc{ 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
			winrt::com_ptr<ID3D11Buffer> probe;
			winrt::com_ptr<ID3D11Device> resourceDevice;
			if (SUCCEEDED(a_device->CreateBuffer(&desc, nullptr, probe.put())))
				probe->GetDevice(resourceDevice.put());
			winrt::com_ptr<ID3D11Device> contextDevice;
			a_context->GetDevice(contextDevice.put());
			L->info(
				"D3D11 device identity: created={}, context={}, resources={}{}",
				static_cast<const void*>(a_device),
				static_cast<const void*>(contextDevice.get()),
				static_cast<const void*>(resourceDevice.get()),
				resourceDevice && resourceDevice.get() != a_device ? " (device is a proxy over the resource device)" : "");
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
			// Wine's builtin compiler cannot parse the feature HLSL.
			auto* ntdll = GetModuleHandleW(L"ntdll.dll");
			if (ntdll && GetProcAddress(ntdll, "wine_get_version") && identity.fileVersion.major < 10) {
				CacheL->warn(
					"Wine/Proton detected with an old or builtin d3dcompiler_47 ({}); feature shaders will fail to compile. "
					"Place a Windows 10 d3dcompiler_47.dll in the prefix's system32 or next to Fallout4.exe.",
					shader_cache::DescribeCompilerIdentity(identity));
			}

			if (identity.established) {
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
			std::string error;
			const auto cacheSettings = settings::ParseCoreSection(
				settings::core::kShaderCache, feature_config::GetRoot(), "shader_cache", error);
			if (!error.empty())
				CacheL->warn("{}; using the default", error);
			const auto cap = static_cast<std::uint64_t>(cacheSettings.maxSizeMb) * 1024 * 1024;
			shader_cache::SetShaderCacheMaxBytes(cap);
			const auto usage = shader_cache::TrimCacheRecords(root, cap);
			CacheL->info("shader cache storage: {} records, {} bytes, {} trimmed",
				usage.records, usage.bytes, usage.trimmed);
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
			SUCCEEDED(a_result) && a_swapChainDesc && a_swapChainDesc->OutputWindow && a_swapChain && *a_swapChain && a_device && *a_device && a_immediateContext && *a_immediateContext;
		if (!complete)
			return;

		bool expected = false;
		if (!ready.compare_exchange_strong(expected, true))
			return;
		{
			InvokeOwner("GPU adapter diagnostics", [&] {
				L->info("GPU adapter: {}", util::AdapterDescription(*a_device));
			});
			InvokeOwner("D3D11 device identity diagnostics", [&] {
				LogDeviceIdentity(*a_device, *a_immediateContext);
			});
			render::annotation::Initialize(*a_immediateContext);
			render::profiling::InitializeD3D11(*a_device, *a_immediateContext);
			InvokeOwner("Shader cache initialization", [] {
				InitializeShaderCache();
			});
			engine::InstallShaderCompileSummaryLog();
			util::ShaderCompilationBatch shaderCompilationBatch;
			InvokeOwner("RunComputeShader dispatch bridge", [&] {
				engine::EnsureComputeDispatchBridgeInstalled(
					*a_immediateContext);
			});
			// Register injections before the registry freezes.
			InvokeOwner("Native shader creation hooks", [&] {
				engine::InstallNativeShaderCreationHooks(*a_device);
			});
			InvokeOwner("Temporal pipeline D3D11 readiness", [&] {
				render::TemporalPipeline::Get().OnD3D11Ready(a_adapter, *a_device);
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
			InvokeOwner("Shader variant prewarm", [] {
				engine::PrewarmShaderInjectionVariants();
			});
			InvokeOwner("Debug-view selection", [] {
				Menu::Get().ApplyDebugViewSelections();
			});
			InvokeOwner("Dear-Modding UI bootstrap", [&] {
				host::HostClient::Get().OnD3D11Bootstrap(
					*a_device,
					*a_swapChain,
					a_swapChainDesc->OutputWindow);
			});
			InvokeOwner("Shader cache startup summary", [] {
				LogShaderCacheSummary();
			});
		}
	}
}
