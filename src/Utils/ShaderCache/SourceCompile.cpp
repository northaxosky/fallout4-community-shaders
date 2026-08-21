#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include "Utils/ShaderCache/SourceCompile.h"

#include "Utils/ShaderCache/CacheStorage.h"

#include <algorithm>
#include <d3dcompiler.h>
#include <memory>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <wrl/client.h>

namespace cs::shader_cache
{
	namespace
	{
		std::filesystem::path ResolveCandidate(
			const std::filesystem::path& a_directory,
			const std::filesystem::path& a_name)
		{
			std::error_code error;
			auto candidate = std::filesystem::weakly_canonical(a_directory / a_name, error);
			if (error || candidate.empty())
				candidate = (a_directory / a_name).lexically_normal();
			return candidate;
		}

		class TracingIncludeHandler final : public ID3DInclude
		{
		public:
			TracingIncludeHandler(
				const void*                               a_rootData,
				std::string                               a_rootLocator,
				std::filesystem::path                     a_rootDirectory,
				const std::vector<std::filesystem::path>& a_includeRoots,
				DependencyManifest&                       a_manifest) :
				_rootData(a_rootData),
				_rootLocator(std::move(a_rootLocator)),
				_rootDirectory(std::move(a_rootDirectory)),
				_includeRoots(a_includeRoots),
				_manifest(&a_manifest)
			{}
			HRESULT STDMETHODCALLTYPE Open(
				D3D_INCLUDE_TYPE a_includeType,
				LPCSTR           a_fileName,
				LPCVOID          a_parentData,
				LPCVOID*         a_data,
				UINT*            a_bytes) override
			{
				if (!a_fileName || !a_data || !a_bytes)
					return E_INVALIDARG;

				*a_data  = nullptr;
				*a_bytes = 0;

				// never unwind through d3dcompiler
				try {
					IncludeResolution resolution;
					resolution.kind = a_includeType == D3D_INCLUDE_SYSTEM
						? IncludeKind::kSystem
						: IncludeKind::kLocal;
					resolution.requestedName = a_fileName;
					resolution.parentLocator = _rootLocator;

					auto includingDirectory = _rootDirectory;
					if (a_parentData && a_parentData == _rootData) {
						resolution.parentLocator = _rootLocator;
					} else if (a_parentData) {
						const auto parent = _openedFiles.find(a_parentData);
						if (parent != _openedFiles.end()) {
							includingDirectory       = parent->second.directory;
							resolution.parentLocator = parent->second.locator;
						}
					}

					// preserve the compiler's native include encoding
					const std::filesystem::path requested(resolution.requestedName);
					return Resolve(includingDirectory, requested, resolution, a_data, a_bytes);
				} catch (...) {
					*a_data  = nullptr;
					*a_bytes = 0;
					return E_FAIL;
				}
			}

			HRESULT STDMETHODCALLTYPE Close(LPCVOID a_data) override
			{
				if (a_data == _rootData)
					return S_OK;
				try {
					const auto file = _openedFiles.find(a_data);
					if (file == _openedFiles.end())
						return E_FAIL;
					_openedFiles.erase(file);
					return S_OK;
				} catch (...) {
					return E_FAIL;
				}
			}

		private:
			struct OpenedFile
			{
				std::unique_ptr<std::uint8_t[]> buffer;
				std::filesystem::path           directory;
				std::string                     locator;
			};

