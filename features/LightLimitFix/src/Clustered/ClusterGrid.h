#pragma once

#include "LightGather.h"
#include "Render/FrameBuffer.h"
#include "Utils/CSBuffer.h"

#include <array>
#include <cstdint>
#include <memory>

namespace cs::features::light_limit_fix
{
	using ClusterDimensions = std::array<std::uint32_t, 3>;

	class ClusterGrid
	{
	public:
		static constexpr std::uint32_t kTileSize = 64;
		static constexpr std::uint32_t kSlices = 32;

		static ClusterDimensions Dimensions(std::uint32_t a_width, std::uint32_t a_height) noexcept;
		static ClusterDimensions ActiveDimensions() noexcept;

		// Throws when a shader or resource cannot be created.
		void Create();
		// Throws on upload failure; false when the grid cannot hold the frame.
		bool Build(const LightGather& a_gather, const engine::WorldCameraRecord& a_camera);

		ID3D11ShaderResourceView* LightsView() const { return _lights->SRV(); }
		ID3D11ShaderResourceView* ConesView() const { return _cones->SRV(); }
		ID3D11ShaderResourceView* IndexListView() const { return _lightIndexList->SRV(); }
		ID3D11ShaderResourceView* GridView() const { return _lightGrid->SRV(); }

	private:
		winrt::com_ptr<ID3D11ComputeShader> _buildShader;
		winrt::com_ptr<ID3D11ComputeShader> _cullShader;
		std::unique_ptr<buffer::ConstantBuffer> _buildCB;
		std::unique_ptr<buffer::ConstantBuffer> _cullCB;
		std::unique_ptr<buffer::StructuredBuffer> _lights;
		std::unique_ptr<buffer::StructuredBuffer> _cones;
		std::unique_ptr<buffer::StructuredBuffer> _clusters;
		std::unique_ptr<buffer::StructuredBuffer> _lightIndexCounter;
		std::unique_ptr<buffer::StructuredBuffer> _lightIndexList;
		std::unique_ptr<buffer::StructuredBuffer> _lightGrid;
		std::uint32_t _capacity = 0;
	};
}
