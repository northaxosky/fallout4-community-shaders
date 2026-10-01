#pragma once

#include "ExponentialHeightFogSettings.h"
#include "Render/FrameBuffer.h"
#include "Utils/CSBuffer.h"

#include <array>
#include <memory>

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
		ID3D11ShaderResourceView* Integrated() const { return _integrated ? _integrated->srv.get() : nullptr; }
		ID3D11SamplerState* Sampler() const { return _linearSampler.get(); }
		DirectX::XMUINT3 Grid() const { return _grid; }

	private:
		struct alignas(16) Constants
		{
			DirectX::XMUINT4 gridSizeAndFlags{};
			DirectX::XMFLOAT4 invGridSizeAndNearFade{}, gridZParams{};
			DirectX::XMFLOAT4X4 clipToWorld{};
			DirectX::XMFLOAT4 frameJitterOffsets[16]{};
			DirectX::XMFLOAT4 historyParameters{}, jitterParameters{};
		};
		static_assert(sizeof(Constants) == 400);
		static_assert(offsetof(Constants, clipToWorld) == 48);
		static_assert(offsetof(Constants, historyParameters) == 368);
		bool UpdateCamera(const engine::WorldCameraRecord&, const DirectX::XMFLOAT2& a_previousRatio);
		void PrepareSky();

		winrt::com_ptr<ID3D11Device> _device;
		winrt::com_ptr<ID3D11SamplerState> _linearSampler, _shadowSampler;
		std::unique_ptr<buffer::ConstantBuffer> _constants;
		std::unique_ptr<buffer::ConstantBuffer> _cameraConstants;
		std::array<winrt::com_ptr<ID3D11ComputeShader>, 4> _shaders;
		winrt::com_ptr<ID3D11ComputeShader> _skyShader;
		std::unique_ptr<buffer::Texture2D> _skySource, _skyOutput;
		std::unique_ptr<buffer::Texture3D> _material, _scattering, _history, _integrated;
		std::unique_ptr<buffer::Texture2D> _depth, _depthHistory;
		DirectX::XMUINT3 _grid{};
		std::uint32_t _lastFrame = UINT32_MAX;
		std::uint32_t _lastSkyFrame = UINT32_MAX;
		bool _skyReady = false;
		bool _hasHistory = false, _hasDepthHistory = false;
		DirectX::XMFLOAT2 _previousRatio{ 1.0f, 1.0f };
		// Render-thread counters persist across resource resets.
		std::uint64_t _volumeAllocations{}, _skyAllocations{}, _volumeFrames{}, _historyFrames{}, _skyDispatches{};
		bool _temporalEnabled{};
	};
}
