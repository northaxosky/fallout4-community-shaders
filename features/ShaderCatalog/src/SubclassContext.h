#pragma once

#include <cstdint>

namespace cs::features::catalog::context
{
	// Thread-local shader-loading context shared by subclass hooks and CreatePixelShader.
	struct Context
	{
		const char*   subclass_name  = nullptr;  // string literal; process-lifetime
		std::uint32_t technique_bits = 0;
		bool          active         = false;
	};

	extern thread_local Context g_ctx;
	extern thread_local Context g_stickyCtx;

	inline void SetSticky(const char* a_name, std::uint32_t a_techniqueBits) noexcept
	{
		g_stickyCtx.subclass_name  = a_name;
		g_stickyCtx.technique_bits = a_techniqueBits;
		g_stickyCtx.active         = (a_name != nullptr);
	}

	inline void ClearSticky() noexcept
	{
		g_stickyCtx = {};
	}

	inline Context CurrentOrSticky() noexcept
	{
		return g_ctx.active ? g_ctx : g_stickyCtx;
	}

	struct Scope
	{
		Context prev;

		Scope(const char* a_name, std::uint32_t a_techniqueBits) noexcept
		{
			prev = g_ctx;
			g_ctx.subclass_name  = a_name;
			g_ctx.technique_bits = a_techniqueBits;
			g_ctx.active         = (a_name != nullptr);
		}

		~Scope() noexcept
		{
			g_ctx = prev;
		}

		Scope(const Scope&)            = delete;
		Scope& operator=(const Scope&) = delete;
	};
}
