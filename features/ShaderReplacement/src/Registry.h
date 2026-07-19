#pragma once

#include "Sha1.h"

#include <atomic>
#include <d3d11.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <winrt/base.h>

namespace cs::features::replacement
{
	struct ReplacementEntry
	{
		std::string                                       name;
		Sha1Result                                        runtime_sha1{};
		bool                                              runtime_sha1_known = false;
		std::wstring                                      hlsl_abs_path;
		std::string                                       entry  = "main";
		std::string                                       profile = "ps_5_0";
		std::vector<std::pair<std::string, std::string>>  defines;
		bool                                              default_enabled = false;
		std::string                                       comment;

		bool                                              enabled_in_ini     = false;
		bool                                              compile_attempted  = false;
		bool                                              compile_ok         = false;
		std::string                                       compile_error;
		std::string                                       compiled_sha1_hex;
		winrt::com_ptr<ID3D11PixelShader>                 compiled_ps;

		std::atomic<uint64_t>                             match_hits{ 0 };
		std::atomic<uint64_t>                             substitution_hits{ 0 };
		std::atomic<uint64_t>                             passthrough_compile_fail{ 0 };
		std::atomic<uint64_t>                             passthrough_disabled{ 0 };

		ReplacementEntry() = default;
		ReplacementEntry(const ReplacementEntry&) = delete;
		ReplacementEntry& operator=(const ReplacementEntry&) = delete;
	};

	class Registry
	{
	public:
		static Registry& Get();

		bool LoadFromJson(const std::wstring& path, const std::wstring& shaders_root);

		ReplacementEntry* FindByName(std::string_view a_name) noexcept;
		ReplacementEntry* FindByRuntimeSha1(const Sha1Result& s) noexcept;
		std::vector<std::unique_ptr<ReplacementEntry>>&       All() noexcept       { return _entries; }
		const std::vector<std::unique_ptr<ReplacementEntry>>& All() const noexcept { return _entries; }

		bool Loaded() const noexcept { return _loaded; }
		const std::string& LastError() const noexcept { return _lastError; }

	private:
		Registry() = default;

		std::vector<std::unique_ptr<ReplacementEntry>> _entries;
		bool                                           _loaded = false;
		std::string                                    _lastError;
	};
}
