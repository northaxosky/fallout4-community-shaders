#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <winrt/base.h>

namespace cs::features::imagespace
{
	enum class LUTLoadStatus
	{
		Ok,
		DeviceNotReady,
		FileMissing,
		DDSLoadFailed,
		DimensionsMismatch,
		CreateTextureFailed,
		SRVCreationFailed,
	};

	struct LUTLoadResult
	{
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		LUTLoadStatus                            status = LUTLoadStatus::Ok;
	};

	// Device-not-ready misses stay retryable.
	[[nodiscard]] LUTLoadResult LoadLUTFromFile(std::string_view a_filename);

	// Render thread performs lookup only.
	class LUTCache
	{
	public:
		// Config thread only; device misses remain retryable.
		ID3D11ShaderResourceView* GetOrLoad(const std::string& a_filename);

		// Config thread only.
		void Preload(const std::vector<std::string>& a_filenames);

		// Render thread only; never loads.
		[[nodiscard]] ID3D11ShaderResourceView* TryGet(const std::string& a_filename) const;

		void Clear() { entries.clear(); }

	private:
		std::unordered_map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> entries;
	};
}
