#include "ClusterGrid.h"

#include "Render/Annotation.h"
#include "Render/ComputeScope.h"
#include "Render/Engine.h"
#include "Render/RenderExtents.h"
#include "Utils/CSUtil.h"

#include <stdexcept>

namespace cs::features::light_limit_fix
{
	namespace
	{
		constexpr auto kBuildPath = L"Data\\Shaders\\LightLimitFix\\ClusterBuildingCS.hlsl";
		constexpr auto kCullPath = L"Data\\Shaders\\LightLimitFix\\ClusterCullingCS.hlsl";

		template <class T>
		std::unique_ptr<buffer::StructuredBuffer> MakeBuffer(std::string_view a_name, std::uint32_t a_count,
			bool a_dynamic)
		{
			auto result = std::make_unique<buffer::StructuredBuffer>(
				buffer::StructuredBufferDesc<T>(a_count, !a_dynamic, a_dynamic), a_count);
			result->CreateSRV();
			if (!a_dynamic)
				result->CreateUAV();
			result->SetName(a_name);
			return result;
		}

		winrt::com_ptr<ID3D11ComputeShader> Compile(const wchar_t* a_path, std::string_view a_name)
		{
			winrt::com_ptr<ID3D11ComputeShader> shader;
			shader.attach(static_cast<ID3D11ComputeShader*>(util::CompileShader(a_path, {}, "cs_5_0")));
			if (!shader)
				throw std::runtime_error("the LLF cluster compute shader failed to compile");
			render::annotation::SetName(shader.get(), a_name);
			return shader;
		}
	}

	ClusterDimensions ClusterGrid::Dimensions(std::uint32_t a_width, std::uint32_t a_height) noexcept
	{
		return { (a_width + kTileSize - 1) / kTileSize, (a_height + kTileSize - 1) / kTileSize, kSlices };
	}

	ClusterDimensions ClusterGrid::ActiveDimensions() noexcept
	{
		const auto* state = engine::GetGraphicsState();
		if (!state)
			return {};
		const auto extent = render::GetActiveExtent(state->screenWidth, state->screenHeight);
		return Dimensions(extent.width, extent.height);
	}

	void ClusterGrid::Create()
	{
		const auto* state = engine::GetGraphicsState();
		if (!state || !state->screenWidth || !state->screenHeight)
			throw std::runtime_error("the LLF cluster grid has no screen size");
		const auto size = Dimensions(state->screenWidth, state->screenHeight);
		_capacity = size[0] * size[1] * size[2];

		_buildShader = Compile(kBuildPath, "LightLimitFix/ClusterBuilding.CS");
		_cullShader = Compile(kCullPath, "LightLimitFix/ClusterCulling.CS");
		_buildCB = std::make_unique<buffer::ConstantBuffer>(buffer::ConstantBufferDesc<LightBuildingCB>());
		_cullCB = std::make_unique<buffer::ConstantBuffer>(buffer::ConstantBufferDesc<LightCullingCB>());
		_buildCB->SetName("LLF Cluster Building CB");
		_cullCB->SetName("LLF Cluster Culling CB");

		_lights = MakeBuffer<LightData>("LLF Lights", kMaxLights, true);
		_cones = MakeBuffer<ConeData>("LLF Cones", kMaxLights, true);
		_clusters = MakeBuffer<ClusterAABB>("LLF Clusters", _capacity, false);
		_lightIndexCounter = MakeBuffer<std::uint32_t>("LLF Light Index Counter", 1, false);
		_lightIndexList = MakeBuffer<std::uint32_t>("LLF Light Index List", _capacity * kClusterMaxLights, false);
		_lightGrid = MakeBuffer<LightGrid>("LLF Light Grid", _capacity, false);
	}

	bool ClusterGrid::Build(const LightGather& a_gather, const engine::WorldCameraRecord& a_camera)
	{
		auto* context = engine::GetImmediateContext();
		const auto clusterSize = ActiveDimensions();
		const auto clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];
		if (!context || clusterCount == 0 || clusterCount > _capacity)
			return false;

		const auto lights = a_gather.Lights();
		const auto cones = a_gather.Cones();
		const auto lightCount = static_cast<std::uint32_t>(lights.size());
		if (lightCount) {
			_lights->Update(lights.data(), lights.size_bytes());
			_cones->Update(cones.data(), cones.size_bytes());
		}

		const auto depth = engine::GetCameraDepthParameters(a_camera);
		const LightBuildingCB building{ .LightsNear = depth.y, .LightsFar = depth.x, .ClusterSize = { clusterSize[0], clusterSize[1], clusterSize[2], 0 } };
		const LightCullingCB culling{ .LightCount = lightCount,
			.ClusterSize = { clusterSize[0], clusterSize[1], clusterSize[2], 0 } };
		_buildCB->Update(building);
		_cullCB->Update(culling);

		ComputeScope scope(context, 2, 0, 3, 1);
		{
			render::annotation::ScopedEvent event("LightLimitFix::ClusterBuild");
			auto* constants = _buildCB->CB();
			auto* clusters = _clusters->UAV();
			context->CSSetConstantBuffers(0, 1, &constants);
			context->CSSetUnorderedAccessViews(0, 1, &clusters, nullptr);
			context->CSSetShader(_buildShader.get(), nullptr, 0);
			context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);
			ID3D11UnorderedAccessView* none = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
		}
		{
			render::annotation::ScopedEvent event("LightLimitFix::ClusterCull");
			const UINT reset[4]{};
			context->ClearUnorderedAccessViewUint(_lightIndexCounter->UAV(), reset);
			auto* constants = _cullCB->CB();
			ID3D11ShaderResourceView* srvs[]{ _clusters->SRV(), _lights->SRV() };
			ID3D11UnorderedAccessView* uavs[]{ _lightIndexCounter->UAV(), _lightIndexList->UAV(), _lightGrid->UAV() };
			context->CSSetConstantBuffers(0, 1, &constants);
			context->CSSetShaderResources(0, 2, srvs);
			context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
			context->CSSetShader(_cullShader.get(), nullptr, 0);
			context->Dispatch((clusterSize[0] + 15) / 16, (clusterSize[1] + 15) / 16, (clusterSize[2] + 3) / 4);
		}
		return true;
	}
}
