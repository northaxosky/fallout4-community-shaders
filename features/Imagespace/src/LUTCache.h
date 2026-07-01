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

	// Shared LUT loader; DeviceNotReady is non-terminal and must not be negative-cached.
	[[nodiscard]] LUTLoadResult LoadLUTFromFile(std::string_view a_filename);

	// Maps LUT base names to 32x32x32 Texture3D SRVs; render thread uses TryGet only.
	class LUTCache
	{
	public:
		// CONFIG-APPLY THREAD ONLY. Synchronously loads misses; DeviceNotReady misses are retryable.
		ID3D11ShaderResourceView* GetOrLoad(const std::string& a_filename);

		// CONFIG-APPLY THREAD ONLY. Loads each filename and ignores failures.
		void Preload(const std::vector<std::string>& a_filenames);

		// RENDER-THREAD SAFE under Imagespace's single-render-thread invariant. Pure lookup; never loads.
		// The returned raw pointer stays valid only while no Clear()/Preload() runs, which the invariant
		// guarantees (both happen end-frame on the same thread, after the render-frame read).
		[[nodiscard]] ID3D11ShaderResourceView* TryGet(const std::string& a_filename) const;

		void Clear() { entries.clear(); }

	private:
		std::unordered_map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> entries;
	};
}
