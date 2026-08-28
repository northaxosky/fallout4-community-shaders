#include "Utils/ShaderCache/CompilerIdentity.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// IAT slot, not &D3DCompile; that names our own thunk
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

		bool ReadFileVersion(
			const std::filesystem::path& a_path,
			CompilerFileVersion&         a_version)
		{
			DWORD ignored = 0;
			const DWORD size =
				GetFileVersionInfoSizeW(a_path.c_str(), &ignored);
			if (size == 0)
				return false;

			std::vector<std::uint8_t> bytes(size);
			if (!GetFileVersionInfoW(
					a_path.c_str(),
					0,
					size,
					bytes.data())) {
				return false;
			}

			void* value = nullptr;
			UINT  valueSize = 0;
			if (!VerQueryValueW(
					bytes.data(),
					L"\\",
					&value,
					&valueSize)
				|| !value
				|| valueSize < sizeof(VS_FIXEDFILEINFO)) {
				return false;
			}

			const auto& fixed = *static_cast<const VS_FIXEDFILEINFO*>(value);
			if (fixed.dwSignature != 0xFEEF04BD)
				return false;

			DWORD versionMs = fixed.dwFileVersionMS;
			DWORD versionLs = fixed.dwFileVersionLS;
			const bool manifestShimmed =
				HIWORD(versionMs) == 6
				&& LOWORD(versionMs) <= 3
				&& HIWORD(fixed.dwProductVersionMS) >= 10
				&& versionLs == fixed.dwProductVersionLS;
			// System DLL file versions may be manifest-shimmed.
			if (manifestShimmed) {
				versionMs = fixed.dwProductVersionMS;
				versionLs = fixed.dwProductVersionLS;
			}

			a_version = {
				static_cast<std::uint16_t>(HIWORD(versionMs)),
				static_cast<std::uint16_t>(LOWORD(versionMs)),
				static_cast<std::uint16_t>(HIWORD(versionLs)),
				static_cast<std::uint16_t>(LOWORD(versionLs))
			};
			return true;
		}

		std::string PathToUtf8(const std::filesystem::path& a_path)
		{
			const auto encoded = a_path.u8string();
			std::string result;
			result.reserve(encoded.size());
			for (const char8_t character : encoded)
				result.push_back(static_cast<char>(character));
			return result;
		}

		std::string FormatVersion(CompilerFileVersion a_version)
		{
			char buffer[64]{};
			std::snprintf(
				buffer,
				std::size(buffer),
				"%u.%u.%u.%u",
				static_cast<unsigned>(a_version.major),
				static_cast<unsigned>(a_version.minor),
				static_cast<unsigned>(a_version.build),
				static_cast<unsigned>(a_version.revision));
			return buffer;
		}
	}

	const char* DescribeCompilerIdentityMechanism(
		CompilerIdentityMechanism a_mechanism) noexcept
	{
		switch (a_mechanism) {
		case CompilerIdentityMechanism::kUnavailable:
			return "unavailable";
		case CompilerIdentityMechanism::kVersionInfo:
			return "version info";
		case CompilerIdentityMechanism::kContentHash:
			return "content hash fallback";
		}
		return "unknown";
	}

	std::string DescribeCompilerIdentityValue(
		const CompilerIdentity& a_identity)
	{
		if (!a_identity.established)
			return "unavailable";
		if (a_identity.mechanism == CompilerIdentityMechanism::kVersionInfo)
			return FormatVersion(a_identity.fileVersion);
		if (a_identity.mechanism == CompilerIdentityMechanism::kContentHash)
			return "sha256:" + sha256::Sha256ToHex(a_identity.moduleDigest);
		return "unavailable";
	}

	std::string DescribeCompilerIdentity(const CompilerIdentity& a_identity)
	{
		if (!a_identity.established)
			return "unavailable";
		return PathToUtf8(a_identity.modulePath.filename()) + " "
			+ DescribeCompilerIdentityValue(a_identity);
	}

	CompilerIdentity MakeVersionCompilerIdentity(
		std::filesystem::path a_modulePath,
		std::uint64_t         a_moduleLength,
		CompilerFileVersion   a_version)
	{
		CompilerIdentity identity;
		if (a_modulePath.empty() || a_moduleLength == 0)
			return identity;

		const auto version = FormatVersion(a_version);
		const auto material =
			std::string("FO4CS.compiler-version.v1|") + version;
		const auto digest =
			sha256::Sha256Compute(material.data(), material.size());
		if (sha256::Sha256IsZero(digest))
			return identity;

		identity.established = true;
		identity.modulePath = std::move(a_modulePath);
		identity.moduleLength = a_moduleLength;
		identity.moduleDigest = digest;
		identity.mechanism = CompilerIdentityMechanism::kVersionInfo;
		identity.fileVersion = a_version;
		return identity;
	}

	CompilerIdentity ResolveCompilerIdentity(
		const std::filesystem::path& a_modulePath) noexcept
	{
		try {
			std::error_code error;
			auto canonical =
				std::filesystem::weakly_canonical(a_modulePath, error);
			if (error || canonical.empty())
				canonical = a_modulePath;

			const auto length = std::filesystem::file_size(canonical, error);
			if (!error) {
				CompilerFileVersion version;
				if (ReadFileVersion(canonical, version)) {
					return MakeVersionCompilerIdentity(
						std::move(canonical),
						length,
						version);
				}
			}

			sha256::Sha256Result digest{};
			std::uint64_t        hashedLength = 0;
			if (!sha256::Sha256ComputeFile(
					canonical,
					digest,
					hashedLength)
				|| sha256::Sha256IsZero(digest)) {
				return {};
			}

			CompilerIdentity identity;
			identity.established = true;
			identity.modulePath = std::move(canonical);
			identity.moduleLength = hashedLength;
			identity.moduleDigest = digest;
			identity.mechanism = CompilerIdentityMechanism::kContentHash;
			return identity;
		} catch (...) {
			return {};
		}
	}

	CompilerIdentity ResolveD3DCompilerIdentity() noexcept
	{
		try {
			const auto* address = static_cast<const void*>(__imp_D3DCompile);
			if (!address)
				return {};

			HMODULE module = nullptr;
			if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
						| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(address),
					&module)
				|| !module) {
				return {};
			}

			std::wstring modulePath;
			if (!ResolveModulePath(module, modulePath))
				return {};
			return ResolveCompilerIdentity(modulePath);
		} catch (...) {
			return {};
		}
	}

	const CompilerIdentity& GetD3DCompilerIdentity() noexcept
	{
		static const CompilerIdentity identity = ResolveD3DCompilerIdentity();
		return identity;
	}
}
