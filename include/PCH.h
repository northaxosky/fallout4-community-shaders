#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#pragma warning(push)
#include "F4SE/F4SE.h"
#include "RE/Fallout.h"
#include "REX/REX.h"
#pragma warning(pop)

#include "Windows.h"

#ifdef TRACY_SUPPORT
#include <tracy/Tracy.hpp>
#include <tracy/TracyD3D11.hpp>
#else
using TracyD3D11Ctx = void*;
#define ZoneScoped ((void)0)
#define ZoneScopedN(x) ((void)0)
#define ZoneTransientN(name, str, active) ((void)0)
#define FrameMark ((void)0)
#define TracyPlot(name, val) ((void)0)
#define TracyMessage(txt, sz) ((void)0)
#define TracyD3D11Context(d, c) nullptr
#define TracyD3D11Destroy(ctx) ((void)0)
#define TracyD3D11Zone(ctx, name) ((void)0)
#define TracyD3D11ZoneTransientS(ctx, var, name, depth, active) ((void)0)
#define TracyD3D11Collect(ctx) ((void)0)
#endif

#undef DEBUG
#undef ERROR

#include <string>
using namespace std::literals;

#include "detours/detours.h"

#include "SimpleMath.h"

using float2 = DirectX::SimpleMath::Vector2;
using float3 = DirectX::SimpleMath::Vector3;
using float4 = DirectX::SimpleMath::Vector4;
using float4x4 = DirectX::SimpleMath::Matrix;
using uint = uint32_t;

#include <directx/d3dx12.h>

#include <magic_enum/magic_enum.hpp>

#define DLLEXPORT __declspec(dllexport)

namespace stl
{
	template <class T>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = REL::GetTrampoline();
		T::func = trampoline.write_call<5>(a_src, T::thunk);
	}

	template <class F, size_t index, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[index] };
		T::func = vtbl.write_vfunc(T::size, T::thunk);
	}

	template <class F, class T>
	void write_vfunc()
	{
		write_vfunc<F, 0, T>();
	}

	template <std::size_t idx, class T>
	void write_vfunc(REL::ID id)
	{
		REL::Relocation<std::uintptr_t> vtbl{ id };
		T::func = vtbl.write_vfunc(idx, T::thunk);
	}

	template <class T>
	void detour_thunk(REL::ID a_relId)
	{
		*(uintptr_t*)&T::func = Detours::X64::DetourFunction(a_relId.address(), (uintptr_t)&T::thunk);
	}

	template <class T>
	void detour_thunk_ignore_func(REL::ID a_relId)
	{
		std::ignore = Detours::X64::DetourFunction(a_relId.address(), (uintptr_t)&T::thunk);
	}

	template <std::size_t idx, class T>
	void detour_vfunc(void* target)
	{
		*(uintptr_t*)&T::func = Detours::X64::DetourClassVTable(*(uintptr_t*)target, &T::thunk, idx);
	}
}

namespace DX
{
	class com_exception : public std::exception
	{
	public:
		explicit com_exception(HRESULT hr) noexcept :
			result(hr) {}

		const char* what() const override
		{
			static char s_str[64] = {};
			sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(result));
			return s_str;
		}

	private:
		HRESULT result;
	};

	inline void ThrowIfFailed(HRESULT hr)
	{
		if (FAILED(hr)) {
			throw com_exception(hr);
		}
	}
}

#include "Plugin.h"
