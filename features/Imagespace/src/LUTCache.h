#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <winrt/base.h>

namespace cs::features::imagespace
{
	// Maps base LUT filename (without ".dds" extension and directory prefix) to an SRV referencing the
	// loaded 32x32x32 Texture3D. The cache decouples per-frame LUT selection from synchronous DDS I/O:
	// LUTs are populated via Preload/GetOrLoad on the config-apply path (game thread / ImGui), and the
	// render thread calls only TryGet which is a pure lookup.
	class LUTCache
	{
	public:
		// CONFIG-APPLY THREAD ONLY. Performs synchronous DDS load on cache miss. Returns nullptr on
		// failure (missing file, bad dims, device error). Successful loads are cached for the
		// lifetime of the LUTCache instance.
		ID3D11ShaderResourceView* GetOrLoad(const std::string& a_filename);

		// CONFIG-APPLY THREAD ONLY. Convenience: GetOrLoad each filename and ignore the return value.
		void Preload(const std::vector<std::string>& a_filenames);

		// RENDER-THREAD SAFE. Pure cache lookup; returns nullptr if a_filename was never preloaded.
		[[nodiscard]] ID3D11ShaderResourceView* TryGet(const std::string& a_filename) const;

		void Clear() { entries.clear(); }

	private:
		std::unordered_map<std::string, winrt::com_ptr<ID3D11ShaderResourceView>> entries;
	};
}
