#include "ScreenSpaceGI.h"

#include <DearModdingUI/Client.h>
#include <DirectXTex.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <vector>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Menu.h"
#include "Menu/SettingsEdit.h"
#include "Render/Annotation.h"
#include "Render/CanonicalDepth.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/RenderHooks.h"
#include "Render/RendererContext.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderVariantRuntimeResolver.h"
#include "Render/SharedData.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.screenspacegi");
		using RT = cs::engine::RenderTarget;
		using Texture = std::unique_ptr<cs::buffer::Texture2D>;

		Texture CreateTexture(UINT a_width, UINT a_height, DXGI_FORMAT a_format,
			std::string_view a_name, UINT a_mips = 1)
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = a_mips;
			desc.ArraySize = 1;
			desc.Format = a_format;
			desc.SampleDesc.Count = 1;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			auto result = std::make_unique<cs::buffer::Texture2D>(desc);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = a_format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = a_mips;
			result->CreateSRV(srv);
			if (a_mips == 1) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.Format = a_format;
				uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				result->CreateUAV(uav);
			}
			result->SetName(a_name, std::format("{}.SRV", a_name), std::format("{}.UAV", a_name));
			return result;
		}

		// FO4: preserve t8-t9 and b10 without widening the engine low-slot cleanup.
		class Passes
		{
		public:
			Passes(ID3D11DeviceContext* a_context, UINT a_consumerSlot, ID3D11Buffer* a_consumer) :
				_context(a_context), _consumerSlot(a_consumerSlot)
			{
				_context->CSGetConstantBuffers(_consumerSlot, 1, _consumer.put());
				_context->CSSetConstantBuffers(_consumerSlot, 1, &a_consumer);
				ID3D11ShaderResourceView* tail[2]{};
				_context->CSGetShaderResources(8, 2, tail);
				for (UINT i = 0; i < 2; ++i)
					_tail[i].attach(tail[i]);
				ID3D11ShaderResourceView* nullSRVs[8]{};
				ID3D11UnorderedAccessView* nullUAVs[6]{};
				_context->CSSetShaderResources(0, 8, nullSRVs);
				_context->CSSetUnorderedAccessViews(0, 6, nullUAVs, nullptr);
			}
			~Passes()
			{
				ID3D11ShaderResourceView* tail[]{ _tail[0].get(), _tail[1].get() };
				_context->CSSetShaderResources(8, 2, tail);
				auto* consumer = _consumer.get();
				_context->CSSetConstantBuffers(_consumerSlot, 1, &consumer);
			}
			void Run(ID3D11ComputeShader* a_shader,
				std::initializer_list<ID3D11ShaderResourceView*> a_srvs,
				std::initializer_list<ID3D11UnorderedAccessView*> a_uavs,
				UINT a_width, UINT a_height, UINT a_group, std::string_view a_name)
			{
				cs::render::annotation::ScopedEvent event(a_name);
				_context->CSSetShaderResources(0, static_cast<UINT>(a_srvs.size()), a_srvs.begin());
				_context->CSSetUnorderedAccessViews(0, static_cast<UINT>(a_uavs.size()), a_uavs.begin(), nullptr);
				_context->CSSetShader(a_shader, nullptr, 0);
				_context->Dispatch((a_width + a_group - 1) / a_group, (a_height + a_group - 1) / a_group, 1);
				ID3D11ShaderResourceView* nullSRVs[10]{};
				ID3D11UnorderedAccessView* nullUAVs[6]{};
				_context->CSSetShaderResources(0, static_cast<UINT>(a_srvs.size()), nullSRVs);
				_context->CSSetUnorderedAccessViews(0, static_cast<UINT>(a_uavs.size()), nullUAVs, nullptr);
			}

		private:
			ID3D11DeviceContext* _context;
			UINT _consumerSlot;
			winrt::com_ptr<ID3D11Buffer> _consumer;
			std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> _tail;
		};
	}

	ScreenSpaceGI* ScreenSpaceGI::GetSingleton()
	{
		static ScreenSpaceGI instance;
		return &instance;
	}

	bool ScreenSpaceGI::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(ssgi_settings::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_recompile = true;
		_liveSettings = settings::BindLiveSettings(ssgi_settings::kSchema, _settings,
			[this] { _recompile = true; QueueReset("live_settings"); });
		return true;
	}

	bool ScreenSpaceGI::SaveSettings()
	{
		return settings::SaveDelta(ssgi_settings::kSchema, GetConfigKey(), _settings, *L);
	}

	void ScreenSpaceGI::Load()
	{
		if (!cs::engine::RegisterFeatureShaderBindings("ScreenSpaceGI", *this)) {
			FailLoad("SSGI shader contribution registration failed.");
			return;
		}
		cs::engine::RegisterPostDeferredLightsImpl([] { GetSingleton()->OnPostDeferredLights(); GetSingleton()->Prepass(); });
		if (!cs::engine::RegisterPostDeferredPrePass([] { GetSingleton()->ApplyVanillaSSAO(); }))
			L->warn("Vanilla SSAO control could not register; the engine's AO stays as configured.");
		_started = true;
	}

	void ScreenSpaceGI::OnDataLoaded()
	{
		if (auto* ui = RE::UI::GetSingleton())
			ui->RegisterSink<RE::MenuOpenCloseEvent>(this);
	}

	RE::BSEventNotifyControl ScreenSpaceGI::ProcessEvent(
		const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (a_event.menuName == RE::LoadingMenu::MENU_NAME && !a_event.opening)
			QueueReset("loading_screen_closed");
		return RE::BSEventNotifyControl::kContinue;
	}

	void ScreenSpaceGI::ApplyVanillaSSAO()
	{
		const auto state = cs::engine::GetScalableAOComputeState();
		if (!state)
			return;
		if (!_vanillaSSAOSnapshot)
			_vanillaSSAOSnapshot = *state->applied;
		// FO4: DrawModel and console commands rewrite both native AO bits.
		const bool applied = _settings.enableVanillaSSAO && *_vanillaSSAOSnapshot;
		*state->applied = applied;
		*state->active = _settings.enableVanillaSSAO && (*state->base || applied);
		_vanillaSSAOApplied = applied;
	}

	bool ScreenSpaceGI::CompileShaders()
	{
		constexpr const wchar_t* files[]{
			L"prefilterDepths.cs.hlsl", L"radianceDisocc.cs.hlsl", L"prefilterRadiance.cs.hlsl",
			L"prefilterNormal.cs.hlsl", L"gi.cs.hlsl", L"blur.cs.hlsl", L"upsample.cs.hlsl"
		};
		std::vector<std::pair<const char*, const char*>> defines;
		if (_settings.resolutionMode == 1)
			defines.emplace_back("HALF_RES", "");
		if (_settings.resolutionMode == 2)
			defines.emplace_back("QUARTER_RES", "");
		if (_settings.enableTemporalDenoiser)
			defines.emplace_back("TEMPORAL_DENOISER", "");
		if (_settings.enableGI)
			defines.emplace_back("GI", "");
		if (_settings.enableExperimentalSpecularGI)
			defines.emplace_back("GI_SPECULAR", "");
		_prepare = nullptr;
		_prepare.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(L"Data\\Shaders\\FO4\\ScreenSpaceGI\\Prepare.cs.hlsl", defines, "cs_5_0")));
		if (_prepare)
			cs::render::annotation::SetName(_prepare.get(), "SSGI/Prepare.CS");
		bool ready = static_cast<bool>(_prepare);
		for (std::size_t i = 0; i < _shaders.size(); ++i) {
			auto passDefines = defines;
			if (i == 0)
				passDefines.emplace_back("LINEAR_FILTER", "");
			const auto path = std::filesystem::path(L"Data\\Shaders\\ScreenSpaceGI") / files[i];
			_shaders[i] = nullptr;
			_shaders[i].attach(reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(path.c_str(), passDefines, "cs_5_0")));
			ready &= static_cast<bool>(_shaders[i]);
			if (_shaders[i])
				cs::render::annotation::SetName(_shaders[i].get(), std::format("SSGI/{}.CS", path.filename().string()));
		}
		_recompile = false;
		QueueReset("shader_configuration");
		return ready;
	}

	void ScreenSpaceGI::QueueReset(const char* a_reason) noexcept
	{
		_resetReason = a_reason;
		_queuedReset = true;
	}

	void ScreenSpaceGI::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_started || !a_device)
			return;
		try {
			_constants = std::make_unique<cs::buffer::ConstantBuffer>(cs::buffer::ConstantBufferDesc<SSGICB>());
			_consumer = std::make_unique<cs::buffer::ConstantBuffer>(cs::buffer::ConstantBufferDesc<ConsumerCB>());
			_consumerData.reset();
			_constants->SetName("SSGI/Constants.Buffer");
			_consumer->SetName("SSGI/Consumer.Buffer");
			DirectX::ScratchImage image;
			DX::ThrowIfFailed(DirectX::LoadFromDDSFile(L"Data\\Shaders\\ScreenSpaceGI\\fast_2uges.dds",
				DirectX::DDS_FLAGS_NONE, nullptr, image));
			DX::ThrowIfFailed(DirectX::CreateShaderResourceView(a_device, image.GetImages(),
				image.GetImageCount(), image.GetMetadata(), _noise.put()));
			cs::render::annotation::SetName(_noise.get(), "SSGI/Noise.SRV");
			D3D11_SAMPLER_DESC desc{};
			desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.MaxAnisotropy = 1;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			DX::ThrowIfFailed(a_device->CreateSamplerState(&desc, _pointSampler.put()));
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			DX::ThrowIfFailed(a_device->CreateSamplerState(&desc, _linearSampler.put()));
			cs::render::annotation::SetName(_pointSampler.get(), "SSGI/PointClamp.Sampler");
			cs::render::annotation::SetName(_linearSampler.get(), "SSGI/LinearClamp.Sampler");
			_failed = !CompileShaders() || !_prepare;
			EnsureResources();
		} catch (const std::exception& e) {
			_failed = true;
			L->error("SSGI initialization failed: {}", e.what());
		}
	}

	void ScreenSpaceGI::EnsurePrepareResources(Resources& a_resources, UINT a_width, UINT a_height)
	{
		const bool specular = _settings.enableExperimentalSpecularGI;
		if (!a_resources.normalGloss || a_resources.normalGlossSpecular != specular) {
			a_resources.normalGloss = CreateTexture(a_width, a_height,
				specular ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R16G16_FLOAT, "SSGI/NormalGloss");
			a_resources.normalGlossSpecular = specular;
		}
		if (_settings.enableGI) {
			if (!a_resources.diffuse)
				a_resources.diffuse = CreateTexture(a_width, a_height, DXGI_FORMAT_R11G11B10_FLOAT, "SSGI/Diffuse");
		} else {
			a_resources.diffuse.reset();
		}
		// Only GI_SPECULAR kernels touch the specular history.
		if (specular && !a_resources.specular[0]) {
			const float clear[4]{};
			auto* context = cs::engine::GetImmediateContext();
			for (UINT i = 0; i < 2; ++i) {
				a_resources.specular[i] = CreateTexture(a_width, a_height, DXGI_FORMAT_R16G16B16A16_FLOAT, std::format("SSGI/Specular[{}]", i));
				context->ClearUnorderedAccessViewFloat(a_resources.specular[i]->uav.get(), clear);
			}
		} else if (!specular) {
			a_resources.specular = {};
		}
	}

	bool ScreenSpaceGI::EnsureResources()
	{
		auto* state = cs::engine::GetGraphicsState();
		if (!state || !state->screenWidth || !state->screenHeight)
			return false;
		const UINT width = state->screenWidth, height = state->screenHeight;
		try {
			if (_resourcesReady && width == _width && height == _height) {
				EnsurePrepareResources(_textures, width, height);
				return true;
			}
			_resourcesReady = false;
			Resources resources;
			auto* device = cs::engine::GetDevice();
			const auto mip = [&](MipTexture& a_texture, DXGI_FORMAT a_format, const char* a_name) {
				a_texture.texture = CreateTexture(width, height, a_format, a_name, 5);
				for (UINT i = 0; i < 5; ++i) {
					D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
					desc.Format = a_format;
					desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
					desc.Texture2D.MipSlice = i;
					DX::ThrowIfFailed(device->CreateUnorderedAccessView(a_texture.texture->resource.get(), &desc, a_texture.uavs[i].put()));
					cs::render::annotation::SetName(a_texture.uavs[i].get(), std::format("{}[{}].UAV", a_name, i));
				}
			};
			mip(resources.depth, DXGI_FORMAT_R16_FLOAT, "SSGI/Depth");
			mip(resources.normals, DXGI_FORMAT_R8G8_UNORM, "SSGI/Normal");
			mip(resources.radiance, DXGI_FORMAT_R11G11B10_FLOAT, "SSGI/Radiance");
			EnsurePrepareResources(resources, width, height);
			resources.radianceTemp = CreateTexture(width, height, DXGI_FORMAT_R11G11B10_FLOAT, "SSGI/RadianceTemp");
			resources.previousGeometry = CreateTexture(width, height, DXGI_FORMAT_R11G11B10_FLOAT, "SSGI/PreviousGeometry");
			const auto pair = [&](TexturePair& a_pair, DXGI_FORMAT a_format, const char* a_name) {
				for (UINT i = 0; i < 2; ++i)
					a_pair[i] = CreateTexture(width, height, a_format, std::format("{}[{}]", a_name, i));
			};
			pair(resources.ao, DXGI_FORMAT_R8_UNORM, "SSGI/AO");
			pair(resources.accumulation, DXGI_FORMAT_R8_UNORM, "SSGI/Accumulation");
			pair(resources.luma, DXGI_FORMAT_R16G16B16A16_FLOAT, "SSGI/Luma");
			pair(resources.chroma, DXGI_FORMAT_R16G16_FLOAT, "SSGI/Chroma");
			_textures = std::move(resources);
			_width = width;
			_height = height;
			++_generation;
			_lastAO = _lastGI = _lastAccum = _outputAO = _outputGI = 0;
			QueueReset("resource_recreation");
			const float clear[4]{};
			auto* context = cs::engine::GetImmediateContext();
			cs::render::annotation::ScopedEvent timing("SSGI/ClearHistory");
			for (auto* textures : { &_textures.ao, &_textures.accumulation, &_textures.luma, &_textures.chroma })
				for (auto& texture : *textures)
					context->ClearUnorderedAccessViewFloat(texture->uav.get(), clear);
			context->ClearUnorderedAccessViewFloat(_textures.previousGeometry->uav.get(), clear);
			_resourcesReady = true;
			return true;
		} catch (const std::exception& e) {
			_failed = true;
			L->error("SSGI resource creation failed: {}", e.what());
			return false;
		}
	}

	void ScreenSpaceGI::UpdateConstants(const cs::engine::WorldCameraRecord& a_camera,
		UINT a_width, UINT a_height, UINT a_frame)
	{
		SSGICB data{};
		// FO4: b1 is column-major, so native row-vector storage supplies its transpose.
		data.PrevInvViewMat = _previousViewInverse;
		_previousViewInverse = a_camera.ViewInverse;
		data.NDCToViewMul = { 2.0f / a_camera.Projection._11, -2.0f / a_camera.Projection._22, 0, 0 };
		data.NDCToViewAdd = { -1.0f / a_camera.Projection._11, 1.0f / a_camera.Projection._22, 0, 0 };
		data.TexDim = { static_cast<float>(_width), static_cast<float>(_height) };
		data.RcpTexDim = { 1.0f / _width, 1.0f / _height };
		data.FrameDim = { static_cast<float>(a_width), static_cast<float>(a_height) };
		data.RcpFrameDim = { 1.0f / a_width, 1.0f / a_height };
		data.FrameIndex = a_frame;
		data.NumSlices = static_cast<UINT>(_settings.numSlices);
		data.NumSteps = static_cast<UINT>(_settings.numSteps);
		data.MinScreenRadius = _settings.minScreenRadius * a_width;
		data.EffectRadius = (std::max)(_settings.aoRadius, _settings.giRadius);
		data.AORadius = _settings.aoRadius / data.EffectRadius;
		data.GIRadius = _settings.giRadius / data.EffectRadius;
		data.Thickness = _settings.thickness;
		data.DepthFadeRange = { _settings.depthFadeRange[0], _settings.depthFadeRange[1] };
		data.DepthFadeScaleConst = 1.0f / (data.DepthFadeRange.y - data.DepthFadeRange.x);
		data.GISaturation = _settings.giSaturation;
		data.GIDistanceCompensation = _settings.giDistanceCompensation;
		data.GICompensationMaxDist = _settings.aoRadius;
		data.AOPower = _settings.aoPower;
		data.GIStrength = _settings.giStrength;
		data.DepthDisocclusion = _settings.depthDisocclusion;
		data.NormalDisocclusion = _settings.normalDisocclusion;
		data.MaxAccumFrames = static_cast<UINT>(_settings.maxAccumFrames);
		data.BlurRadius = _settings.blurRadius;
		data.DistanceNormalisation = _settings.distanceNormalisation;
		_constants->Update(data);
	}

	void ScreenSpaceGI::OnPostDeferredLights()
	{
		auto* state = cs::engine::GetGraphicsState();
		auto* context = cs::engine::GetImmediateContext();
		if (!_started || !state || !context)
			return;
		if (_hasFrame && _lastFrame == state->frameCount) {
			++_repeats;
			return;
		}
		_hasFrame = true;
		_lastFrame = state->frameCount;
		_produced = false;
		_binds = 0;
		_historyUsed = false;
		_cameraReady = false;
		_tiledAvailable = false;
		_motionAvailable = false;
		if (!_settings.enabled)
			return;
		if (_recompile && !CompileShaders()) {
			_failed = true;
			return;
		}
		if (!_prepare || !_noise || !_constants || !EnsureResources() ||
			std::ranges::any_of(_shaders, [](const auto& a_shader) { return !a_shader; }))
			return;
		const auto camera = cs::engine::GetWorldCameraRecord();
		_cameraReady = camera.has_value();
		const bool tiled = cs::engine::QueryTiledLightingEnabled();
		auto* manager = cs::engine::GetRenderTargetManager();
		auto* depth = cs::render::GetCanonicalSceneDepthSRV();
		auto* normals = cs::engine::GetRenderTargetSRV(RT::kGbufferNormal);
		auto* material = cs::engine::GetRenderTargetSRV(RT::kGbufferMaterial);
		auto* albedo = cs::engine::GetRenderTargetSRV(RT::kGbufferAlbedo);
		auto* emissive = cs::engine::GetRenderTargetSRV(RT::kGbufferEmissive);
		auto* diffuse = cs::engine::GetRenderTargetSRV(RT::kDiffuseBufferA);
		auto* diffuseB = cs::engine::GetRenderTargetSRV(RT::kDiffuseBufferB);
		auto* motion = cs::engine::GetRenderTargetSRV(RT::kMotionVectors);
		_motionAvailable = motion != nullptr;
		_tiledAvailable = diffuseB != nullptr;
		if (!camera || !manager || !depth || !normals || !motion ||
			(_settings.enableExperimentalSpecularGI && !material) ||
			(_settings.enableGI && (!albedo || !emissive || !diffuse || (tiled && !diffuseB))) ||
			!cs::render::IsSharedDataReady()) {
			QueueReset("missing_inputs");
			return;
		}
		_tiled = tiled;
		const auto origin = cs::engine::CameraWorldOrigin(*camera);
		const auto previous = cs::engine::CameraPreviousWorldOrigin(*camera);
		_origin[0] = origin.x;
		_origin[1] = origin.y;
		_origin[2] = origin.z;
		_previousOrigin[0] = previous.x;
		_previousOrigin[1] = previous.y;
		_previousOrigin[2] = previous.z;
		const UINT width = static_cast<UINT>(std::floor(_width * manager->GetDynamicWidthRatio()));
		const UINT height = static_cast<UINT>(std::floor(_height * manager->GetDynamicHeightRatio()));
		const UINT internalWidth = width >> _settings.resolutionMode;
		const UINT internalHeight = height >> _settings.resolutionMode;
		if (!internalWidth || !internalHeight)
			return;

		try {
			cs::engine::ComputeOMScope scope(context, 8, 2, 6, 2);
			cs::render::ScopedComputeSharedDataBinding substrate(context);
			const bool reset = _queuedReset.exchange(false);
			_historyUsed = !reset && _settings.enableTemporalDenoiser;
			if (reset) {
				cs::render::annotation::ScopedEvent timing("SSGI/ResetAccumulation");
				const float clear[4]{};
				for (auto& texture : _textures.accumulation)
					context->ClearUnorderedAccessViewFloat(texture->uav.get(), clear);
				++_resetCount;
			}
			UpdateConstants(*camera, width, height, state->frameCount);
			auto* cb = _constants->CB();
			context->CSSetConstantBuffers(1, 1, &cb);
			UpdateConsumer(true, tiled);
			ID3D11SamplerState* samplers[]{ _pointSampler.get(), _linearSampler.get() };
			context->CSSetSamplers(0, 2, samplers);
			Passes pass(context, kConsumerSlot, _consumer->CB());
			cs::render::annotation::ScopedEvent generation("ScreenSpaceGI/Generate", false);
			auto& t = _textures;
			const auto specularSRV = [&](UINT a_index) { return t.specular[a_index] ? t.specular[a_index]->srv.get() : nullptr; };
			const auto specularUAV = [&](UINT a_index) { return t.specular[a_index] ? t.specular[a_index]->uav.get() : nullptr; };
			// FO4: prepare native normals and current shaded diffuse before unchanged upstream kernels.
			pass.Run(_prepare.get(), { normals, material, albedo, diffuse, tiled ? diffuseB : nullptr, emissive },
				{ t.normalGloss->uav.get(), t.diffuse ? t.diffuse->uav.get() : nullptr }, width, height, 8, "SSGI/Prepare");
			const auto prefilter = [&](UINT a_shader, ID3D11ShaderResourceView* a_input, MipTexture& a_output, UINT a_width, UINT a_height, const char* a_name) {
				pass.Run(_shaders[a_shader].get(), { a_input }, { a_output.uavs[0].get(), a_output.uavs[1].get(), a_output.uavs[2].get(), a_output.uavs[3].get(), a_output.uavs[4].get() }, a_width, a_height, 16, a_name);
			};
			prefilter(0, depth, t.depth, width, height, "SSGI/PrefilterDepths");
			UINT ao = _lastAO, gi = _lastGI;
			pass.Run(_shaders[1].get(), { t.diffuse ? t.diffuse->srv.get() : nullptr, t.depth.texture->srv.get(), t.normalGloss->srv.get(), t.previousGeometry->srv.get(), motion, t.accumulation[_lastAccum]->srv.get(), t.ao[ao]->srv.get(), t.luma[gi]->srv.get(), t.chroma[gi]->srv.get(), specularSRV(ao) },
				{ t.radianceTemp->uav.get(), t.accumulation[!_lastAccum]->uav.get(), t.ao[!ao]->uav.get(),
					t.luma[!gi]->uav.get(), t.chroma[!gi]->uav.get(), specularUAV(!ao) },
				internalWidth, internalHeight, 8, "SSGI/RadianceDisocc");
			prefilter(2, t.radianceTemp->srv.get(), t.radiance, internalWidth, internalHeight, "SSGI/PrefilterRadiance");
			ao = !ao;
			gi = !gi;
			_lastAccum = !_lastAccum;
			prefilter(3, t.normalGloss->srv.get(), t.normals, internalWidth, internalHeight, "SSGI/PrefilterNormals");
			pass.Run(_shaders[4].get(), { t.depth.texture->srv.get(), t.normalGloss->srv.get(), t.radiance.texture->srv.get(), _noise.get(), t.accumulation[_lastAccum]->srv.get(), t.luma[gi]->srv.get(), t.chroma[gi]->srv.get(), specularSRV(ao), t.normals.texture->srv.get() },
				{ t.ao[!ao]->uav.get(), t.luma[!gi]->uav.get(), t.chroma[!gi]->uav.get(),
					specularUAV(!ao), t.previousGeometry->uav.get() },
				internalWidth, internalHeight, 8, "SSGI/GI");
			ao = !ao;
			gi = !gi;
			_lastAO = ao;
			_lastGI = gi;
			if (_settings.enableBlur) {
				pass.Run(_shaders[5].get(), { t.depth.texture->srv.get(), t.normalGloss->srv.get(), t.accumulation[_lastAccum]->srv.get(), t.luma[gi]->srv.get(), t.chroma[gi]->srv.get() },
					{ t.accumulation[!_lastAccum]->uav.get(), t.luma[!gi]->uav.get(), t.chroma[!gi]->uav.get() },
					internalWidth, internalHeight, 8, "SSGI/Blur");
				gi = !gi;
				_lastGI = gi;
				_lastAccum = !_lastAccum;
			}
			if (_settings.resolutionMode != 0) {
				pass.Run(_shaders[6].get(), { t.depth.texture->srv.get(), t.ao[ao]->srv.get(), t.luma[gi]->srv.get(), t.chroma[gi]->srv.get(), specularSRV(ao) },
					{ t.ao[!ao]->uav.get(), t.luma[!gi]->uav.get(), t.chroma[!gi]->uav.get(), specularUAV(!ao) },
					width, height, 8, "SSGI/Upsample");
				ao = !ao;
				gi = !gi;
			}
			_outputAO = ao;
			_outputGI = gi;
			_produced = true;
			_failed = false;
		} catch (const std::exception& e) {
			QueueReset("generation_failed");
			_failed = true;
			CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "SSGI generation failed: {}", e.what());
		}
	}

	void ScreenSpaceGI::UpdateConsumer(bool a_enabled, bool a_tiled)
	{
		const ConsumerCB data{ a_enabled, a_tiled, {} };
		if (_consumerData && _consumerData->Enabled == data.Enabled && _consumerData->Tiled == data.Tiled)
			return;
		_consumer->Update(data);
		_consumerData = data;
	}

	void ScreenSpaceGI::Prepass()
	{
		BindComposition(cs::engine::GetImmediateContext());
	}

	void ScreenSpaceGI::BindComposition(ID3D11DeviceContext* a_context)
	{
		if (!a_context || !_consumer)
			return;
		const auto* state = cs::engine::GetGraphicsState();
		const bool ready = _settings.enabled && _produced && state && _hasFrame && _lastFrame == state->frameCount;
		UpdateConsumer(ready, _tiled);
		auto* cb = _consumer->CB();
		cs::engine::BindFrameConstantBuffers(a_context, cs::engine::ShaderStage::kPixel, kConsumerSlot, 1, &cb);
		ID3D11ShaderResourceView* views[kCompositionCount]{};
		if (ready) {
			auto& t = _textures;
			const bool hq = _settings.enableExperimentalSpecularGI;
			views[0] = t.ao[_outputAO]->srv.get();
			// Upstream Deferred.cpp:362-364 binds null diffuse GI in HQ mode.
			views[1] = hq ? nullptr : t.luma[_outputGI]->srv.get();
			views[2] = hq ? nullptr : t.chroma[_outputGI]->srv.get();
			views[3] = t.normalGloss->srv.get();
		}
		cs::engine::BindFrameShaderResources(a_context, cs::engine::ShaderStage::kPixel, kCompositionSlot, kCompositionCount, views);
		auto* specular = ready && _textures.specular[_outputAO] ? _textures.specular[_outputAO]->srv.get() : nullptr;
		cs::engine::BindFrameShaderResources(a_context, cs::engine::ShaderStage::kPixel, kSpecularSlot, 1, &specular);
		++_binds;
	}

	std::span<const FeatureDebugView> ScreenSpaceGI::GetDebugViews() const noexcept
	{
		static constexpr std::array views{ FeatureDebugView{ .id = "occlusion", .label = "Occlusion preview", .kind = FeatureDebugViewKind::kTexturePreview, .textureProvider = [](const Feature& a_feature) {
																return static_cast<const ScreenSpaceGI&>(a_feature).GetOcclusionDebugTexture();
															} } };
		return views;
	}
	void ScreenSpaceGI::SetDebugView(std::string_view a_view) noexcept { _preview = a_view == "occlusion"; }
	FeatureDebugTexture ScreenSpaceGI::GetOcclusionDebugTexture() const
	{
		FeatureDebugTexture result{ .unavailableText = "Buffer not allocated." };
		if (_preview && _resourcesReady) {
			result.texture = _textures.ao[_outputAO]->srv.get();
			result.width = _width;
			result.height = _height;
			result.caption = "Occlusion (bright = occluded)";
		}
		return result;
	}

	void ScreenSpaceGI::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink.Field("enabled", _settings.enabled).Field("injection_registered", _started.load()).Field("resources_ready", _resourcesReady.load()).Field("resource_init_failed", _failed.load()).Field("ao_produced", _produced.load()).Field("ao_denoised", false).Field("vanilla_ssao_applied", _vanillaSSAOApplied.load()).Field("radiance_available", _produced.load()).Field("bounce_produced", _produced && _settings.enableGI).Field("bounce_denoised", _produced && _settings.enableGI && _settings.enableBlur).Field("normal_bound", _binds > 0 && _produced).Field("history_valid", _produced && _historyUsed).Field("motion_available", _motionAvailable.load()).Field("temporal_dispatches", static_cast<std::int64_t>(_produced && _settings.enableTemporalDenoiser ? 2 + _settings.enableBlur : 0)).Field("reset_count", static_cast<std::int64_t>(_resetCount.load())).Field("last_reset_reason", std::string_view(_resetReason.load())).Field("tiled_b_available", _tiledAvailable.load()).Field("tiled_lighting_active", _tiled.load()).Field("camera_ready", _cameraReady.load()).Field("camera_origin", std::format("{} {} {}", _origin[0].load(), _origin[1].load(), _origin[2].load())).Field("camera_previous_origin", std::format("{} {} {}", _previousOrigin[0].load(), _previousOrigin[1].load(), _previousOrigin[2].load())).Field("radiance_source_count", static_cast<std::int64_t>(_produced && _settings.enableGI ? (_tiled ? 2 : 1) : 0)).Field("composition_binds", static_cast<std::int64_t>(_binds.load())).Field("repeat_callbacks", static_cast<std::int64_t>(_repeats.load())).Field("contaminated_light_classes", std::int64_t{ 16 }).Field("contaminated_routes", std::int64_t{ 24 }).Field("resolution_mode", static_cast<std::int64_t>(_settings.resolutionMode)).Field("generation", static_cast<std::int64_t>(_generation)).Dimensions("working", _width, _height);
	}

	void ScreenSpaceGI::DrawSettings()
	{
		static bool showAdvanced = false;
		settings::SettingsEdit edit{ *this };
		const auto tooltip = [](const char* a_text) {
			if (dmui::ui::IsItemHovered())
				dmui::ui::SetTooltip("%s", a_text);
		};
		const auto slider = [&](const char* a_label, auto Settings::* a_member, const char* a_format = nullptr) {
			const auto range = ssgi_settings::kSchema.EditRange(a_member);
			edit.Continuous(dmui::ui::SliderScalar(a_label, &(_settings.*a_member), &range.min, &range.max, a_format));
		};
		const auto percentSlider = [&](const char* a_label, float Settings::* a_member) {
			const auto range = ssgi_settings::kSchema.EditRange(a_member);
			float percent = _settings.*a_member * 100.0f;
			const float minimum = range.min * 100.0f, maximum = range.max * 100.0f;
			if (edit.Continuous(dmui::ui::SliderScalar(a_label, &percent, &minimum, &maximum, "%.1f%%")))
				_settings.*a_member = percent * 0.01f;
		};
		const auto toggle = [&](const char* a_label, bool& a_value, bool a_compile = false) {
			if (edit.Discrete(dmui::ui::Checkbox(a_label, &a_value)) && a_compile)
				_recompile = true;
		};
		const auto preset = [&](int a_slices, int a_steps, int a_resolution, bool a_gi) {
			_settings.numSlices = a_slices;
			_settings.numSteps = a_steps;
			if (a_resolution >= 0)
				_settings.resolutionMode = a_resolution;
			_settings.enableBlur = true;
			_settings.enableGI = a_gi;
			_recompile = true;
			edit.Discrete(true);
		};
		dmui::ui::Separator();
		dmui::ui::Text("Toggles");
		static_cast<void>(dmui::ui::Checkbox("Show Advanced Options", &showAdvanced));
		toggle("Enabled", _settings.enabled);
		dmui::ui::BeginDisabled(!_settings.enabled);
		toggle("Indirect Lighting (IL)", _settings.enableGI, true);
		dmui::ui::EndDisabled();
		toggle("Vanilla SSAO", _settings.enableVanillaSSAO);
		tooltip("Enable Fallout 4's built-in SSAO. Usually disabled when using SSGI to avoid double-darkening.");
		if (showAdvanced) {
			toggle("(Experimental) HQ Specular IL", _settings.enableExperimentalSpecularGI, true);
			tooltip("An experimental specular GI that is more accurate but requires more samples. Won't be blurred.");
		}
		dmui::ui::Separator();
		dmui::ui::Text("Quality/Performance");
		dmui::ui::BeginDisabled(!_settings.enabled);
		if (dmui::ui::Button("AO only"))
			preset(1, 6, -1, false);
		dmui::ui::SameLine();
		if (dmui::ui::Button("Low"))
			preset(10, 12, 2, true);
		dmui::ui::SameLine();
		if (dmui::ui::Button("Standard"))
			preset(4, 8, 1, true);
		dmui::ui::SameLine();
		if (dmui::ui::Button("Extreme"))
			preset(4, 8, 0, true);
		dmui::ui::SameLine();
		if (dmui::ui::Button("Reference"))
			preset(8, 10, 0, true);
		if (showAdvanced) {
			slider("Slices", &Settings::numSlices);
			slider("Steps Per Slice", &Settings::numSteps);
		}
		static constexpr std::array<const char*, kResolutionModes> labels{ "Full Res", "Half Res", "Quarter Res" };
		for (int mode = 0; mode < static_cast<int>(kResolutionModes); ++mode) {
			if (mode != 0)
				dmui::ui::SameLine();
			if (dmui::ui::Selectable(labels[mode], _settings.resolutionMode == mode, dmui::ui::SelectableFlags::kNone, { 120.0f, 0.0f })) {
				_settings.resolutionMode = mode;
				_recompile = true;
				edit.Discrete(true);
			}
		}
		dmui::ui::EndDisabled();
		dmui::ui::Separator();
		dmui::ui::Text("Visual");
		dmui::ui::BeginDisabled(!_settings.enabled);
		slider("AO Power", &Settings::aoPower, "%.2f");
		dmui::ui::BeginDisabled(!_settings.enableGI);
		slider("IL Source Brightness", &Settings::giStrength, "%.2f");
		dmui::ui::EndDisabled();
		slider("AO radius", &Settings::aoRadius, "%.1f units");
		dmui::ui::BeginDisabled(!_settings.enableGI);
		slider("IL radius", &Settings::giRadius, "%.1f units");
		dmui::ui::EndDisabled();
		if (showAdvanced)
			slider("Min Screen Radius", &Settings::minScreenRadius, "%.3f");
		const float minimumFade = 10000.0f, maximumFade = 50000.0f;
		edit.Continuous(dmui::ui::SliderScalar("Depth Fade Near", &_settings.depthFadeRange[0], &minimumFade, &maximumFade, "%.0f units"));
		edit.Continuous(dmui::ui::SliderScalar("Depth Fade Far", &_settings.depthFadeRange[1], &minimumFade, &maximumFade, "%.0f units"));
		if (showAdvanced)
			slider("Thickness", &Settings::thickness, "%.1f units");
		dmui::ui::EndDisabled();
		dmui::ui::Separator();
		dmui::ui::Text("Visual - IL");
		dmui::ui::BeginDisabled(!_settings.enabled || !_settings.enableGI);
		if (showAdvanced)
			slider("IL Distance Compensation", &Settings::giDistanceCompensation, "%.1f");
		percentSlider("IL Saturation", &Settings::giSaturation);
		dmui::ui::EndDisabled();
		dmui::ui::Separator();
		dmui::ui::Text("Denoising");
		dmui::ui::BeginDisabled(!_settings.enabled);
		toggle("Temporal Denoiser", _settings.enableTemporalDenoiser, true);
		dmui::ui::SameLine();
		toggle("Blur", _settings.enableBlur);
		if (showAdvanced) {
			dmui::ui::BeginDisabled(!_settings.enableTemporalDenoiser);
			slider("Max Frame Accumulation", &Settings::maxAccumFrames);
			dmui::ui::EndDisabled();
			dmui::ui::BeginDisabled(!_settings.enableTemporalDenoiser && !_settings.enableGI);
			percentSlider("Movement Disocclusion", &Settings::depthDisocclusion);
			dmui::ui::EndDisabled();
			dmui::ui::BeginDisabled(!_settings.enableBlur);
			slider("Blur Radius", &Settings::blurRadius, "%.1f px");
			slider("Geometry Weight", &Settings::distanceNormalisation, "%.2f");
			dmui::ui::EndDisabled();
		}
		dmui::ui::EndDisabled();
		dmui::ui::Separator();
		dmui::ui::TextDisabled("Resources: %s (%ux%u) | composition binds: %u | generation: %u",
			_failed ? "failed" : (_resourcesReady ? "ready" : "not ready"), _width, _height, _binds.load(), _generation);
		dmui::ui::TextDisabled("History: %s | motion: %s | resets: %u (%s)",
			_historyUsed ? "in use" : "seeding", _motionAvailable ? "yes" : "no", _resetCount.load(), _resetReason.load());
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void ScreenSpaceGI::RestoreDefaultSettings()
	{
		_settings = Settings{};
		_recompile = true;
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { cs::FeatureManager::Get().Register(ScreenSpaceGI::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
