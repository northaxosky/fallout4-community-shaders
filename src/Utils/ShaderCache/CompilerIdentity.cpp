#include "Utils/ShaderCache/CompilerIdentity.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <mutex>
#include <string>
#include <system_error>

// The IAT slot the loader filled with the real entry point; &D3DCompile would only name our own thunk.
extern "C" void* __imp_D3DCompile;

namespace cs::shader_cache
{
	namespace
	{
		bool ResolveModulePath(HMODULE a_module, std::wstring& a_path)
		{
			std::wstring buffer(MAX_PATH, L'\0');
			for (;;) {
				const DWORD written = GetModuleFileNameW(
					a_module,
					buffer.data(),
					static_cast<DWORD>(buffer.size()));
				if (written == 0)
					return false;
				if (written < buffer.size()) {
					buffer.resize(written);
					a_path = std::move(buffer);
					return true;
				}
				if (buffer.size() >= 32768)
					return false;
				buffer.resize(buffer.size() * 2);
			}
		}
	}

	CompilerIdentity ResolveD3DCompilerIdentity() noexcept
	{
		CompilerIdentity identity;
		try {
			const auto* address = static_cast<const void*>(__imp_D3DCompile);
			if (!address)
				return identity;

			HMODULE module = nullptr;
			if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
						| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(address),
					&module)
				|| !module) {
				return identity;
			}

			std::wstring modulePath;
			if (!ResolveModulePath(module, modulePath))
				return identity;

			std::error_code       error;
			std::filesystem::path canonical =
				std::filesystem::weakly_canonical(modulePath, error);
			if (error || canonical.empty())
				canonical = modulePath;

			sha256::Sha256Result digest{};
			std::uint64_t        length = 0;
			if (!sha256::Sha256ComputeFile(canonical, digest, length)
				|| sha256::Sha256IsZero(digest)) {
				return identity;
			}

			identity.modulePath   = std::move(canonical);
			identity.moduleLength = length;
			identity.moduleDigest = digest;
			identity.established  = true;
		} catch (...) {
			return {};
		}
		return identity;
	}

	const CompilerIdentity& GetD3DCompilerIdentity() noexcept
	{
		static const CompilerIdentity identity = ResolveD3DCompilerIdentity();
		return identity;
	}
}
