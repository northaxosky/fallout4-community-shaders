#include "VolumetricShadows.h"
#include "Render/SharedDataLayout.h"

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <string>
#include <utility>

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/FrameProfiler.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/ShaderInjection.h"
#include "Render/SharedData.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.volumetricshadows");

		constexpr std::array kSkipNames{ "none", "unavailable", "disabled", "sun_cascades", "source", "shaders" };

		constexpr float kLit[4]{ 1.0f, 1.0f, 0.0f, 0.0f };
		constexpr std::uint32_t kBlurGroupSize = 128;
		constexpr std::uint32_t kMipCount = 2;
		constexpr const wchar_t* kPreviewPath = L"Data\\Shaders\\FO4\\VolumetricShadows\\ShadowPreviewCS.hlsl";

		std::size_t CascadeMip(auto a_cascade)
		{
			return a_cascade == decltype(a_cascade)::kCascade0 ? 1 : 0;
		}

		constexpr std::array<std::string_view, kMipCount> kDownsamplePasses{ "VolumetricShadows::DownsampleMip0", "VolumetricShadows::DownsampleMip1" };
		constexpr std::array<std::array<std::string_view, 2>, kMipCount> kBlurPasses{ { { "VolumetricShadows::BlurHMip0", "VolumetricShadows::BlurVMip0" },
			{ "VolumetricShadows::BlurHMip1", "VolumetricShadows::BlurVMip1" } } };

		// A group covers 16 source texels per axis; a 32x reduction gathers 2x2 quads.
		std::uint32_t DownsampleGroups(std::uint32_t a_sourceSize, std::uint32_t a_mapSize)
		{
			return a_sourceSize / 16 / std::max(a_sourceSize / a_mapSize / 16, 1u);
		}

		winrt::com_ptr<ID3D11ComputeShader> CompileCompute(const wchar_t* a_path, const char* a_define = nullptr)
		{
			winrt::com_ptr<ID3D11ComputeShader> shader;
			const auto defines = a_define ? std::vector<std::pair<const char*, const char*>>{ { a_define, "1" } } : std::vector<std::pair<const char*, const char*>>{};
			shader.attach(static_cast<ID3D11ComputeShader*>(cs::util::CompileShader(a_path, defines, "cs_5_0")));
			if (!shader)
				L->warn("{} failed to compile.", a_define ? a_define : "ShadowPreviewCS");
			return shader;
		}
	}

	VolumetricShadows* VolumetricShadows::GetSingleton()
	{
		static VolumetricShadows instance;
		return &instance;
	}

	void VolumetricShadows::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings("VolumetricShadows", *this, [this](cs::engine::ShaderReplacementRegistration& registration) {
				registration.bind = [this, target = registration.targetId](ID3D11DeviceContext* a_context) { BindConsumer(a_context, target); };
			})) {
			FailLoad("Volumetric Shadows shader contribution registration failed.");
			return;
		}
		if (!cs::engine::AcquireSunShadowLights<2>()) {
			FailLoad("Volumetric Shadows needs the shared sun shadow light data");
			return;
		}
		if (!cs::engine::RegisterPostSunShadowRender(*this, [this] { Produce(); })) {
			FailLoad("Volumetric Shadows needs the main sun shadow render hook");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);
	}

	bool VolumetricShadows::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the shader contribution did not register";
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error = "the shared substrate is unavailable, so the consumers read no camera data";
			return false;
		}
		if (!cs::engine::ValidateShaderInjectionRoutes("VolumetricShadows", a_error))
			return false;
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	VolumetricShadows::MipChain VolumetricShadows::CreateMipChain(ID3D11Device* a_device, std::string_view a_name)
	{
		MipChain chain;
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = kMapSize;
		desc.Height = kMapSize;
		desc.MipLevels = kMipCount;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R16G16_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
		DX::ThrowIfFailed(a_device->CreateTexture2D(&desc, nullptr, chain.texture.put()));
		cs::render::annotation::SetName(chain.texture.get(), a_name);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = kMipCount;
		DX::ThrowIfFailed(a_device->CreateShaderResourceView(chain.texture.get(), &srvDesc, chain.srv.put()));
		cs::render::annotation::SetName(chain.srv.get(), std::format("{}.SRV", a_name));

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = desc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		for (std::uint32_t mip = 0; mip < kMipCount; ++mip) {
			srvDesc.Texture2D.MostDetailedMip = mip;
			srvDesc.Texture2D.MipLevels = 1;
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(chain.texture.get(), &srvDesc, chain.mipSRV[mip].put()));
			cs::render::annotation::SetName(chain.mipSRV[mip].get(), std::format("{}.SRV.Mip{}", a_name, mip));
			uavDesc.Texture2D.MipSlice = mip;
			DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(chain.texture.get(), &uavDesc, chain.mipUAV[mip].put()));
			cs::render::annotation::SetName(chain.mipUAV[mip].get(), std::format("{}.UAV.Mip{}", a_name, mip));
		}
		return chain;
	}

	void VolumetricShadows::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		_map = CreateMipChain(a_device, "VolumetricShadows/ShadowCopy");
		_blurTemp = CreateMipChain(a_device, "VolumetricShadows/ShadowBlurTemp");

		_downsampleMip0 = CompileCompute(L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl", "DOWNSAMPLE_SHADOW_MIP0");
		_downsampleMip1 = CompileCompute(L"Data\\Shaders\\VolumetricShadows\\DownsampleShadowCS.hlsl", "DOWNSAMPLE_SHADOW_MIP1");
		_blurHorizontal = CompileCompute(L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl", "BLUR_HORIZONTAL");
		_blurVertical = CompileCompute(L"Data\\Shaders\\VolumetricShadows\\BlurShadowCS.hlsl", "BLUR_VERTICAL");
		_previewCS = CompileCompute(kPreviewPath);
		for (std::size_t mip = 0; mip < _preview.size(); ++mip)
			_preview[mip].Create(a_device, kMapSize >> mip, kMapSize >> mip, std::format("VolumetricShadows/Preview{}", mip));

		// Consumers read the map from the first draw, before any sun render.
		winrt::com_ptr<ID3D11DeviceContext> context;
		a_device->GetImmediateContext(context.put());
		_mapsLit = false;
		ClearToLit(context.get());
		L->info("shadow map: {0}x{0} R16G16_UNORM, {1} mips", kMapSize, kMipCount);
	}

	bool VolumetricShadows::EnsureSourceView(ID3D11Device* a_device, const cs::engine::SunCascadeSnapshot& a_snapshot)
	{
		D3D11_TEXTURE2D_DESC source{};
		a_snapshot.texture->GetDesc(&source);
		// The map's 512 mip reduces 2x to 16x; the 256 mip takes up to 32x.
		if (source.Format != DXGI_FORMAT_R16_TYPELESS || source.ArraySize != a_snapshot.count || source.MipLevels != 1 || source.SampleDesc.Count != 1 ||
			!(source.BindFlags & D3D11_BIND_SHADER_RESOURCE) || source.Width != source.Height || source.Width < 2 * kMapSize || source.Width > 16 * kMapSize || !std::has_single_bit(source.Width))
			return false;
		if (_sourceSRV && _sourceTexture == a_snapshot.texture && _sourceDesc.Width == source.Width && _sourceDesc.ArraySize == source.ArraySize)
			return true;

		const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
			.Format = DXGI_FORMAT_R16_UNORM,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
			.Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = 0, .ArraySize = source.ArraySize }
		};
		_sourceSRV = nullptr;
		_sourceTexture = nullptr;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		if (FAILED(a_device->CreateShaderResourceView(a_snapshot.texture, &srvDesc, srv.put())))
			return false;
		cs::render::annotation::SetName(srv.get(), "VolumetricShadows/SunCascades.SRV");
		_sourceSRV = std::move(srv);
		_sourceTexture = a_snapshot.texture;
		_sourceDesc = source;
		L->info("sun cascades: {}x{}x{} R16_TYPELESS, SRV R16_UNORM", source.Width, source.Height, source.ArraySize);
		return true;
	}

	void VolumetricShadows::ClearToLit(ID3D11DeviceContext* a_context)
	{
		if (_mapsLit || !a_context || !_map.texture)
			return;
		for (const auto& uav : _map.mipUAV)
			a_context->ClearUnorderedAccessViewFloat(uav.get(), kLit);
		_mapsLit = true;
	}

	void VolumetricShadows::Dispatch(ID3D11DeviceContext* a_context, std::string_view a_pass, ID3D11ComputeShader* a_shader, ID3D11ShaderResourceView* a_input, ID3D11UnorderedAccessView* a_output, std::uint32_t a_groupsX, std::uint32_t a_groupsY)
	{
		cs::render::annotation::ScopedEvent event(a_pass);
		// FO4: no ESRAM shadow map, so t1 repeats the input and the min is a no-op.
		const std::array<ID3D11ShaderResourceView*, 2> srvs{ a_input, a_input };
		const std::array<ID3D11ShaderResourceView*, 2> noSrvs{};
		ID3D11UnorderedAccessView* const noUav = nullptr;
		a_context->CSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
		a_context->CSSetUnorderedAccessViews(0, 1, &a_output, nullptr);
		a_context->CSSetShader(a_shader, nullptr, 0);
		a_context->Dispatch(a_groupsX, a_groupsY, 1);
		a_context->CSSetShaderResources(0, static_cast<UINT>(noSrvs.size()), noSrvs.data());
		a_context->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
	}

	void VolumetricShadows::Blur(ID3D11DeviceContext* a_context, std::size_t a_mip)
	{
		const std::uint32_t size = kMapSize >> a_mip;
		const std::uint32_t groups = (size + kBlurGroupSize - 1) / kBlurGroupSize;
		Dispatch(a_context, kBlurPasses[a_mip][0], _blurHorizontal.get(), _map.mipSRV[a_mip].get(), _blurTemp.mipUAV[a_mip].get(), groups, size);
		Dispatch(a_context, kBlurPasses[a_mip][1], _blurVertical.get(), _blurTemp.mipSRV[a_mip].get(), _map.mipUAV[a_mip].get(), size, groups);
	}

	VolumetricShadows::Skip VolumetricShadows::DownsampleAndBlur(ID3D11DeviceContext* a_context)
	{
		auto* device = cs::engine::GetDevice();
		if (!IsHealthy() || !_map.texture || !device)
			return Skip::kUnavailable;
		if (!IsEnabled())
			return Skip::kDisabled;

		const auto& lights = cs::engine::GetSunShadowLightState();
		const auto& snapshot = lights.snapshot;
		_sunStatus = lights.status;
		if (_sunStatus == cs::engine::SunCascadeStatus::kOk && (!lights.published || !snapshot.texture))
			_sunStatus = cs::engine::SunCascadeStatus::kInvalid;
		if (_sunStatus != cs::engine::SunCascadeStatus::kOk)
			return Skip::kSunCascades;
		if (!EnsureSourceView(device, snapshot))
			return Skip::kSource;
		if (!(_downsampleMip0 && _downsampleMip1 && _blurHorizontal && _blurVertical))
			return Skip::kShaders;

		// The engine may still have the cascade array bound as a depth target.
		cs::engine::ComputeOMScope scope(a_context, 2, 1, 1, 0);
		ID3D11SamplerState* const sampler = cs::render::GetLinearClampSampler();
		a_context->CSSetSamplers(0, 1, &sampler);

		// FO4: a single cascade leaves cascade 1 without a slice, so it reads lit.
		const bool hasCascade1 = snapshot.count > 1;
		if (hasCascade1) {
			const std::uint32_t groups = DownsampleGroups(_sourceDesc.Width, kMapSize);
			Dispatch(a_context, kDownsamplePasses[0], _downsampleMip0.get(), _sourceSRV.get(), _map.mipUAV[0].get(), groups, groups);
		} else
			a_context->ClearUnorderedAccessViewFloat(_map.mipUAV[0].get(), kLit);
		const std::uint32_t groups1 = DownsampleGroups(_sourceDesc.Width, kMapSize >> 1);
		Dispatch(a_context, kDownsamplePasses[1], _downsampleMip1.get(), _sourceSRV.get(), _map.mipUAV[1].get(), groups1, groups1);
		if (hasCascade1)
			Blur(a_context, 0);
		Blur(a_context, 1);

		_mapsLit = false;
		_counters.cascadeCount.store(snapshot.count, std::memory_order_relaxed);
		_counters.sourceWidth.store(_sourceDesc.Width, std::memory_order_relaxed);
		_counters.sourceHeight.store(_sourceDesc.Height, std::memory_order_relaxed);
		return Skip::kNone;
	}

	void VolumetricShadows::Produce()
	{
		auto* context = cs::engine::GetImmediateContext();
		const auto skip = context ? DownsampleAndBlur(context) : Skip::kUnavailable;
		if (skip == Skip::kNone) {
			_counters.productions.fetch_add(1, std::memory_order_relaxed);
		} else {
			// A skipped frame must not leave the previous cascades in the map.
			ClearToLit(context);
		}
		const bool forwarded = skip == Skip::kSunCascades;
		const auto name = forwarded ? cs::engine::SunCascadeStatusName(_sunStatus) : kSkipNames[static_cast<std::size_t>(skip)];
		if (skip != Skip::kNone) {
			auto& counter = forwarded ? _counters.sunStatusSkipped[static_cast<std::size_t>(_sunStatus)] : _counters.skipped[static_cast<std::size_t>(skip)];
			counter.fetch_add(1, std::memory_order_relaxed);
		}
		if (std::exchange(_loggedSkip, name) != name)
			L->info("shadow map: {}", name);
		RenderDebugPreview(context);
	}

	void VolumetricShadows::RenderDebugPreview(ID3D11DeviceContext* a_context)
	{
		const auto cascade = _debugCascade.load(std::memory_order_acquire);
		if (cascade == DebugCascade::kNone || !a_context || !_previewCS)
			return;
		const std::size_t mip = CascadeMip(cascade);
		cs::render::annotation::ScopedEvent event("VolumetricShadows::Preview");
		_preview[mip].Convert(a_context, _previewCS.get(), _map.mipSRV[mip].get());
	}

	void VolumetricShadows::BindConsumer(ID3D11DeviceContext* a_context, cs::engine::ShaderInjectionTarget a_target)
	{
		// Disabled: ClearState nulls t98 each frame, so lobes stay native.
		if (!a_context || !IsHealthy() || !_injectionsOperational.load(std::memory_order_acquire) || !IsEnabled() || !_map.srv)
			return;
		// Skylighting variants still read the VSM while Skylighting is disabled.
		if (a_target == cs::engine::ShaderInjectionTarget::kBsdfLight && !cs::engine::ActiveLightHasSunShadowedFamily())
			return;
		ID3D11ShaderResourceView* const map = _map.srv.get();
		ID3D11ShaderResourceView* const lights = cs::engine::GetSunShadowLightsSRV<2>();
		cs::engine::BindInjectionShaderResources(a_context, volumetric_shadows_shader::kSharedShadowMapSlot, 1, &map);
		cs::engine::BindInjectionShaderResources(a_context, volumetric_shadows_shader::kShadowLightsSlot, 1, &lights);
		(a_target == cs::engine::ShaderInjectionTarget::kBsWater   ? _counters.waterDraws :
			a_target == cs::engine::ShaderInjectionTarget::kEffect ? _counters.effectDraws :
																	 _counters.lightDraws)
			.fetch_add(1, std::memory_order_relaxed);
	}

	std::span<const FeatureDebugView> VolumetricShadows::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "cascade_0",
				.label = "VSM cascade 0",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const VolumetricShadows&>(a_feature).GetCascadeDebugTexture(DebugCascade::kCascade0);
				} },
			FeatureDebugView{ .id = "cascade_1", .label = "VSM cascade 1", .kind = FeatureDebugViewKind::kTexturePreview, .textureProvider = [](const Feature& a_feature) {
								 return static_cast<const VolumetricShadows&>(a_feature).GetCascadeDebugTexture(DebugCascade::kCascade1);
							 } }
		};
		return views;
	}

	void VolumetricShadows::SetDebugView(std::string_view a_view) noexcept
	{
		for (auto& preview : _preview)
			preview.Invalidate();
		_debugCascade.store(
			a_view == "cascade_0" ? DebugCascade::kCascade0 :
			a_view == "cascade_1" ? DebugCascade::kCascade1 :
									DebugCascade::kNone,
			std::memory_order_release);
	}

	FeatureDebugTexture VolumetricShadows::GetCascadeDebugTexture(DebugCascade a_cascade) const
	{
		FeatureDebugTexture texture{ .unavailableText = "Shadow map not allocated." };
		if (_debugCascade.load(std::memory_order_acquire) != a_cascade || !_map.texture)
			return texture;
		return _preview[CascadeMip(a_cascade)].GetDebugTexture("Shadow map preview not populated.");
	}

	void VolumetricShadows::WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB& a_fo4)
	{
		a_fo4.EnabledVolumetricShadows = _injectionsOperational.load(std::memory_order_acquire) ? 1u : 0u;
	}

	void VolumetricShadows::DrawSettings()
	{
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void VolumetricShadows::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto count = [](const std::atomic<std::uint64_t>& a_value) { return a_value.load(std::memory_order_relaxed); };
		a_sink
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("productions", count(_counters.productions));
		for (std::size_t skip = static_cast<std::size_t>(Skip::kUnavailable); skip < _counters.skipped.size(); ++skip) {
			if (skip != static_cast<std::size_t>(Skip::kSunCascades))
				a_sink.Field(std::format("skipped_{}", kSkipNames[skip]), count(_counters.skipped[skip]));
		}
		for (std::size_t status = static_cast<std::size_t>(cs::engine::SunCascadeStatus::kNoLight); status < _counters.sunStatusSkipped.size(); ++status)
			a_sink.Field(std::format("skipped_{}", cs::engine::SunCascadeStatusName(static_cast<cs::engine::SunCascadeStatus>(status))), count(_counters.sunStatusSkipped[status]));
		a_sink
			.Field("cascade_count", _counters.cascadeCount.load(std::memory_order_relaxed))
			.Dimensions("source", _counters.sourceWidth.load(std::memory_order_relaxed), _counters.sourceHeight.load(std::memory_order_relaxed))
			.Field("dflight_draws_bound", count(_counters.lightDraws))
			.Field("water_draws_bound", count(_counters.waterDraws))
			.Field("effect_draws_bound", count(_counters.effectDraws));
		cs::render::profiling::CollectPassTimings(a_sink, "VolumetricShadows::");
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(VolumetricShadows::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
