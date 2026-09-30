#pragma once

#include "FeatureBuffer.h"
#include "Render/FrameBufferMath.h"
#include "Render/SharedFeatureData.h"

#include <array>
#include <limits>

namespace cs::render
{
	[[nodiscard]] inline DirectX::XMFLOAT4 PackAmbientSH(const DirectX::XMFLOAT4& a_row) noexcept
	{
		return { a_row.w / 0.2820948f, -a_row.y / 0.4886025f, a_row.z / 0.4886025f, -a_row.x / 0.4886025f };
	}

	struct alignas(16) FrameDataCB
	{
		DirectX::XMFLOAT4X4 CameraView{}, CameraProj{}, CameraViewProj{};
		DirectX::XMFLOAT4X4 CameraViewProjUnjittered{}, CameraPreviousViewProjUnjittered{};
		DirectX::XMFLOAT4X4 CameraProjUnjittered{}, CameraProjUnjitteredInverse{};
		DirectX::XMFLOAT4X4 CameraViewInverse{}, CameraViewProjInverse{}, CameraProjInverse{};
		DirectX::XMFLOAT4 CameraPosAdjust{}, CameraPreviousPosAdjust{}, FrameParams{};
		DirectX::XMFLOAT4 DynamicResolutionParams1{}, DynamicResolutionParams2{};
	};
#define CS_FRAME_OFFSET(member, offset) static_assert(offsetof(FrameDataCB, member) == offset)
	CS_FRAME_OFFSET(CameraView, 0);
	CS_FRAME_OFFSET(CameraProj, 64);
	CS_FRAME_OFFSET(CameraViewProj, 128);
	CS_FRAME_OFFSET(CameraViewProjUnjittered, 192);
	CS_FRAME_OFFSET(CameraPreviousViewProjUnjittered, 256);
	CS_FRAME_OFFSET(CameraProjUnjittered, 320);
	CS_FRAME_OFFSET(CameraProjUnjitteredInverse, 384);
	CS_FRAME_OFFSET(CameraViewInverse, 448);
	CS_FRAME_OFFSET(CameraViewProjInverse, 512);
	CS_FRAME_OFFSET(CameraProjInverse, 576);
	CS_FRAME_OFFSET(CameraPosAdjust, 640);
	CS_FRAME_OFFSET(CameraPreviousPosAdjust, 656);
	CS_FRAME_OFFSET(FrameParams, 672);
	CS_FRAME_OFFSET(DynamicResolutionParams1, 688);
	CS_FRAME_OFFSET(DynamicResolutionParams2, 704);
#undef CS_FRAME_OFFSET
	static_assert(sizeof(FrameDataCB) == 720);

	struct alignas(16) SharedDataCB
	{
		std::array<DirectX::XMFLOAT4, 25> WaterData = [] {
			std::array<DirectX::XMFLOAT4, 25> water{};
			water.fill({ 1.0f, 1.0f, 1.0f, -2147483648.0f });
			return water;
		}();
		DirectX::XMFLOAT4 DirLightDirection{}, DirLightColor{}, SunDirection{}, SunColor{};
		DirectX::XMFLOAT4 MasserDirection{}, MasserColor{}, SecundaDirection{}, SecundaColor{};
		DirectX::XMFLOAT4 CameraData{}, BufferDim{};
		float Timer = 0.0f;
		std::uint32_t FrameCount = 0, FrameCountAlwaysActive = 0, InInterior = 0;
		std::uint32_t HasDirectionalShadows = 0, InMapMenu = 0, HideSky = 0;
		float MipBias = 0.0f;
		float WaterSystemHeight = -(std::numeric_limits<float>::max)();
		float pad0[3]{};
		DirectX::XMFLOAT4 AmbientSHR{}, AmbientSHG{}, AmbientSHB{}, HDRData{};
	};
#define CS_SHARED_OFFSET(member, offset) static_assert(offsetof(SharedDataCB, member) == offset)
	CS_SHARED_OFFSET(WaterData, 0);
	CS_SHARED_OFFSET(DirLightDirection, 400);
	CS_SHARED_OFFSET(DirLightColor, 416);
	CS_SHARED_OFFSET(SunDirection, 432);
	CS_SHARED_OFFSET(SunColor, 448);
	CS_SHARED_OFFSET(MasserDirection, 464);
	CS_SHARED_OFFSET(MasserColor, 480);
	CS_SHARED_OFFSET(SecundaDirection, 496);
	CS_SHARED_OFFSET(SecundaColor, 512);
	CS_SHARED_OFFSET(CameraData, 528);
	CS_SHARED_OFFSET(BufferDim, 544);
	CS_SHARED_OFFSET(Timer, 560);
	CS_SHARED_OFFSET(FrameCount, 564);
	CS_SHARED_OFFSET(FrameCountAlwaysActive, 568);
	CS_SHARED_OFFSET(InInterior, 572);
	CS_SHARED_OFFSET(HasDirectionalShadows, 576);
	CS_SHARED_OFFSET(InMapMenu, 580);
	CS_SHARED_OFFSET(HideSky, 584);
	CS_SHARED_OFFSET(MipBias, 588);
	CS_SHARED_OFFSET(WaterSystemHeight, 592);
	CS_SHARED_OFFSET(pad0, 596);
	CS_SHARED_OFFSET(AmbientSHR, 608);
	CS_SHARED_OFFSET(AmbientSHG, 624);
	CS_SHARED_OFFSET(AmbientSHB, 640);
	CS_SHARED_OFFSET(HDRData, 656);
#undef CS_SHARED_OFFSET
	static_assert(sizeof(SharedDataCB) == 672);

