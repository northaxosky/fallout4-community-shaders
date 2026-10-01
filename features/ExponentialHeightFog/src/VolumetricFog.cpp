#include "VolumetricFog.h"
#include "ExponentialHeightFogMath.h"

#include "Render/CanonicalDepth.h"
#include "Render/Engine.h"
#include "Render/FrameProfiler.h"
#include "Render/RendererContext.h"
#include "Render/SharedData.h"
#include "Render/SharedDataLayout.h"
#include "Telemetry/Telemetry.h"
#include "TerrainShadows.h"
#include "Utils/CSUtil.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace cs::features::exponential_height_fog
{
	namespace
	{
		constexpr std::array<std::string_view, 4> kPassNames{
			"ExponentialHeightFog/depth", "ExponentialHeightFog/material",
			"ExponentialHeightFog/scattering", "ExponentialHeightFog/integration"
		};

		class ScatteringInputs
		{
		public:
			explicit ScatteringInputs(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_integrated = nullptr) : _context(a_context)
			{
				for (std::size_t i = 0; i < _slots.size(); ++i)
					_context->CSGetShaderResources(_slots[i], 1, _saved[i].put());
				auto* terrain = TerrainShadows::GetSingleton();
				for (const auto slot : _slots) {
					ID3D11ShaderResourceView* srv = slot == 19 ? a_integrated :
					                                             (slot == 60 && terrain->IsLoaded() ? terrain->GetShadowHeightSRV() : nullptr);
					_context->CSSetShaderResources(slot, 1, &srv);
				}
			}
			~ScatteringInputs()
			{
				for (std::size_t i = 0; i < _slots.size(); ++i) {
					auto* srv = _saved[i].get();
					_context->CSSetShaderResources(_slots[i], 1, &srv);
				}
			}

		private:
			static constexpr std::array<UINT, 6> _slots{ 19, 50, 60, 76, 77, 98 };
			ID3D11DeviceContext* _context;
			std::array<winrt::com_ptr<ID3D11ShaderResourceView>, _slots.size()> _saved;
		};
	}

	bool VolumetricFog::Initialize(ID3D11Device* a_device)
	{
		_device.copy_from(a_device);
		D3D11_SAMPLER_DESC desc{};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.MaxAnisotropy = 1;
		desc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(a_device->CreateSamplerState(&desc, _linearSampler.put()));
		desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
		DX::ThrowIfFailed(a_device->CreateSamplerState(&desc, _shadowSampler.put()));
		_constants = std::make_unique<buffer::ConstantBuffer>(buffer::ConstantBufferDesc<Constants>());
		_constants->SetName("ExponentialHeightFog/VolumetricFogCB");
		_cameraConstants = std::make_unique<buffer::ConstantBuffer>(buffer::ConstantBufferDesc<render::FrameDataCB>());
		_cameraConstants->SetName("ExponentialHeightFog/Camera");
		const wchar_t* files[]{
			L"VolumetricFogConservativeDepthCS.hlsl", L"VolumetricFogMaterialCS.hlsl",
			L"VolumetricFogLightScatteringCS.hlsl", L"VolumetricFogIntegrationCS.hlsl"
		};
		for (std::size_t i = 0; i < _shaders.size(); ++i) {
			const std::wstring path = std::wstring(L"Data\\Shaders\\ExponentialHeightFog\\") + files[i];
			std::vector<std::pair<const char*, const char*>> defines;
			if (i == 2 && TerrainShadows::GetSingleton()->IsLoaded())
				defines.emplace_back("TERRAIN_SHADOWS", "1");
			_shaders[i].attach(static_cast<ID3D11ComputeShader*>(util::CompileShader(path.c_str(), defines, "cs_5_0")));
			if (!_shaders[i])
				return false;
			render::annotation::SetName(_shaders[i].get(), "ExponentialHeightFog/Pass" + std::to_string(i));
		}
		_skyShader.attach(static_cast<ID3D11ComputeShader*>(util::CompileShader(
			L"Data\\Shaders\\FO4\\ExponentialHeightFog\\SkyCompositeCS.hlsl", {}, "cs_5_0")));
		return _skyShader != nullptr;
	}

	void VolumetricFog::Reset()
	{
		_material.reset();
		_scattering.reset();
		_history.reset();
		_integrated.reset();
		_depth.reset();
		_depthHistory.reset();
		_grid = {};
		_hasHistory = _hasDepthHistory = false;
		_lastFrame = UINT32_MAX;
	}

	void VolumetricFog::PrepareSky()
	{
		_skyReady = false;
		auto* target = engine::ResolveRenderTarget(engine::RenderTarget::kMainTemp);
		if (!target || !target->texture || !_skyShader)
			return;
		auto* texture = reinterpret_cast<ID3D11Texture2D*>(target->texture);
		auto* nativeView = engine::GetRenderTargetSRV(engine::RenderTarget::kMainTemp);
		if (!nativeView)
			return;
		D3D11_SHADER_RESOURCE_VIEW_DESC nativeSRV{};
		nativeView->GetDesc(&nativeSRV);
		if (nativeSRV.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
			return;
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		if (!_skySource || _skySource->desc.Width != desc.Width || _skySource->desc.Height != desc.Height ||
			_skySource->desc.Format != desc.Format) {
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			desc.CPUAccessFlags = desc.MiscFlags = 0;
			auto source = std::make_unique<buffer::Texture2D>(desc);
			auto output = std::make_unique<buffer::Texture2D>(desc);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			// FO4: native targets may be typeless; preserve their resource and typed view formats.
			srv.Format = nativeSRV.Format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			source->CreateSRV(srv);
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = nativeSRV.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			output->CreateUAV(uav);
			source->SetName("ExponentialHeightFog/SkySource", "ExponentialHeightFog/SkySource.SRV");
			output->SetName("ExponentialHeightFog/SkyOutput", {}, "ExponentialHeightFog/SkyOutput.UAV");
			_skySource = std::move(source);
			_skyOutput = std::move(output);
			++_skyAllocations;
		}
		_skyReady = true;
	}

	void VolumetricFog::CompositeSky(ID3D11DeviceContext* a_context)
	{
		const auto camera = engine::GetWorldCameraRecord();
		const auto* graphics = engine::GetGraphicsState();
		if (!_skyReady || !camera || !graphics || graphics->frameCount == _lastSkyFrame)
			return;
		render::annotation::ScopedEvent event("ExponentialHeightFog/sky");
		if (!UpdateCamera(*camera, _previousRatio))
			return;
		auto* target = engine::ResolveRenderTarget(engine::RenderTarget::kMainTemp);
		auto* texture = reinterpret_cast<ID3D11Texture2D*>(target->texture);
		engine::CopyResourcePreservingOM(a_context, _skySource->resource.get(), texture);
		{
			engine::ComputeOMScope scope(a_context, 1, 1, 1, 0);
			render::ScopedComputeSharedDataBinding shared(a_context);
			if (!shared.IsActive())
				return;
			ScatteringInputs inputs(a_context, Integrated());
			auto* cameraBuffer = _cameraConstants->CB();
			a_context->CSSetConstantBuffers(render::kFrameDataSlot, 1, &cameraBuffer);
			auto* source = _skySource->srv.get();
			auto* output = _skyOutput->uav.get();
			auto* sampler = _linearSampler.get();
			a_context->CSSetShaderResources(0, 1, &source);
			a_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			a_context->CSSetSamplers(0, 1, &sampler);
			a_context->CSSetShader(_skyShader.get(), nullptr, 0);
			a_context->Dispatch((_skySource->desc.Width + 7) / 8, (_skySource->desc.Height + 7) / 8, 1);
		}
		engine::CopyResourcePreservingOM(a_context, texture, _skyOutput->resource.get());
		_lastSkyFrame = graphics->frameCount;
		++_skyDispatches;
	}

	void VolumetricFog::Prepare(const Settings& a_settings, std::uint32_t a_width, std::uint32_t a_height)
	{
		if (a_settings.enabled)
			PrepareSky();
		else
			_skyReady = false;
		if (!a_settings.enabled || !a_settings.volumetricFogEnabled || a_settings.volumetricFogExtinctionScale <= 0) {
			Reset();
			return;
		}
		auto pixelSize = std::clamp(a_settings.volumetricGridPixelSize, 4u, 64u);
		const auto gridZ = std::clamp(a_settings.volumetricGridSizeZ, 16u, 160u);
		const auto gridFor = [&](std::uint32_t a_pixelSize) {
			return DirectX::XMUINT3{ std::max(1u, (a_width + a_pixelSize - 1) / a_pixelSize),
				std::max(1u, (a_height + a_pixelSize - 1) / a_pixelSize), gridZ };
		};
		auto grid = gridFor(pixelSize);
		while (pixelSize < 64 && static_cast<std::uint64_t>(grid.x) * grid.y * grid.z > 16ull * 1024 * 1024)
			grid = gridFor(++pixelSize);
		if (_material && _grid.x == grid.x && _grid.y == grid.y && _grid.z == grid.z)
			return;
		Reset();
		D3D11_TEXTURE3D_DESC volume{};
		volume.Width = grid.x;
		volume.Height = grid.y;
		volume.Depth = grid.z;
		volume.MipLevels = 1;
		volume.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		volume.Usage = D3D11_USAGE_DEFAULT;
		volume.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		const auto makeVolume = [&](const char* a_name, bool a_writable = true) {
			auto result = std::make_unique<buffer::Texture3D>(_device.get(), volume, a_writable);
			result->SetName(a_name);
			return result;
		};
		_material = makeVolume("ExponentialHeightFog/VBufferA");
		_scattering = makeVolume("ExponentialHeightFog/LightScattering");
		_history = makeVolume("ExponentialHeightFog/LightScatteringHistory", false);
		_integrated = makeVolume("ExponentialHeightFog/IntegratedLightScattering");
		D3D11_TEXTURE2D_DESC depth{};
		depth.Width = grid.x;
		depth.Height = grid.y;
		depth.MipLevels = depth.ArraySize = 1;
		depth.Format = DXGI_FORMAT_R32_FLOAT;
		depth.SampleDesc.Count = 1;
		depth.Usage = D3D11_USAGE_DEFAULT;
		depth.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = depth.Format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		_depth = std::make_unique<buffer::Texture2D>(depth);
		_depth->CreateSRV(srv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = depth.Format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		_depth->CreateUAV(uav);
		_depth->SetName("ExponentialHeightFog/ConservativeDepth", "ExponentialHeightFog/ConservativeDepth.SRV");
		_depthHistory = std::make_unique<buffer::Texture2D>(depth);
		_depthHistory->CreateSRV(srv);
		_depthHistory->SetName("ExponentialHeightFog/ConservativeDepthHistory", "ExponentialHeightFog/ConservativeDepthHistory.SRV");
		_grid = grid;
		++_volumeAllocations;
	}

	bool VolumetricFog::Dispatch(ID3D11DeviceContext* a_context, const Settings& a_settings,
		const engine::WorldCameraRecord& a_camera, std::uint32_t a_frame, bool a_temporal)
	{
		if (!a_settings.enabled || !a_settings.volumetricFogEnabled || !_material)
			return true;
		if (a_settings.fogDensity <= 0) {
			_hasHistory = _hasDepthHistory = false;
			_lastFrame = UINT32_MAX;
			return true;
		}
		auto* depth = render::GetCanonicalSceneDepthSRV();
		const bool history = a_temporal && _hasHistory && _lastFrame != UINT32_MAX && a_frame == _lastFrame + 1u;
		Constants cb{};
		cb.gridSizeAndFlags = { _grid.x, _grid.y, _grid.z,
			(depth ? 2u : 0u) | (depth && history && _hasDepthHistory ? 16u : 0u) };
		cb.invGridSizeAndNearFade = { 1.0f / static_cast<float>(_grid.x), 1.0f / static_cast<float>(_grid.y), 1.0f / static_cast<float>(_grid.z),
			a_settings.volumetricFogNearFadeInDistance > 0 ? 1.0f / a_settings.volumetricFogNearFadeInDistance : 100000000.0f };
		const auto cameraData = engine::GetCameraDepthParameters(a_camera);
		const auto gridZ = GridZParameters(cameraData.y, a_settings.volumetricFogStartDistance,
			a_settings.volumetricFogDistance, a_settings.volumetricDepthDistributionScale, _grid.z);
		// FO4: fail closed on nonfinite dispatch data without changing the pinned slice formula.
		if (!std::ranges::all_of(gridZ, [](float value) { return std::isfinite(value); }))
			return false;
		cb.gridZParams = { gridZ[0], gridZ[1], gridZ[2], gridZ[3] };
		DirectX::XMStoreFloat4x4(&cb.clipToWorld, DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr,
													  DirectX::XMLoadFloat4x4(&a_camera.ViewProjectionUnjittered))));
		for (std::uint32_t i = 0; i < std::size(cb.frameJitterOffsets); ++i) {
			const auto frame = (a_frame - i) & 1023u;
			cb.frameJitterOffsets[i] = { a_temporal ? Halton(frame, 2) : 0.5f,
				a_temporal ? Halton(frame, 3) : 0.5f, a_temporal ? Halton(frame, 5) : 0.5f, 0 };
		}
		cb.historyParameters = { history ? std::clamp(a_settings.volumetricHistoryWeight, 0.0f, 0.99f) : 0,
			static_cast<float>(std::clamp(a_settings.volumetricHistoryMissSampleCount, 1u, 16u)), 0, 0 };
		cb.jitterParameters = { a_temporal ? std::max(a_settings.volumetricSampleJitterMultiplier, 0.0f) : 0,
			static_cast<float>(a_frame % 8), 0, 0 };
		const auto* manager = engine::GetRenderTargetManager();
		const DirectX::XMFLOAT2 ratio{ manager ? manager->GetDynamicWidthRatio() : 1.0f,
			manager ? manager->GetDynamicHeightRatio() : 1.0f };
		{
			render::annotation::ScopedEvent event("ExponentialHeightFog/upload");
			_constants->Update(cb);
			if (!UpdateCamera(a_camera, history ? _previousRatio : ratio))
				return false;
		}
		// Material consumers leave the integrated volume resident until its next producer.
		ID3D11ShaderResourceView* noVolume = nullptr;
		a_context->PSSetShaderResources(19, 1, &noVolume);
		engine::ComputeOMScope scope(a_context, 5, 2, 1, 1);
		render::ScopedComputeSharedDataBinding shared(a_context);
		if (!shared.IsActive())
			return false;
		ScatteringInputs inputs(a_context);
		ID3D11Buffer* cameraBuffer = _cameraConstants->CB();
		a_context->CSSetConstantBuffers(render::kFrameDataSlot, 1, &cameraBuffer);
		ID3D11Buffer* buffer = _constants->CB();
		a_context->CSSetConstantBuffers(0, 1, &buffer);
		ID3D11SamplerState* samplers[]{ _linearSampler.get(), _shadowSampler.get() };
		a_context->CSSetSamplers(0, 2, samplers);
		const auto dispatch = [&](std::size_t a_pass, ID3D11UnorderedAccessView* a_output, UINT a_groupsZ) {
			render::annotation::ScopedEvent event(kPassNames[a_pass]);
			a_context->CSSetUnorderedAccessViews(0, 1, &a_output, nullptr);
			a_context->CSSetShader(_shaders[a_pass].get(), nullptr, 0);
			a_context->Dispatch((_grid.x + 7) / 8, (_grid.y + 7) / 8, a_groupsZ);
			ID3D11UnorderedAccessView* empty = nullptr;
			a_context->CSSetUnorderedAccessViews(0, 1, &empty, nullptr);
		};
		if (depth)
			dispatch(0, _depth->uav.get(), 1);
		dispatch(1, _material->uav.get(), (_grid.z + 3) / 4);
		ID3D11ShaderResourceView* resources[]{ _material->srv.get(), nullptr,
			history ? _history->srv.get() : nullptr, _depth->srv.get(),
			history && _hasDepthHistory ? _depthHistory->srv.get() : nullptr };
		a_context->CSSetShaderResources(0, 5, resources);
		dispatch(2, _scattering->uav.get(), (_grid.z + 3) / 4);
		auto* scattering = _scattering->srv.get();
		a_context->CSSetShaderResources(0, 1, &scattering);
		dispatch(3, _integrated->uav.get(), 1);
		ID3D11ShaderResourceView* empty[5]{};
		a_context->CSSetShaderResources(0, 5, empty);
		if (a_temporal) {
			render::annotation::ScopedEvent event("ExponentialHeightFog/history_copy");
			a_context->CopyResource(_history->resource.get(), _scattering->resource.get());
			if (depth)
				a_context->CopyResource(_depthHistory->resource.get(), _depth->resource.get());
		}
		_hasHistory = a_temporal;
		_hasDepthHistory = a_temporal && depth;
		_lastFrame = a_frame;
		_previousRatio = ratio;
		_temporalEnabled = a_temporal;
		++_volumeFrames;
		if (history)
			++_historyFrames;
		return true;
	}

	void VolumetricFog::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		a_sink.Field("volume_allocations", _volumeAllocations)
			.Field("sky_allocations", _skyAllocations)
			.Field("volume_frames", _volumeFrames)
			.Field("history_frames", _historyFrames)
			.Field("temporal_enabled", _temporalEnabled)
			.Field("sky_dispatches", _skyDispatches);
		std::uint32_t timingPasses = 0;
		constexpr std::string_view prefix = "ExponentialHeightFog/";
		for (const auto& result : render::profiling::GetProfiler().GetResults()) {
			if (!result.valid || !result.name.starts_with(prefix))
				continue;
			const auto pass = result.name.substr(prefix.size());
			a_sink.Field(pass + "_gpu_ms", result.gpuTimeMs)
				.Field(pass + "_cpu_ms", result.cpuTimeMs);
			++timingPasses;
		}
		a_sink.Field("timing_passes", timingPasses);
	}

	bool VolumetricFog::UpdateCamera(const engine::WorldCameraRecord& a_camera, const DirectX::XMFLOAT2& a_previousRatio)
	{
		const auto* manager = engine::GetRenderTargetManager();
		const DirectX::XMFLOAT2 ratio{ manager ? manager->GetDynamicWidthRatio() : 1.0f,
			manager ? manager->GetDynamicHeightRatio() : 1.0f };
		const auto* target = engine::ResolveRenderTarget(engine::RenderTarget::kMainTemp);
		if (!target || !target->texture || ratio.x <= 0 || ratio.y <= 0)
			return false;
		D3D11_TEXTURE2D_DESC desc{};
		reinterpret_cast<ID3D11Texture2D*>(target->texture)->GetDesc(&desc);
		const float size = static_cast<float>(desc.Width);
		const float clamp = REX::FModule::IsRuntimeOG() ? (std::trunc(size * ratio.x) - 1.0f) / size : ratio.x - 0.5f / size;
		// FO4: each CS boundary owns a fresh world-camera copy, never native b12.
		_cameraConstants->Update(render::PackFrameData(a_camera, ratio, a_previousRatio, ratio.x - clamp));
		return true;
	}
}
