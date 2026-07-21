#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cs::features::replacement
{
	struct ReplacementEntry
	{
		std::string name;
		bool        default_enabled = false;
		std::string comment;
		bool        enabled_in_ini = false;
	};

	class Registry
	{
	public:
		static Registry& Get();

		bool LoadFromJson(const std::wstring& path);

		ReplacementEntry* FindByName(std::string_view a_name) noexcept;
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
