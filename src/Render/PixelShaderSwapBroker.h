#pragma once

#include "Utils/CSSha1.h"

#include <cstddef>

struct ID3D11Device;
struct ID3D11PixelShader;

namespace cs::engine
{
	// A successful resolver replaces *a_out with net refcount one.
	using PixelShaderSwapResolver = bool (*)(const void* a_bytecode, std::size_t a_bytecodeLength,
		const sha1::Sha1Result& a_sha, ID3D11PixelShader** a_out) noexcept;
	using PixelShaderBytecodeObserver = void (*)(const void* a_bytecode, std::size_t a_bytecodeLength) noexcept;
	using OriginalPixelShaderObserver = void (*)(const sha1::Sha1Result& a_sha, ID3D11PixelShader* a_shader) noexcept;
	using ResolvedPixelShaderObserver = void (*)(const sha1::Sha1Result& a_sha, ID3D11PixelShader* a_shader) noexcept;

	struct PixelShaderSwapObserver
	{
		PixelShaderBytecodeObserver observeBytecode = nullptr;
		OriginalPixelShaderObserver observeOriginal = nullptr;
		ResolvedPixelShaderObserver observeResolved = nullptr;
	};

	void SetPixelShaderSwapBrokerDevice(ID3D11Device* a_device);
	bool RegisterPixelShaderSwapResolver(PixelShaderSwapResolver a_resolver);
	bool RegisterPixelShaderSwapObserver(PixelShaderSwapObserver a_observer);
	bool PixelShaderSwapBrokerHooksInstalled() noexcept;

	class ScopedPixelShaderBrokerBypass
	{
	public:
		ScopedPixelShaderBrokerBypass() noexcept;
		~ScopedPixelShaderBrokerBypass() noexcept;

		ScopedPixelShaderBrokerBypass(const ScopedPixelShaderBrokerBypass&) = delete;
		ScopedPixelShaderBrokerBypass(ScopedPixelShaderBrokerBypass&&) = delete;
		ScopedPixelShaderBrokerBypass& operator=(const ScopedPixelShaderBrokerBypass&) = delete;
		ScopedPixelShaderBrokerBypass& operator=(ScopedPixelShaderBrokerBypass&&) = delete;
	};
}
