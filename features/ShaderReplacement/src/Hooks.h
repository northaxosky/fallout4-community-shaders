#pragma once

#include <d3d11.h>

#include "PCH.h"

namespace cs::features::replacement::hooks
{
	// Second-stage CreatePixelShader vtable detour. Installed AFTER ShaderCatalog's so the
	// chain is: engine -> our thunk -> ShaderCatalog's thunk -> original. After the original
	// writes *a_out we sha1 the bytecode, look it up in Registry, and swap *a_out for our
	// pre-compiled replacement when matched + enabled + compile-ok.
	struct CreatePixelShaderReplaceHook
	{
		static HRESULT STDMETHODCALLTYPE thunk(
			ID3D11Device*       a_this,
			const void*         a_bytecode,
			SIZE_T              a_bytecode_len,
			ID3D11ClassLinkage* a_linkage,
			ID3D11PixelShader** a_out);
		static inline HRESULT (STDMETHODCALLTYPE *func)(
			ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**) = nullptr;
	};

	void Install(ID3D11Device* a_device);
}
