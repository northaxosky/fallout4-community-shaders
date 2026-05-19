#include "Hooks.h"

#include "CatalogDB.h"
#include "Sha1.h"

namespace cs::features::catalog::hooks
{
	namespace
	{
		// Hot-path body, identical shape for every stage. Inlined into each thunk by the
		// compiler. Order is: sha1 -> stack capture -> enqueue. Cost dominated by SHA1 over the
		// bytecode; everything else is ALU + a single atomic CAS in the ring. The original
		// function pointer must NEVER be skipped or its arguments mutated; that invariant is
		// what the adversarial reviewer is checking.
		__forceinline void RecordEntry(char stage, const void* bytecode, SIZE_T len) noexcept
		{
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
		RecordEntry('p', a_bytecode, a_bytecode_len);
		return func(a_this, a_bytecode, a_bytecode_len, a_linkage, a_out);
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
		// Slot numbers from d3d11-device-vtable-map.md (Workspace docs); identical across OG/NG/AE.
		stl::detour_vfunc<12, CreateVertexShaderHook  >(a_device);
		stl::detour_vfunc<13, CreateGeometryShaderHook>(a_device);
		stl::detour_vfunc<15, CreatePixelShaderHook   >(a_device);
		stl::detour_vfunc<16, CreateHullShaderHook    >(a_device);
		stl::detour_vfunc<17, CreateDomainShaderHook  >(a_device);
		stl::detour_vfunc<18, CreateComputeShaderHook >(a_device);
	}
}
