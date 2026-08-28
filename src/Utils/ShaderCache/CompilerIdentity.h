#pragma once

#include "Utils/CSSha256.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace cs::shader_cache
{
	enum class CompilerIdentityMechanism : std::uint8_t
	{
		kUnavailable,
		kVersionInfo,
		kContentHash
	};

	struct CompilerFileVersion
	{
		std::uint16_t major = 0;
		std::uint16_t minor = 0;
		std::uint16_t build = 0;
		std::uint16_t revision = 0;
	};

	struct CompilerIdentity
	{
		bool                  established = false;
		std::filesystem::path modulePath;
		std::uint64_t         moduleLength = 0;
		sha256::Sha256Result  moduleDigest{};
		CompilerIdentityMechanism mechanism =
			CompilerIdentityMechanism::kUnavailable;
		CompilerFileVersion fileVersion;
	};

	const char* DescribeCompilerIdentityMechanism(
		CompilerIdentityMechanism a_mechanism) noexcept;
	std::string DescribeCompilerIdentityValue(
		const CompilerIdentity& a_identity);
	std::string DescribeCompilerIdentity(const CompilerIdentity& a_identity);

	CompilerIdentity MakeVersionCompilerIdentity(
		std::filesystem::path a_modulePath,
		std::uint64_t         a_moduleLength,
		CompilerFileVersion   a_version);
	CompilerIdentity ResolveCompilerIdentity(
		const std::filesystem::path& a_modulePath) noexcept;
	const CompilerIdentity& GetD3DCompilerIdentity() noexcept;
	CompilerIdentity        ResolveD3DCompilerIdentity() noexcept;
}
