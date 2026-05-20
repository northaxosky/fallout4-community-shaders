#include "Hooks.h"

#include "Log.h"
#include "Registry.h"
#include "Sha1.h"

#include <atomic>

namespace cs::features::replacement::hooks
{
	namespace { auto* L = cs::log::Get("cs.feature.shaderreplacement"); }

	HRESULT STDMETHODCALLTYPE CreatePixelShaderReplaceHook::thunk(
		ID3D11Device*       a_this,
		const void*         a_bytecode,
		SIZE_T              a_bytecode_len,
		ID3D11ClassLinkage* a_linkage,
		ID3D11PixelShader** a_out)
	{
		HRESULT hr = func(a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
		if (FAILED(hr) || !a_out || !*a_out || !a_bytecode || a_bytecode_len == 0)
			return hr;

		const auto sha = Sha1Compute(a_bytecode, a_bytecode_len);
		auto* entry = Registry::Get().FindByRuntimeSha1(sha);
		if (!entry)
			return hr;

		entry->match_hits.fetch_add(1, std::memory_order_relaxed);

		if (!entry->enabled_in_ini) {
			entry->passthrough_disabled.fetch_add(1, std::memory_order_relaxed);
			return hr;
		}
		if (!entry->compile_ok || !entry->compiled_ps) {
			entry->passthrough_compile_fail.fetch_add(1, std::memory_order_relaxed);
			return hr;
		}

		// Swap *a_out with the pre-compiled replacement. Net refcount of the engine-bound
		// shader must end at exactly 1: AddRef our replacement, Release the engine's.
		ID3D11PixelShader* mine = entry->compiled_ps.get();
		mine->AddRef();
		(*a_out)->Release();
		*a_out = mine;

		const auto prev = entry->substitution_hits.fetch_add(1, std::memory_order_relaxed);
		if (prev == 0) {
			L->info("Replaced PS sha={} -> {}", Sha1ToHex(sha), entry->name);
		}
		return hr;
	}

	void Install(ID3D11Device* a_device)
	{
		// Slot 15 = CreatePixelShader on the D3D11 device vtable (matches ShaderCatalog).
		stl::detour_vfunc<15, CreatePixelShaderReplaceHook>(a_device);
	}
}
