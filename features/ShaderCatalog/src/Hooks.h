#pragma once

#include <d3d11.h>

#include "PCH.h"

namespace cs::features::catalog::hooks
{
	struct CreateVertexShaderHook
	{
		static HRESULT STDMETHODCALLTYPE thunk(
			ID3D11Device*        a_this,
			const void*          a_bytecode,
			SIZE_T               a_bytecode_len,
			ID3D11ClassLinkage*  a_linkage,
			ID3D11VertexShader** a_out);
		static inline HRESULT (STDMETHODCALLTYPE *func)(
			ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**) = nullptr;
	};

	struct CreatePixelShaderHook
	{
		static HRESULT STDMETHODCALLTYPE thunk(
			ID3D11Device*        a_this,
			const void*          a_bytecode,
			SIZE_T               a_bytecode_len,
			ID3D11ClassLinkage*  a_linkage,
			ID3D11PixelShader**  a_out);
		static inline HRESULT (STDMETHODCALLTYPE *func)(
			ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**) = nullptr;
	};

	struct CreateGeometryShaderHook
	{
		static HRESULT STDMETHODCALLTYPE thunk(
			ID3D11Device*          a_this,
			const void*            a_bytecode,
			SIZE_T                 a_bytecode_len,
			ID3D11ClassLinkage*    a_linkage,
			ID3D11GeometryShader** a_out);
		static inline HRESULT (STDMETHODCALLTYPE *func)(
			ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11GeometryShader**) = nullptr;
	};

	struct CreateComputeShaderHook
	{
		static HRESULT STDMETHODCALLTYPE thunk(
			ID3D11Device*         a_this,
			const void*           a_bytecode,
			SIZE_T                a_bytecode_len,
			ID3D11ClassLinkage*   a_linkage,
			ID3D11ComputeShader** a_out);
		static inline HRESULT (STDMETHODCALLTYPE *func)(
			ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**) = nullptr;
	};

	struct CreateHullShaderHook
	{
		static HRESULT STDMETHODCALLTYPE thunk(
			ID3D11Device*      a_this,
			const void*        a_bytecode,
			SIZE_T             a_bytecode_len,
			ID3D11ClassLinkage* a_linkage,
			ID3D11HullShader** a_out);
		static inline HRESULT (STDMETHODCALLTYPE *func)(
			ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11HullShader**) = nullptr;
	};

	struct CreateDomainShaderHook
	{
		static HRESULT STDMETHODCALLTYPE thunk(
			ID3D11Device*        a_this,
			const void*          a_bytecode,
			SIZE_T               a_bytecode_len,
			ID3D11ClassLinkage*  a_linkage,
			ID3D11DomainShader** a_out);
		static inline HRESULT (STDMETHODCALLTYPE *func)(
			ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11DomainShader**) = nullptr;
	};

	// Installs all five vtable detours on the supplied device. Idempotent in practice (each
	// detour_vfunc replaces the slot and stashes the prior pointer, so calling it twice would
	// re-chain; caller guards against double-install via _hooksInstalled).
	void InstallAll(ID3D11Device* a_device);
}
