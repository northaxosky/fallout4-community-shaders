#pragma once

#include "Utils/CSSha256.h"

#include <cstdint>
#include <filesystem>

namespace cs::shader_cache
{
	// Identity of the d3dcompiler module that will actually service D3DCompile in this process.
	struct CompilerIdentity
	{
		bool                  established = false;
		std::filesystem::path modulePath;
		std::uint64_t         moduleLength = 0;
		sha256::Sha256Result  moduleDigest{};
	};

	const CompilerIdentity& GetD3DCompilerIdentity() noexcept;
	CompilerIdentity        ResolveD3DCompilerIdentity() noexcept;
}