	struct alignas(16) FO4SharedDataCB
	{
		ScreenSpaceGIFeatureData screenSpaceGISettings{};
		InverseSquareLightingFeatureData inverseSquareLightingSettings{};
		ExponentialHeightFogFeatureData exponentialHeightFogSettings{};
		std::uint32_t WetnessDebugVisualization = 0, TerrainShadowMode = 0;
		std::uint32_t DynamicCubemapsDebugVisualization = 0, EnabledSSR = 0;
		DirectX::XMFLOAT2 HeightRange{}, DebugHeightRange{};
		float DeltaTime = 0.0f, pad0[3]{};
	};
	static_assert(sizeof(FO4SharedDataCB) == 96);
	static_assert(offsetof(FO4SharedDataCB, WetnessDebugVisualization) == 48);
	static_assert(offsetof(FO4SharedDataCB, HeightRange) == 64);
	static_assert(offsetof(FO4SharedDataCB, DeltaTime) == 80);

	[[nodiscard]] inline FrameDataCB PackFrameData(
		const engine::WorldCameraRecord& a_camera, DirectX::XMFLOAT2 a_ratio,
		DirectX::XMFLOAT2 a_previousRatio, float a_clampOffset) noexcept
	{
		using namespace DirectX;
		FrameDataCB data{};
		const auto pack = [](XMFLOAT4X4& a_dest, const XMFLOAT4X4& a_source) {
			XMStoreFloat4x4(&a_dest, XMMatrixTranspose(XMLoadFloat4x4(&a_source)));
		};
		// Upstream row_major matrices multiply column vectors.
		pack(data.CameraView, a_camera.View);
		pack(data.CameraProj, a_camera.Projection);
		pack(data.CameraViewProj, a_camera.ViewProjection);
		pack(data.CameraViewProjUnjittered, a_camera.ViewProjectionUnjittered);
		pack(data.CameraPreviousViewProjUnjittered, a_camera.PreviousViewProjectionUnjittered);
		pack(data.CameraProjUnjittered, a_camera.ProjectionUnjittered);
		pack(data.CameraProjUnjitteredInverse, a_camera.ProjectionUnjitteredInverse);
		pack(data.CameraViewInverse, a_camera.ViewInverse);
		pack(data.CameraViewProjInverse, a_camera.ViewProjectionInverse);
		pack(data.CameraProjInverse, a_camera.ProjectionInverse);
		data.CameraPosAdjust = a_camera.CameraPosAdjust;
		data.CameraPreviousPosAdjust = a_camera.CameraPreviousPosAdjust;
		data.CameraPreviousPosAdjust.w = a_clampOffset;
		// FO4 has no validated inverse-gamma or Skyrim frame-flag source.
		data.FrameParams = {};
		data.DynamicResolutionParams1 = { a_ratio.x, a_ratio.y, a_previousRatio.x, a_previousRatio.y };
		data.DynamicResolutionParams2 = { 1.0f / a_ratio.x, 1.0f / a_ratio.y,
			a_ratio.x - a_clampOffset, a_previousRatio.x - a_clampOffset };
		return data;
	}
}
