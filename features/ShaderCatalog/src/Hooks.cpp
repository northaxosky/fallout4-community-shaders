#include "Hooks.h"

#include "CatalogDB.h"
#include "Log.h"
#include "PixelShaderTracker.h"
#include "Sha1.h"
#include "ShaderCatalogSuppression.h"
#include "SubclassContext.h"

#include <atomic>

namespace cs::features::catalog::hooks
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.catalog");

		std::atomic<bool> g_psSetShaderHookInstalled{ false };
		std::atomic<std::uint64_t> g_scopedBinds{ 0 };
		std::atomic<std::uint64_t> g_matchedBinds{ 0 };
		std::atomic<std::uint64_t> g_missedBinds{ 0 };

		// Published once (release) from OnD3D11Ready; hot path acquire-loads it, publishing compiled replacements to shader-creation threads.
		std::atomic<ShaderCatalog::PixelShaderSwapCallback> g_psSwapCallback{ nullptr };

		// Hot path for every stage: SHA1, stack capture, enqueue; cost is mostly bytecode hashing.
		__forceinline void RecordEntry(char stage, const void* bytecode, SIZE_T len) noexcept
		{
			if (RecordingSuppressed())
				return;

			// Skip invalid creation attempts D3D would reject; avoids hashing null/empty bytecode.
			if (!bytecode || len == 0)
				return;

			CatalogEntry e{};
			e.stage = stage;
			e.bytecode_size = len;
			e.source_va = reinterpret_cast<std::uintptr_t>(bytecode);
			e.thread_id = ::GetCurrentThreadId();
			LARGE_INTEGER c;
			QueryPerformanceCounter(&c);
			e.timestamp_qpc = c.QuadPart;

			// SHA1 over the raw bytecode; deterministic across runtimes/processes.
			const auto hash = Sha1Compute(bytecode, len);
			e.sha1_bytes = hash.bytes;

			// Cheap stack capture (RVAs only); symbolication is deferred to the writer.
			void* frames[4] = {};
			CaptureStackBackTrace(2, 4, frames, nullptr);
			for (int i = 0; i < 4; ++i) e.stack_frames[i] = frames[i];

			const auto& ctx = context::g_ctx;
			if (ctx.active && ctx.subclass_name) {
				e.subclass_name      = ctx.subclass_name;
				e.has_subclass       = true;
				e.technique_bits     = ctx.technique_bits;
				e.has_technique_bits = (ctx.technique_bits != 0);
			}

			CatalogDB::Get().EnqueueShader(e);
		}
	}

	HRESULT STDMETHODCALLTYPE CreateVertexShaderHook::thunk(
		ID3D11Device* a_this, const void* a_bytecode, SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage, ID3D11VertexShader** a_out)
	{
		RecordEntry('v', a_bytecode, a_bytecode_len);
		return func(a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
	}

	HRESULT STDMETHODCALLTYPE CreatePixelShaderHook::thunk(
		ID3D11Device* a_this, const void* a_bytecode, SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage, ID3D11PixelShader** a_out)
	{
		if (!RecordingSuppressed())
			RecordEntry('p', a_bytecode, a_bytecode_len);

		const HRESULT hr = func(a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
		if (!RecordingSuppressed() && SUCCEEDED(hr) && a_out && *a_out && a_bytecode && a_bytecode_len != 0) {
			const auto hash = Sha1Compute(a_bytecode, a_bytecode_len);
			shader_tracker::TrackPixelShader(*a_out, hash);

			// Broker: offer the created shader to the optional consumer (ShaderReplacement).
			if (const auto swapCb = g_psSwapCallback.load(std::memory_order_acquire)) {
				swapCb(a_bytecode, a_bytecode_len, hash, a_out);
				// If it swapped *a_out, re-track the replacement under the same SHA1 for attribution.
				if (*a_out)
					shader_tracker::TrackPixelShader(*a_out, hash, true);
			}
		}
		return hr;
	}

	void STDMETHODCALLTYPE PSSetShaderHook::thunk(
		ID3D11DeviceContext*       a_this,
		ID3D11PixelShader*         a_shader,
		ID3D11ClassInstance* const* a_classInstances,
		UINT                       a_numClassInstances)
	{
		func(a_this, a_shader, a_classInstances, a_numClassInstances);

		const auto ctx = context::CurrentOrSticky();
		if (!ctx.active || !ctx.subclass_name)
			return;
		if (!a_shader)
			return;

		g_scopedBinds.fetch_add(1, std::memory_order_relaxed);
		Sha1Result sha{};
		if (shader_tracker::TryGetPixelShaderSha1(a_shader, sha)) {
			g_matchedBinds.fetch_add(1, std::memory_order_relaxed);
			CatalogDB::Get().EnqueueAttribution(sha, ctx.subclass_name, ctx.technique_bits);
		} else {
			const auto miss = g_missedBinds.fetch_add(1, std::memory_order_relaxed);
			if (miss < 8)
				L->debug("PSSetShader in {} scope missed pixel-shader tracker", ctx.subclass_name);
		}
		context::ClearSticky();
	}

	HRESULT STDMETHODCALLTYPE CreateGeometryShaderHook::thunk(
		ID3D11Device* a_this, const void* a_bytecode, SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage, ID3D11GeometryShader** a_out)
	{
		RecordEntry('g', a_bytecode, a_bytecode_len);
		return func(a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
	}

	HRESULT STDMETHODCALLTYPE CreateComputeShaderHook::thunk(
		ID3D11Device* a_this, const void* a_bytecode, SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage, ID3D11ComputeShader** a_out)
	{
		RecordEntry('c', a_bytecode, a_bytecode_len);
		return func(a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
	}

	HRESULT STDMETHODCALLTYPE CreateHullShaderHook::thunk(
		ID3D11Device* a_this, const void* a_bytecode, SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage, ID3D11HullShader** a_out)
	{
		RecordEntry('h', a_bytecode, a_bytecode_len);
		return func(a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
	}

	HRESULT STDMETHODCALLTYPE CreateDomainShaderHook::thunk(
		ID3D11Device* a_this, const void* a_bytecode, SIZE_T a_bytecode_len,
		ID3D11ClassLinkage* a_linkage, ID3D11DomainShader** a_out)
	{
		RecordEntry('d', a_bytecode, a_bytecode_len);
		return func(a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
	}

	void InstallAll(ID3D11Device* a_device)
	{
		// D3D11 device vtable slots from d3d11-device-vtable-map.md; identical across OG/NG/AE.
		stl::detour_vfunc<12, CreateVertexShaderHook  >(a_device);
		stl::detour_vfunc<13, CreateGeometryShaderHook>(a_device);
		stl::detour_vfunc<15, CreatePixelShaderHook   >(a_device);
		stl::detour_vfunc<16, CreateHullShaderHook    >(a_device);
		stl::detour_vfunc<17, CreateDomainShaderHook  >(a_device);
		stl::detour_vfunc<18, CreateComputeShaderHook >(a_device);

		ID3D11DeviceContext* context = nullptr;
		a_device->GetImmediateContext(&context);
		if (context) {
			stl::detour_vfunc<9, PSSetShaderHook>(context);
			context->Release();
			g_psSetShaderHookInstalled.store(true, std::memory_order_release);
		}
	}

	void SetPixelShaderSwapCallback(ShaderCatalog::PixelShaderSwapCallback a_cb) noexcept
	{
		g_psSwapCallback.store(a_cb, std::memory_order_release);
	}

	RuntimeAttributionStats GetRuntimeAttributionStats()
	{
		return RuntimeAttributionStats{
			g_psSetShaderHookInstalled.load(std::memory_order_acquire),
			g_scopedBinds.load(std::memory_order_relaxed),
			g_matchedBinds.load(std::memory_order_relaxed),
			g_missedBinds.load(std::memory_order_relaxed)
		};
	}
}
