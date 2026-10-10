#pragma once

#include "ExponentialHeightFogSettings.h"
#include "Render/FrameBuffer.h"
#include "Utils/CSBuffer.h"

#include <array>
#include <memory>
#include <string_view>

namespace cs::telemetry
{
	class Sink;
}

namespace cs::features::exponential_height_fog
{
	class VolumetricFog
	{
	public:
		bool Initialize(ID3D11Device*);
		void Prepare(const Settings&, std::uint32_t a_width, std::uint32_t a_height);
		bool Dispatch(ID3D11DeviceContext*, const Settings&, const engine::WorldCameraRecord&,
			std::uint32_t a_frame, bool a_temporal);
		void Reset();
		void CompositeSky(ID3D11DeviceContext*);
		void CollectTelemetry(telemetry::Sink&) const;
		bool SkyReady() const { return _skyReady; }
		ID3D11ShaderResourceView* Integrated() const { return _near.integrated ? _near.integrated->srv.get() : nullptr; }
		ID3D11ShaderResourceView* IntegratedFar() const { return _far.integrated ? _far.integrated->srv.get() : nullptr; }
		ID3D11SamplerState* Sampler() const { return _linearSampler.get(); }
		DirectX::XMUINT3 Grid() const { return _near.grid; }

	private:
		struct alignas(16) Constants
		{
			DirectX::XMUINT4 gridSizeAndFlags{};
			DirectX::XMFLOAT4 invGridSizeAndNearFade{}, gridZParams{};
			DirectX::XMFLOAT4X4 clipToWorld{};
			DirectX::XMFLOAT4 frameJitterOffsets[16]{};
			DirectX::XMFLOAT4 historyParameters{}, jitterParameters{};
			DirectX::XMUINT4 farGridSizeAndFlags{};
			DirectX::XMFLOAT4 farInvGridSizeAndNearFade{}, farGridZParams{}, farRange{};
		};
		static_assert(sizeof(Constants) == 464);
		static_assert(offsetof(Constants, clipToWorld) == 48);
		static_assert(offsetof(Constants, historyParameters) == 368);
		static_assert(offsetof(Constants, farGridSizeAndFlags) == 400);
		// One froxel volume and its history; far continues where near ends.
		struct Volume
		{
			std::unique_ptr<buffer::Texture3D> material, scattering, history, integrated;
			std::unique_ptr<buffer::Texture2D> depth, depthHistory;
			DirectX::XMUINT3 grid{};
			bool hasHistory = false, hasDepthHistory = false;
		};
		void Allocate(Volume&, const DirectX::XMUINT3& a_grid, std::string_view a_prefix) const;
		bool UpdateCamera(const engine::WorldCameraRecord&, const DirectX::XMFLOAT2& a_previousRatio);
		void PrepareSky();

		winrt::com_ptr<ID3D11Device> _device;
		winrt::com_ptr<ID3D11SamplerState> _linearSampler, _shadowSampler;
		std::unique_ptr<buffer::ConstantBuffer> _constants;
		std::unique_ptr<buffer::ConstantBuffer> _cameraConstants;
		std::array<std::array<winrt::com_ptr<ID3D11ComputeShader>, 4>, 2> _shaders;
		winrt::com_ptr<ID3D11ComputeShader> _skyShader;
		std::unique_ptr<buffer::Texture2D> _skySource, _skyOutput;
		Volume _near, _far;
		std::uint32_t _lastFrame = UINT32_MAX;
		std::uint32_t _lastSkyFrame = UINT32_MAX;
		bool _skyReady = false;
		DirectX::XMFLOAT2 _previousRatio{ 1.0f, 1.0f };
		// Render-thread counters persist across resource resets.
		std::uint64_t _volumeAllocations{}, _skyAllocations{}, _volumeFrames{}, _historyFrames{}, _skylightingFrames{}, _skyDispatches{}, _dispatches{};
		bool _temporalEnabled{};
	};
}
