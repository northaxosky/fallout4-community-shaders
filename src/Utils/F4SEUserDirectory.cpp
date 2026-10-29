#include "Utils/F4SEUserDirectory.h"

#include "F4SE/API.h"
#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

#include <memory>
#include <string>

namespace cs::util
{
	std::filesystem::path F4SEUserDirectory()
	{
		wchar_t* known = nullptr;
		const auto result = REX::W32::SHGetKnownFolderPath(
			REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(known));
		std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> folder(known, REX::W32::CoTaskMemFree);
		if (!folder || result != 0)
			return {};
		auto name = F4SE::GetSaveFolderName();
		return std::filesystem::path(folder.get()) / "My Games" / (name.empty() ? "Fallout4" : std::string(name)) / "F4SE";
	}
}
