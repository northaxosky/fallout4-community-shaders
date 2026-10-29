#pragma once

#include "LightData.h"

#include "RE/B/BSDFTiledLighting.h"
#include "Utils/CSBuffer.h"

#include <array>
#include <memory>
#include <mutex>
#include <optional>

namespace cs::features::light_limit_fix
{
	// FO4: per-light records for native consumers (t8 tiled, b11 raster).
	class DeferredLights
	{
	public:
		static constexpr std::uint32_t kMaxLights = RE::BSDFTiledLighting::MAX_LIGHTS;

		void Create();
		void Record(std::uint32_t a_side, std::uint32_t a_index, const LightData& a_light);
		void Clear();
		// Throws when the D3D11 upload fails.
		void UploadTiled(std::uint32_t a_side, std::uint32_t a_count);
		ID3D11ShaderResourceView* TiledView() const;
		// Throws on D3D11 upload failure; unchanged records are skipped.
		ID3D11Buffer* RasterBuffer(const LightData& a_light);

	private:
		std::mutex _mutex;
		std::array<std::array<LightData, kMaxLights>, 2> _records{};
		std::array<std::unique_ptr<buffer::StructuredBuffer>, 2> _tiled;
		buffer::CachedConstantBuffer<LightData> _raster;
		std::uint32_t _uploadedSide = 0;
	};
}
