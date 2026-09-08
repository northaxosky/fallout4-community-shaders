#include "Utils/StreamlineModule.h"

#include <expected>
#include <string>
#include <system_error>

namespace cs::files
{
	namespace
	{
		using sl::security::TrustFailure;

		std::expected<bool, DWORD> Exists(const std::filesystem::path& a_path)
		{
			const auto attributes = GetFileAttributesW(a_path.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES) {
				if (attributes & FILE_ATTRIBUTE_DIRECTORY)
					return std::unexpected(ERROR_DIRECTORY);
				return true;
			}
			const auto error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
				return false;
			return std::unexpected(error);
		}

		struct Resolver
		{
			std::filesystem::path directory;
		};

		bool ResolveFile(
			void* a_context,
			std::wstring_view a_basename,
			std::wstring& a_physicalPath)
		{
			auto& resolver = *static_cast<Resolver*>(a_context);
			// The verifier opens each virtual winner and binds its physical path to that held handle.
			a_physicalPath = (resolver.directory / std::filesystem::path(a_basename)).wstring();
			return true;
		}
	}

	sl::security::ProjectLoadResult LoadStreamlineInterposer(
		const std::filesystem::path& a_directory,
		const sl::security::ProjectTrustConfig& a_trust)
	{
		if (a_directory.empty())
			return { TrustFailure::eInvalidArgument, ERROR_INVALID_PARAMETER, nullptr };

		std::error_code error;
		const auto directory = std::filesystem::absolute(a_directory, error);
		if (error)
			return { TrustFailure::eResolutionFailed, static_cast<DWORD>(error.value()), nullptr };

		const auto manifest = directory / sl::security::kProjectManifestName;
		const auto signature = directory / sl::security::kProjectManifestSignatureName;
		const auto haveManifest = Exists(manifest);
		const auto haveSignature = Exists(signature);
		if (!haveManifest || !haveSignature)
			return { TrustFailure::eManifestIo,
				!haveManifest ? haveManifest.error() : haveSignature.error(), nullptr };
		if (*haveManifest != *haveSignature)
			return { TrustFailure::eManifestIncomplete, ERROR_FILE_NOT_FOUND, nullptr };

		Resolver resolver{ directory };
		if (!*haveManifest) {
			return sl::security::authenticateAndLoadNvidiaLibrary(
				(directory / L"sl.interposer.dll").wstring());
		}

		const auto manifestPath = manifest.wstring();
		const auto signaturePath = signature.wstring();
		sl::security::ProjectLoadOptions options{
			.requestedBasename = L"sl.interposer.dll",
			.manifestPath = manifestPath,
			.signaturePath = signaturePath,
			.resolve = ResolveFile,
			.resolveContext = &resolver,
			.trust = &a_trust
		};
		return sl::security::authenticateAndLoadProjectLibrary(options);
	}
}
