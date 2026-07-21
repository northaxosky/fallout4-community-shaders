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

	struct PSSetShaderHook
	{
		static void STDMETHODCALLTYPE thunk(
			ID3D11DeviceContext*       a_this,
			ID3D11PixelShader*         a_shader,
			ID3D11ClassInstance* const* a_classInstances,
			UINT                       a_numClassInstances);
		static inline void (STDMETHODCALLTYPE *func)(
			ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT) = nullptr;
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

	// Install D3D11 vtable detours once; repeat calls would re-chain prior thunks.
	void InstallAll(ID3D11Device* a_device);

	struct RuntimeAttributionStats
	{
		bool psSetShaderHookInstalled = false;
		std::uint64_t scopedBinds = 0;
		std::uint64_t matchedBinds = 0;
		std::uint64_t missedBinds = 0;
	};
	RuntimeAttributionStats GetRuntimeAttributionStats();
}