			HRESULT Resolve(
				const std::filesystem::path& a_includingDirectory,
				const std::filesystem::path& a_requested,
				IncludeResolution&           a_resolution,
				LPCVOID*                     a_data,
				UINT*                        a_bytes)
			{
				std::vector<std::filesystem::path> candidates;
				candidates.reserve(_includeRoots.size() + 1);
				const auto addCandidate = [&candidates](std::filesystem::path a_candidate) {
					if (std::ranges::find(candidates, a_candidate) == candidates.end())
						candidates.push_back(std::move(a_candidate));
				};
				addCandidate(ResolveCandidate(a_includingDirectory, a_requested));
				for (const auto& root : _includeRoots)
					addCandidate(ResolveCandidate(root, a_requested));

				std::vector<std::uint8_t> bytes;
				for (const auto& candidate : candidates) {
					auto         locator = EncodeLocator(candidate);
					IncludeProbe probe;
					probe.path = locator;

					const auto status = ReadFileBytes(candidate, kMaxSourceBytes, bytes);
					if (status == FileReadStatus::kMissing) {
						probe.status = ProbeStatus::kMissing;
						a_resolution.probes.push_back(std::move(probe));
						continue;
					}
					if (status != FileReadStatus::kOk) {
						probe.status = ProbeStatus::kReadFailed;
						a_resolution.probes.push_back(std::move(probe));
						_manifest->includes.push_back(std::move(a_resolution));
						return E_FAIL;
					}

					// hash exactly what the compiler receives
					probe.status        = ProbeStatus::kSuccess;
					probe.contentDigest = sha256::Sha256Compute(bytes.data(), bytes.size());
					probe.contentLength = bytes.size();
					a_resolution.probes.push_back(std::move(probe));

					auto buffer =
						std::make_unique<std::uint8_t[]>(std::max<std::size_t>(bytes.size(), 1));
					std::ranges::copy(bytes, buffer.get());
					auto* data = buffer.get();

					const auto [file, inserted] = _openedFiles.emplace(
						static_cast<LPCVOID>(data),
						OpenedFile{
							std::move(buffer),
							candidate.parent_path(),
							std::move(locator) });
					if (!inserted) {
						_manifest->includes.push_back(std::move(a_resolution));
						return E_FAIL;
					}

					*a_data  = file->first;
					*a_bytes = static_cast<UINT>(bytes.size());
					_manifest->includes.push_back(std::move(a_resolution));
					return S_OK;
				}

				_manifest->includes.push_back(std::move(a_resolution));
				return E_FAIL;
			}

			const void*                             _rootData;
			std::string                             _rootLocator;
			std::filesystem::path                   _rootDirectory;
			std::vector<std::filesystem::path>      _includeRoots;
			DependencyManifest*                     _manifest;
			std::unordered_map<LPCVOID, OpenedFile> _openedFiles;
		};

		std::string ExtractErrorText(ID3DBlob* a_errors)
		{
			std::string text;
			if (!a_errors)
				return text;
			const auto* data = static_cast<const char*>(a_errors->GetBufferPointer());
			const auto  size = a_errors->GetBufferSize();
			if (data && size != 0) {
				text.assign(data, size);
				while (!text.empty() && text.back() == '\0')
					text.pop_back();
			}
			return text;
		}
	}

	SourceCompileOutcome CompileSourceWithManifest(const ShaderRecipe& a_recipe)
	{
		SourceCompileOutcome outcome;

		std::vector<std::uint8_t> rootBytes;
		switch (ReadFileBytes(a_recipe.source, kMaxSourceBytes, rootBytes)) {
		case FileReadStatus::kOk:
			break;
		case FileReadStatus::kMissing:
			outcome.error = "Shader source missing";
			return outcome;
		default:
			outcome.error = "Shader source unreadable";
			return outcome;
		}
		if (rootBytes.empty()) {
			outcome.error = "Shader source is empty";
			return outcome;
		}

		outcome.manifest.rootLocator = EncodeLocator(a_recipe.source);
		outcome.manifest.rootDigest =
			sha256::Sha256Compute(rootBytes.data(), rootBytes.size());
		outcome.manifest.rootLength = rootBytes.size();

		std::vector<D3D_SHADER_MACRO> macros;
		macros.reserve(a_recipe.defines.size() + 1);
		for (const auto& [name, value] : a_recipe.defines)
			macros.push_back({ name.c_str(), value.c_str() });
		macros.push_back({ nullptr, nullptr });

		std::filesystem::path rootDirectory = a_recipe.source.parent_path();
		if (rootDirectory.empty() && !a_recipe.includeRoots.empty())
			rootDirectory = a_recipe.includeRoots.front();

		TracingIncludeHandler handler(
			rootBytes.data(),
			outcome.manifest.rootLocator,
			rootDirectory,
			a_recipe.includeRoots,
			outcome.manifest);

		Microsoft::WRL::ComPtr<ID3DBlob> blob;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		const HRESULT                    result = D3DCompile(
            rootBytes.data(),
            rootBytes.size(),
            outcome.manifest.rootLocator.c_str(),
            macros.data(),
            &handler,
            a_recipe.entryPoint.c_str(),
            a_recipe.profile.c_str(),
            a_recipe.flags1,
            a_recipe.flags2,
            blob.GetAddressOf(),
            errors.GetAddressOf());

		outcome.error = ExtractErrorText(errors.Get());
		if (FAILED(result) || !blob || blob->GetBufferSize() == 0) {
			if (outcome.error.empty())
				outcome.error = "Unknown error";
			return outcome;
		}

		const auto* payload = static_cast<const std::uint8_t*>(blob->GetBufferPointer());
		outcome.bytecode.assign(payload, payload + blob->GetBufferSize());
		outcome.succeeded = true;
		outcome.error.clear();
		return outcome;
	}
}
