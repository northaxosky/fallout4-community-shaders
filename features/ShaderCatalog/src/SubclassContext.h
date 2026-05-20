#pragma once

#include <cstdint>

namespace cs::features::catalog::context
{
	// Thread-local "who is loading shaders right now" context, pushed by per-subclass
	// ReloadShaders hooks and read by the device-vtable CreatePixelShader hook.
	struct Context
	{
		const char*   subclass_name  = nullptr;  // string literal; process-lifetime
		std::uint32_t technique_bits = 0;
		bool          active         = false;
	};

	extern thread_local Context g_ctx;

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
