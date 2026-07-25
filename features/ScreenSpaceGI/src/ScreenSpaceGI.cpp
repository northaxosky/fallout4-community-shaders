#include "ScreenSpaceGI.h"

#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Settings/FeatureConfig.h"
#include "Render/ShaderInjection.h"
#include "ScreenSpaceGILifecycle.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.screenspacegi");

		constexpr const wchar_t* kResolvePath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\ResolveCS.hlsl";
		constexpr const wchar_t* kDecodePath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\XeGTAO\\decode.cs.hlsl";
		constexpr const wchar_t* kPrefilterPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\XeGTAO\\prefilterDepths.cs.hlsl";
		constexpr const wchar_t* kAOPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\XeGTAO\\gi.cs.hlsl";
		constexpr const wchar_t* kDenoisePath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\XeGTAO\\denoise.cs.hlsl";
		constexpr const wchar_t* kAOIntegrationPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\AOIntegrationCS.hlsl";
		constexpr const wchar_t* kBounceTelemetryPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\BounceTelemetryCS.hlsl";
		constexpr const wchar_t* kBounceIntegrationPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\BounceIntegrationPS.hlsl";

		std::string SettingError(std::string_view a_key, std::string_view a_reason)
		{
			return "settings." + std::string(a_key) + ": " + std::string(a_reason);
		}

		bool AcceptSetting(
			feature_config::ScalarReadStatus a_status,
			std::string_view a_key,
			std::string_view a_expected,
			std::string& a_error)
		{
			switch (a_status) {
			case feature_config::ScalarReadStatus::kMissing:
			case feature_config::ScalarReadStatus::kValid:
				return true;
			case feature_config::ScalarReadStatus::kWrongType:
				a_error = SettingError(a_key, "expected " + std::string(a_expected));
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = SettingError(a_key, "invalid value");
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = SettingError(a_key, "value is out of range");
				break;
			}
			return false;
		}

		void StoreViewToWorld(const RE::NiMatrix3& a_rotation, float* a_output)
		{
			for (std::size_t row = 0; row < 3; ++row) {
				a_output[row * 4 + 0] = a_rotation.entry[row].x;
				a_output[row * 4 + 1] = a_rotation.entry[row].y;
				a_output[row * 4 + 2] = a_rotation.entry[row].z;
				a_output[row * 4 + 3] = 0.0f;
			}
		}

		bool IsFullResolutionHDR(
			ID3D11ShaderResourceView* a_srv,
			std::uint32_t a_width,
			std::uint32_t a_height,
			D3D11_TEXTURE2D_DESC& a_desc)
		{
			if (!a_srv) {
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			a_srv->GetDesc(&srvDesc);
			if (srvDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				srvDesc.Format != DXGI_FORMAT_R11G11B10_FLOAT) {
				return false;
			}

			winrt::com_ptr<ID3D11Resource> resource;
			a_srv->GetResource(resource.put());
			auto texture = resource.try_as<ID3D11Texture2D>();
			if (!texture) {
				return false;
			}

			texture->GetDesc(&a_desc);
			return a_desc.Width == a_width &&
				a_desc.Height == a_height &&
				a_desc.SampleDesc.Count == 1;
		}

		class FullscreenDrawScope
		{
		public:
			explicit FullscreenDrawScope(ID3D11DeviceContext* a_context) noexcept :
				_context(a_context)
			{
				_context->OMGetBlendState(
					_blendState.put(), _blendFactor.data(), &_sampleMask);
				_context->OMGetDepthStencilState(
					_depthStencilState.put(), &_stencilRef);
				_context->RSGetState(_rasterizerState.put());
				_viewportCount = static_cast<UINT>(_viewports.size());
				_context->RSGetViewports(&_viewportCount, _viewports.data());
				_context->IAGetInputLayout(_inputLayout.put());
				_context->IAGetPrimitiveTopology(&_topology);

				_vsClassCount = static_cast<UINT>(_vsClasses.size());
				_context->VSGetShader(_vertexShader.put(), _vsClasses.data(), &_vsClassCount);
				_hsClassCount = static_cast<UINT>(_hsClasses.size());
				_context->HSGetShader(_hullShader.put(), _hsClasses.data(), &_hsClassCount);
				_dsClassCount = static_cast<UINT>(_dsClasses.size());
				_context->DSGetShader(_domainShader.put(), _dsClasses.data(), &_dsClassCount);
				_gsClassCount = static_cast<UINT>(_gsClasses.size());
				_context->GSGetShader(_geometryShader.put(), _gsClasses.data(), &_gsClassCount);
				_psClassCount = static_cast<UINT>(_psClasses.size());
				_context->PSGetShader(_pixelShader.put(), _psClasses.data(), &_psClassCount);

				ID3D11ShaderResourceView* shaderResource = nullptr;
				_context->PSGetShaderResources(0, 1, &shaderResource);
				_pixelShaderResource.attach(shaderResource);
				ID3D11Buffer* constantBuffer = nullptr;
				_context->PSGetConstantBuffers(0, 1, &constantBuffer);
				_pixelConstantBuffer.attach(constantBuffer);
			}

			~FullscreenDrawScope() noexcept
			{
				ID3D11ShaderResourceView* nullSRV = nullptr;
				_context->PSSetShaderResources(0, 1, &nullSRV);
				_context->OMSetRenderTargets(0, nullptr, nullptr);

				ID3D11Buffer* constantBuffer = _pixelConstantBuffer.get();
				_context->PSSetConstantBuffers(0, 1, &constantBuffer);
				ID3D11ShaderResourceView* shaderResource = _pixelShaderResource.get();
				_context->PSSetShaderResources(0, 1, &shaderResource);
				_context->PSSetShader(_pixelShader.get(), _psClasses.data(), _psClassCount);
				_context->GSSetShader(_geometryShader.get(), _gsClasses.data(), _gsClassCount);
				_context->DSSetShader(_domainShader.get(), _dsClasses.data(), _dsClassCount);
				_context->HSSetShader(_hullShader.get(), _hsClasses.data(), _hsClassCount);
				_context->VSSetShader(_vertexShader.get(), _vsClasses.data(), _vsClassCount);
				_context->IASetPrimitiveTopology(_topology);
				_context->IASetInputLayout(_inputLayout.get());
				_context->RSSetViewports(_viewportCount, _viewports.data());
				_context->RSSetState(_rasterizerState.get());
				_context->OMSetDepthStencilState(_depthStencilState.get(), _stencilRef);
				_context->OMSetBlendState(
					_blendState.get(), _blendFactor.data(), _sampleMask);

				ReleaseClassInstances(_vsClasses, _vsClassCount);
				ReleaseClassInstances(_hsClasses, _hsClassCount);
				ReleaseClassInstances(_dsClasses, _dsClassCount);
				ReleaseClassInstances(_gsClasses, _gsClassCount);
				ReleaseClassInstances(_psClasses, _psClassCount);
			}

			FullscreenDrawScope(const FullscreenDrawScope&) = delete;
			FullscreenDrawScope(FullscreenDrawScope&&) = delete;
			FullscreenDrawScope& operator=(const FullscreenDrawScope&) = delete;
			FullscreenDrawScope& operator=(FullscreenDrawScope&&) = delete;

		private:
			template <std::size_t Size>
			static void ReleaseClassInstances(
				std::array<ID3D11ClassInstance*, Size>& a_instances,
				UINT a_count) noexcept
			{
				for (UINT index = 0; index < a_count; ++index) {
					if (a_instances[index]) {
						a_instances[index]->Release();
					}
				}
			}

			ID3D11DeviceContext* _context;
			winrt::com_ptr<ID3D11BlendState> _blendState;
			std::array<float, 4> _blendFactor{};
			UINT _sampleMask = 0;
			winrt::com_ptr<ID3D11DepthStencilState> _depthStencilState;
			UINT _stencilRef = 0;
			winrt::com_ptr<ID3D11RasterizerState> _rasterizerState;
			std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
				_viewports{};
			UINT _viewportCount = 0;
			winrt::com_ptr<ID3D11InputLayout> _inputLayout;
			D3D11_PRIMITIVE_TOPOLOGY _topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
			winrt::com_ptr<ID3D11VertexShader> _vertexShader;
			winrt::com_ptr<ID3D11HullShader> _hullShader;
			winrt::com_ptr<ID3D11DomainShader> _domainShader;
			winrt::com_ptr<ID3D11GeometryShader> _geometryShader;
			winrt::com_ptr<ID3D11PixelShader> _pixelShader;
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> _vsClasses{};
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> _hsClasses{};
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> _dsClasses{};
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> _gsClasses{};
			std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> _psClasses{};
			UINT _vsClassCount = 0;
			UINT _hsClassCount = 0;
			UINT _dsClassCount = 0;
			UINT _gsClassCount = 0;
			UINT _psClassCount = 0;
			winrt::com_ptr<ID3D11ShaderResourceView> _pixelShaderResource;
			winrt::com_ptr<ID3D11Buffer> _pixelConstantBuffer;
		};

		bool ParseSettingsTable(
			const toml::table& a_config,
			ScreenSpaceGI::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode) {
				return true;
			}

			const auto* settingsTable = settingsNode->as_table();
			if (!settingsTable) {
				a_error = "settings: expected table";
				return false;
			}

			if (!AcceptSetting(
					feature_config::ReadBool(*settingsTable, "denoise_enabled", a_candidate.denoiseEnabled),
					"denoise_enabled", "boolean", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "denoise_radius", a_candidate.denoiseRadius),
					"denoise_radius", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "effect_radius", a_candidate.effectRadius),
					"effect_radius", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "ao_power", a_candidate.aoPower),
					"ao_power", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(
						*settingsTable, "bounce_strength", a_candidate.bounceStrength, 0.0f, 8.0f),
					"bounce_strength", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "depth_fade_start", a_candidate.depthFadeStart),
					"depth_fade_start", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "depth_fade_end", a_candidate.depthFadeEnd),
					"depth_fade_end", "number", a_error) ||
				!AcceptSetting(
					feature_config::ReadBool(*settingsTable, "enabled", a_candidate.enabled),
					"enabled", "boolean", a_error) ||
				!AcceptSetting(
					feature_config::ReadBool(*settingsTable, "noise_frozen", a_candidate.noiseFrozen),
					"noise_frozen", "boolean", a_error)) {
				return false;
			}

			auto readInteger = [&](std::string_view a_key, int& a_value, std::int64_t a_min, std::int64_t a_max) {
				auto value = static_cast<std::int64_t>(a_value);
				const auto status = feature_config::ReadSignedInteger(
					*settingsTable, a_key, value, a_min, a_max);
				if (!AcceptSetting(status, a_key, "integer", a_error)) {
					return false;
				}
				if (status == feature_config::ScalarReadStatus::kValid) {
					a_value = static_cast<int>(value);
				}
				return true;
			};

			return readInteger("num_slices", a_candidate.numSlices, 1, 64) &&
				readInteger("num_steps", a_candidate.numSteps, 1, 64) &&
				readInteger("mode", a_candidate.mode, 1, 2) &&
				readInteger(
					"radiance_source_rt",
					a_candidate.radianceSourceRT,
					0,
					static_cast<int>(cs::engine::RenderTarget::kCount) - 1) &&
				readInteger("bounce_delivery", a_candidate.bounceDelivery, 0, 2);
		}

		std::unique_ptr<cs::buffer::Texture2D> CreateTexture(
			std::uint32_t a_width,
			std::uint32_t a_height,
			DXGI_FORMAT a_format,
			std::uint32_t a_mipLevels = 1,
			bool a_createMipZeroUAV = true)
		{
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = a_width;
			textureDesc.Height = a_height;
			textureDesc.MipLevels = a_mipLevels;
			textureDesc.ArraySize = 1;
			textureDesc.Format = a_format;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			auto texture = std::make_unique<cs::buffer::Texture2D>(textureDesc);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = textureDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = a_mipLevels;
			texture->CreateSRV(srvDesc);

			if (a_createMipZeroUAV) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
				uavDesc.Format = textureDesc.Format;
				uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				uavDesc.Texture2D.MipSlice = 0;
				texture->CreateUAV(uavDesc);
			}

			return texture;
		}

		std::unique_ptr<cs::buffer::Texture2D> CreateOutputTexture(
			std::uint32_t a_width,
			std::uint32_t a_height)
		{
			return CreateTexture(a_width, a_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
		}

		std::uint32_t TargetComponents(DXGI_FORMAT a_format)
		{
			switch (a_format) {
			case DXGI_FORMAT_R8_UNORM:
			case DXGI_FORMAT_R8_SNORM:
			case DXGI_FORMAT_R16_FLOAT:
			case DXGI_FORMAT_R16_UNORM:
			case DXGI_FORMAT_R16_SNORM:
			case DXGI_FORMAT_R32_FLOAT:
				return 1;
			case DXGI_FORMAT_R8G8_UNORM:
			case DXGI_FORMAT_R8G8_SNORM:
			case DXGI_FORMAT_R16G16_FLOAT:
			case DXGI_FORMAT_R16G16_UNORM:
			case DXGI_FORMAT_R16G16_SNORM:
			case DXGI_FORMAT_R32G32_FLOAT:
				return 2;
			case DXGI_FORMAT_R11G11B10_FLOAT:
				return 4;
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_SNORM:
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_UNORM:
			case DXGI_FORMAT_R16G16B16A16_SNORM:
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
				return 4;
			default:
				return 0;
			}
		}

	}

	ScreenSpaceGI* ScreenSpaceGI::GetSingleton()
	{
		static ScreenSpaceGI instance;
		return &instance;
	}

	bool ScreenSpaceGI::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}

		_settings = candidate;
		return true;
	}

	void ScreenSpaceGI::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("denoise_enabled", _settings.denoiseEnabled);
		settings.insert_or_assign("denoise_radius", _settings.denoiseRadius);
		settings.insert_or_assign("effect_radius", _settings.effectRadius);
		settings.insert_or_assign("ao_power", _settings.aoPower);
		settings.insert_or_assign("bounce_strength", _settings.bounceStrength);
		settings.insert_or_assign("radiance_source_rt", _settings.radianceSourceRT);
		settings.insert_or_assign("bounce_delivery", _settings.bounceDelivery);
		settings.insert_or_assign("depth_fade_start", _settings.depthFadeStart);
		settings.insert_or_assign("depth_fade_end", _settings.depthFadeEnd);
		settings.insert_or_assign("num_slices", _settings.numSlices);
		settings.insert_or_assign("num_steps", _settings.numSteps);
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("mode", _settings.mode);
		settings.insert_or_assign("noise_frozen", _settings.noiseFrozen);

		if (const auto result = feature_config::UpdateFeatureSettings(GetConfigKey(), settings); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void ScreenSpaceGI::Load()
	{
		const bool ambientRegistered = cs::engine::RegisterReplacement({
			.targetId = cs::engine::ShaderInjectionTarget::kAmbientIblPass,
			.contributor = "ScreenSpaceGI",
			.defines = { { "SSGI", "1" } },
			.isReady = [this] {
				return IsReady();
			},
			.bind = [this](ID3D11DeviceContext* a_context) {
				OnAmbientPassInjection(a_context);
			},
			.slotClaims = {
				{
					.stage = cs::engine::ShaderStage::kPixel,
					.resourceType = cs::engine::ShaderResourceType::kShaderResource,
					.slot = kBouncePSSlot
				}
			}
		});
		if (!ambientRegistered) {
			L->error("Failed to register ambient shader replacement.");
		}

		cs::engine::RegisterPostDeferredPrePass([] {
			ScreenSpaceGI::GetSingleton()->OnComputeResolve();
		});
		cs::engine::RegisterPostDeferredLightsImpl([] {
			ScreenSpaceGI::GetSingleton()->OnPostDeferredLights();
		});
		cs::engine::RegisterPostDeferredComposite([] {
			ScreenSpaceGI::GetSingleton()->OnPostDeferredLights();
		});
		cs::engine::RegisterPostDeferredLightsImpl([] {
			ScreenSpaceGI::GetSingleton()->OnAOIntegration();
		});
		_started.store(true, std::memory_order_release);
		L->info("Registered post-deferred-prepass callback (enabled={}).", _settings.enabled);
	}

	void ScreenSpaceGI::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_started.load(std::memory_order_acquire) || !a_device) return;
		EnsureResources();

		_resolveCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kResolvePath, {}, "cs_5_0")));
		if (_resolveCS) {
			L->info("Compiled resolve shader.");
		}

		_decodeCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kDecodePath, {}, "cs_5_0")));
		if (!_decodeCS) {
			L->warn("Failed to compile XeGTAO decode shader.");
		}

		_prefilterCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kPrefilterPath, { { "LINEAR_FILTER", "1" } }, "cs_5_0")));
		if (!_prefilterCS) {
			L->warn("Failed to compile XeGTAO prefilter shader.");
		}

		_aoCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kAOPath, {}, "cs_5_0")));
		if (!_aoCS) {
			L->warn("Failed to compile XeGTAO AO shader.");
		}

		_bounceCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kAOPath, { { "SSGI_BOUNCE", "1" } }, "cs_5_0")));
		if (!_bounceCS) {
			L->warn("Failed to compile XeGTAO bounce shader.");
		}

		_denoiseCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kDenoisePath, {}, "cs_5_0")));
		if (!_denoiseCS) {
			L->warn("Failed to compile XeGTAO denoise shader.");
		}

		_bounceDenoiseCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kDenoisePath, { { "SSGI_BOUNCE", "1" } }, "cs_5_0")));
		if (!_bounceDenoiseCS) {
			L->warn("Failed to compile XeGTAO bounce denoise shader.");
		}

		_bounceTelemetryCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kBounceTelemetryPath, {}, "cs_5_0")));
		_bounceTelemetryCB = std::make_unique<cs::buffer::ConstantBuffer>(
			cs::buffer::ConstantBufferDesc<BounceTelemetryCB>());
		if (!_bounceTelemetryCS) {
			L->warn("Failed to compile bounce telemetry shader.");
		}

		if (!_pointClampSampler) {
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxAnisotropy = 1;
			samplerDesc.MinLOD = 0.0f;
			samplerDesc.MaxLOD = FLT_MAX;
			DX::ThrowIfFailed(cs::util::GetD3DDevice()->CreateSamplerState(&samplerDesc, _pointClampSampler.put()));
		}

		static constexpr const char* componentCounts[] = { "1", "2", "3", "4" };
		for (std::size_t index = 0; index < _aoIntegrationCS.size(); ++index) {
			_aoIntegrationCS[index].attach(reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(
					kAOIntegrationPath,
					{ { "TARGET_COMPONENTS", componentCounts[index] } },
					"cs_5_0")));
		}
		_aoIntegrationCB = std::make_unique<cs::buffer::ConstantBuffer>(
			cs::buffer::ConstantBufferDesc<AOIntegrationCB>());
		if (std::ranges::any_of(_aoIntegrationCS, [](const auto& a_shader) { return !a_shader; })) {
			L->warn("Failed to compile one or more AO integration shader variants.");
		}

		_bounceIntegrationVS.attach(reinterpret_cast<ID3D11VertexShader*>(
			cs::util::CompileShader(kBounceIntegrationPath, {}, "vs_5_0", "VSMain")));
		_bounceIntegrationPS.attach(reinterpret_cast<ID3D11PixelShader*>(
			cs::util::CompileShader(kBounceIntegrationPath, {}, "ps_5_0", "PSMain")));
		_bounceIntegrationCB = std::make_unique<cs::buffer::ConstantBuffer>(
			cs::buffer::ConstantBufferDesc<BounceIntegrationCB>());
		if (!_bounceIntegrationVS || !_bounceIntegrationPS) {
			L->warn("Failed to compile bounce integration graphics shaders.");
		}

		D3D11_BLEND_DESC blendDesc{};
		auto& renderTargetBlend = blendDesc.RenderTarget[0];
		renderTargetBlend.BlendEnable = TRUE;
		renderTargetBlend.SrcBlend = D3D11_BLEND_ONE;
		renderTargetBlend.DestBlend = D3D11_BLEND_ONE;
		renderTargetBlend.BlendOp = D3D11_BLEND_OP_ADD;
		renderTargetBlend.SrcBlendAlpha = D3D11_BLEND_ONE;
		renderTargetBlend.DestBlendAlpha = D3D11_BLEND_ONE;
		renderTargetBlend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		renderTargetBlend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		DX::ThrowIfFailed(a_device->CreateBlendState(
			&blendDesc, _bounceIntegrationBlendState.put()));

		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = FALSE;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		DX::ThrowIfFailed(a_device->CreateDepthStencilState(
			&depthStencilDesc, _bounceIntegrationDepthStencilState.put()));

		D3D11_RASTERIZER_DESC rasterizerDesc{};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.DepthClipEnable = TRUE;
		DX::ThrowIfFailed(a_device->CreateRasterizerState(
			&rasterizerDesc, _bounceIntegrationRasterizerState.put()));
	}

	bool ScreenSpaceGI::IsReady()
	{
		return ssgi_lifecycle::CanBakeAmbientInjection(
			_started.load(std::memory_order_acquire),
			_resourcesReady.load(std::memory_order_acquire),
			_bounceTexture != nullptr,
			_settings.enabled,
			_settings.bounceDelivery);
	}

	bool ScreenSpaceGI::EnsureResources()
	{
		if (_resourceInitFailed.load(std::memory_order_acquire)) {
			return false;
		}
		if (!cs::util::GetD3DDevice()) {
			return false;
		}

		const bool hadResources = _resourcesReady.load(std::memory_order_acquire);
		try {
			auto* state = cs::engine::GetGraphicsState();
			if (!state || state->screenWidth == 0 || state->screenHeight == 0) {
				// Preserve valid resources across transient zero-sized frames.
				if (hadResources) {
					return true;
				}
				throw std::runtime_error("graphics state has no screen dimensions");
			}

			// Full-resolution allocation avoids churn as dynamic resolution changes.
			const std::uint32_t width = state->screenWidth;
			const std::uint32_t height = state->screenHeight;

			if (hadResources && width == _allocW && height == _allocH) {
				return true;
			}

			auto* rendererData = RE::BSGraphics::GetRendererData();
			auto* context = rendererData ?
				reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
				nullptr;
			if (!context) {
				throw std::runtime_error("renderer context unavailable");
			}

			auto resolveCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<ResolveCB>());
			auto bounceTexture = CreateOutputTexture(width, height);
			auto aoTexture = CreateOutputTexture(width, height);
			context->ClearUnorderedAccessViewFloat(
				bounceTexture->uav.get(), ssgi_lifecycle::kBounceIdentity.data());
			context->ClearUnorderedAccessViewFloat(
				aoTexture->uav.get(), ssgi_lifecycle::kAOIdentity.data());
			auto linearDepthTex = CreateTexture(width, height, DXGI_FORMAT_R32_FLOAT);
			auto workingDepthTex = CreateTexture(width, height, DXGI_FORMAT_R32_FLOAT, 5, false);
			auto viewNormalTex = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
			auto aoRawTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM);
			auto aoDenoisedTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM);
			auto bounceSHRawTex = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
			auto bounceCoCgRawTex = CreateTexture(width, height, DXGI_FORMAT_R16G16_FLOAT);
			auto bounceSHDenoisedTex = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
			auto bounceCoCgDenoisedTex = CreateTexture(width, height, DXGI_FORMAT_R16G16_FLOAT);
			auto bounceTelemetryStats = CreateTexture(
				kBounceTelemetryWidth,
				kBounceTelemetryHeight,
				DXGI_FORMAT_R32G32B32A32_FLOAT);
			auto xegtaoCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<XeGTAOCB>());
			auto decodeCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<DecodeCB>());

			auto* device = cs::util::GetD3DDevice();
			D3D11_TEXTURE2D_DESC readbackDesc = bounceTelemetryStats->desc;
			readbackDesc.Usage = D3D11_USAGE_STAGING;
			readbackDesc.BindFlags = 0;
			readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			readbackDesc.MiscFlags = 0;
			winrt::com_ptr<ID3D11Texture2D> bounceTelemetryReadback;
			DX::ThrowIfFailed(device->CreateTexture2D(
				&readbackDesc, nullptr, bounceTelemetryReadback.put()));

			std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 5> workingDepthMipUAVs;
			D3D11_UNORDERED_ACCESS_VIEW_DESC mipUAVDesc{};
			mipUAVDesc.Format = DXGI_FORMAT_R32_FLOAT;
			mipUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			for (std::uint32_t mip = 0; mip < 5; ++mip) {
				mipUAVDesc.Texture2D.MipSlice = mip;
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(
					workingDepthTex->resource.get(), &mipUAVDesc, workingDepthMipUAVs[mip].put()));
			}

			winrt::com_ptr<ID3D11Texture2D> noiseTex;
			winrt::com_ptr<ID3D11ShaderResourceView> noiseSRV;
			if (!_noiseTex) {
				constexpr std::uint32_t kNoiseWidth = 128;
				constexpr std::uint32_t kNoiseHeight = 8192;
				std::vector<std::uint8_t> noiseData(kNoiseWidth * kNoiseHeight * 2);
				auto hilbertIndex = [](std::uint32_t a_posX, std::uint32_t a_posY) -> std::uint32_t {
					std::uint32_t index = 0u;
					for (std::uint32_t curLevel = 64u / 2u; curLevel > 0u; curLevel /= 2u) {
						const std::uint32_t regionX = (a_posX & curLevel) > 0u ? 1u : 0u;
						const std::uint32_t regionY = (a_posY & curLevel) > 0u ? 1u : 0u;
						index += curLevel * curLevel * ((3u * regionX) ^ regionY);
						if (regionY == 0u) {
							if (regionX == 1u) {
								a_posX = 63u - a_posX;
								a_posY = 63u - a_posY;
							}
							std::swap(a_posX, a_posY);
						}
					}
					return index;
				};
				constexpr double kR2X = 0.75487766624669276005;
				constexpr double kR2Y = 0.569840290998053414;
				for (std::uint32_t t = 0; t < 64u; ++t) {
					for (std::uint32_t yy = 0; yy < 128u; ++yy) {
						for (std::uint32_t x = 0; x < 128u; ++x) {
							const std::uint32_t index = hilbertIndex(x % 64u, yy % 64u) + 288u * t;
							const double nx = std::fmod(0.5 + static_cast<double>(index) * kR2X, 1.0);
							const double ny = std::fmod(0.5 + static_cast<double>(index) * kR2Y, 1.0);
							const std::size_t texel =
								(static_cast<std::size_t>(t) * 128u + yy) * 128u + x;
							noiseData[texel * 2 + 0] =
								static_cast<std::uint8_t>(std::lround(nx * 255.0));
							noiseData[texel * 2 + 1] =
								static_cast<std::uint8_t>(std::lround(ny * 255.0));
						}
					}
				}

				D3D11_TEXTURE2D_DESC noiseDesc{};
				noiseDesc.Width = kNoiseWidth;
				noiseDesc.Height = kNoiseHeight;
				noiseDesc.MipLevels = 1;
				noiseDesc.ArraySize = 1;
				noiseDesc.Format = DXGI_FORMAT_R8G8_UNORM;
				noiseDesc.SampleDesc.Count = 1;
				noiseDesc.Usage = D3D11_USAGE_IMMUTABLE;
				noiseDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

				D3D11_SUBRESOURCE_DATA initialData{};
				initialData.pSysMem = noiseData.data();
				initialData.SysMemPitch = kNoiseWidth * 2;
				DX::ThrowIfFailed(device->CreateTexture2D(&noiseDesc, &initialData, noiseTex.put()));

				D3D11_SHADER_RESOURCE_VIEW_DESC noiseSRVDesc{};
				noiseSRVDesc.Format = DXGI_FORMAT_R8G8_UNORM;
				noiseSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				noiseSRVDesc.Texture2D.MostDetailedMip = 0;
				noiseSRVDesc.Texture2D.MipLevels = 1;
				DX::ThrowIfFailed(device->CreateShaderResourceView(noiseTex.get(), &noiseSRVDesc, noiseSRV.put()));
			}

			_resourcesReady.store(false, std::memory_order_release);
			_resolveCB = std::move(resolveCB);
			_bounceTexture = std::move(bounceTexture);
			_aoTexture = std::move(aoTexture);
			_linearDepthTex = std::move(linearDepthTex);
			_workingDepthTex = std::move(workingDepthTex);
			_workingDepthMipUAVs = std::move(workingDepthMipUAVs);
			_viewNormalTex = std::move(viewNormalTex);
			_aoRawTex = std::move(aoRawTex);
			_aoDenoisedTex = std::move(aoDenoisedTex);
			_bounceSHRawTex = std::move(bounceSHRawTex);
			_bounceCoCgRawTex = std::move(bounceCoCgRawTex);
			_bounceSHDenoisedTex = std::move(bounceSHDenoisedTex);
			_bounceCoCgDenoisedTex = std::move(bounceCoCgDenoisedTex);
			_bounceTelemetryStats = std::move(bounceTelemetryStats);
			_bounceTelemetryReadback = std::move(bounceTelemetryReadback);
			_bounceTelemetryPending = false;
			_bounceTelemetryReady.store(false, std::memory_order_relaxed);
			_bounceMean.store(0.0f, std::memory_order_relaxed);
			_bounceMax.store(0.0f, std::memory_order_relaxed);
			_bounceNonzeroFraction.store(0.0f, std::memory_order_relaxed);
			_xegtaoCB = std::move(xegtaoCB);
			_decodeCB = std::move(decodeCB);
			if (noiseTex) {
				_noiseTex = std::move(noiseTex);
				_noiseSRV = std::move(noiseSRV);
			}
			_allocW = width;
			_allocH = height;
			++_generation;
			_resourcesReady.store(true, std::memory_order_release);
			L->info("Resources ready ({}x{}, generation {}).", _allocW, _allocH, _generation);
			return true;
		} catch (const std::exception& e) {
			if (hadResources) {
				L->error("Resource resize failed: {}", e.what());
			} else {
				_resourceInitFailed.store(true, std::memory_order_release);
				_bounceTexture.reset();
				_aoTexture.reset();
				_resolveCB.reset();
				_linearDepthTex.reset();
				_workingDepthTex.reset();
				_workingDepthMipUAVs = {};
				_viewNormalTex.reset();
				_aoRawTex.reset();
				_aoDenoisedTex.reset();
				_bounceSHRawTex.reset();
				_bounceCoCgRawTex.reset();
				_bounceSHDenoisedTex.reset();
				_bounceCoCgDenoisedTex.reset();
				_bounceTelemetryStats.reset();
				_bounceTelemetryReadback = nullptr;
				_bounceTelemetryPending = false;
				_noiseTex = nullptr;
				_noiseSRV = nullptr;
				_pointClampSampler = nullptr;
				_xegtaoCB.reset();
				_decodeCB.reset();
				_allocW = 0;
				_allocH = 0;
				L->error("Resource creation failed: {}", e.what());
			}
			return false;
		} catch (...) {
			if (hadResources) {
				L->error("Resource resize failed.");
			} else {
				_resourceInitFailed.store(true, std::memory_order_release);
				_bounceTexture.reset();
				_aoTexture.reset();
				_resolveCB.reset();
				_linearDepthTex.reset();
				_workingDepthTex.reset();
				_workingDepthMipUAVs = {};
				_viewNormalTex.reset();
				_aoRawTex.reset();
				_aoDenoisedTex.reset();
				_bounceSHRawTex.reset();
				_bounceCoCgRawTex.reset();
				_bounceSHDenoisedTex.reset();
				_bounceCoCgDenoisedTex.reset();
				_bounceTelemetryStats.reset();
				_bounceTelemetryReadback = nullptr;
				_bounceTelemetryPending = false;
				_noiseTex = nullptr;
				_noiseSRV = nullptr;
				_pointClampSampler = nullptr;
				_xegtaoCB.reset();
				_decodeCB.reset();
				_allocW = 0;
				_allocH = 0;
				L->error("Resource creation failed.");
			}
			return false;
		}
	}

	void ScreenSpaceGI::OnComputeResolve()
	{
		_ssgiBoundLastFrame.store(false, std::memory_order_relaxed);
		_bounceInjectedLastFrame.store(false, std::memory_order_relaxed);
		_bounceAnchorBindsLastFrame.store(0, std::memory_order_relaxed);
		_bounceRTVActiveLastFrame.store(false, std::memory_order_relaxed);
		_bounceRTVDrawsLastFrame.store(0, std::memory_order_relaxed);
		_aoProducedLastFrame.store(false, std::memory_order_relaxed);
		_bounceProducedLastFrame.store(false, std::memory_order_relaxed);
		_radianceAvailableLastFrame.store(false, std::memory_order_relaxed);
		_denoisedLastFrame.store(false, std::memory_order_relaxed);
		_resolveDispatchedLastFrame.store(0, std::memory_order_relaxed);

		if (!_started.load(std::memory_order_acquire)) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}

		if (_bounceTexture) {
			context->ClearUnorderedAccessViewFloat(
				_bounceTexture->uav.get(), ssgi_lifecycle::kBounceIdentity.data());
		}
		if (_aoTexture) {
			context->ClearUnorderedAccessViewFloat(
				_aoTexture->uav.get(), ssgi_lifecycle::kAOIdentity.data());
		}
		if (!EnsureResources() || !_bounceTexture || !_aoTexture) {
			return;
		}
		context->ClearUnorderedAccessViewFloat(
			_bounceTexture->uav.get(), ssgi_lifecycle::kBounceIdentity.data());
		context->ClearUnorderedAccessViewFloat(
			_aoTexture->uav.get(), ssgi_lifecycle::kAOIdentity.data());

		auto* state = cs::engine::GetGraphicsState();
		if (!state || !_settings.enabled) {
			return;
		}

		PollBounceTelemetry(context);

		try {
			cs::engine::ComputeOMScope scope(context);

			bool aoProducedThisFrame = false;
			bool bounceProducedThisFrame = false;
			bool bounceDenoisedThisFrame = false;
			bool denoisedThisFrame = false;
			std::uint32_t activeWidth = 0;
			std::uint32_t activeHeight = 0;
			const bool xegtaoReady =
				_decodeCS && _prefilterCS && _aoCS && _denoiseCS &&
				_linearDepthTex && _workingDepthTex && _viewNormalTex && _aoRawTex && _aoDenoisedTex &&
				_noiseSRV && _pointClampSampler && _xegtaoCB && _decodeCB &&
				_workingDepthMipUAVs[0] && _workingDepthMipUAVs[1] && _workingDepthMipUAVs[2] &&
				_workingDepthMipUAVs[3] && _workingDepthMipUAVs[4];
			auto* rtm = cs::engine::GetRenderTargetManager();
			DirectX::XMFLOAT4X4 worldProj{};
			DirectX::XMFLOAT4X4 worldInvProj{};
			DirectX::XMFLOAT4 worldNdcToViewMul{};
			DirectX::XMFLOAT4 worldNdcToViewAdd{};
			auto* sceneCamera = RE::Main::WorldRootCamera();
			const bool projOk = rtm &&
				cs::engine::TryGetWorldSceneProjection(
					worldProj,
					worldInvProj,
					worldNdcToViewMul,
					worldNdcToViewAdd);
			if (_settings.enabled && xegtaoReady && projOk) {
				const float widthRatio = rtm->GetDynamicWidthRatio();
				const float heightRatio = rtm->GetDynamicHeightRatio();
				const int frameW = static_cast<int>(static_cast<float>(_allocW) * widthRatio);
				const int frameH = static_cast<int>(static_cast<float>(_allocH) * heightRatio);
				auto* depthSRV = cs::engine::GetSceneDepthSRV();
				auto* normalSRV = cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferNormal);
				if (frameW > 0 && frameH > 0 && depthSRV && normalSRV) {
					activeWidth = static_cast<std::uint32_t>(frameW);
					activeHeight = static_cast<std::uint32_t>(frameH);
					const float texWidth = static_cast<float>(_allocW);
					const float texHeight = static_cast<float>(_allocH);
					const float frameWidth = static_cast<float>(frameW);
					const float frameHeight = static_cast<float>(frameH);

					DecodeCB decodeCB{};
					decodeCB.InvProj = worldInvProj;
					decodeCB.RcpFrameDim[0] = 1.0f / frameWidth;
					decodeCB.RcpFrameDim[1] = 1.0f / frameHeight;
					decodeCB.FrameDim[0] = frameWidth;
					decodeCB.FrameDim[1] = frameHeight;
					_decodeCB->Update(decodeCB);

					XeGTAOCB xegtaoCB{};
					xegtaoCB.NDCToViewMul[0] = worldNdcToViewMul.x;
					xegtaoCB.NDCToViewMul[1] = worldNdcToViewMul.y;
					xegtaoCB.NDCToViewMul[2] = worldNdcToViewMul.z;
					xegtaoCB.NDCToViewMul[3] = worldNdcToViewMul.w;
					xegtaoCB.NDCToViewAdd[0] = worldNdcToViewAdd.x;
					xegtaoCB.NDCToViewAdd[1] = worldNdcToViewAdd.y;
					xegtaoCB.NDCToViewAdd[2] = worldNdcToViewAdd.z;
					xegtaoCB.NDCToViewAdd[3] = worldNdcToViewAdd.w;
					xegtaoCB.TexDim[0] = texWidth;
					xegtaoCB.TexDim[1] = texHeight;
					xegtaoCB.RcpTexDim[0] = 1.0f / texWidth;
					xegtaoCB.RcpTexDim[1] = 1.0f / texHeight;
					xegtaoCB.FrameDim[0] = frameWidth;
					xegtaoCB.FrameDim[1] = frameHeight;
					xegtaoCB.RcpFrameDim[0] = 1.0f / frameWidth;
					xegtaoCB.RcpFrameDim[1] = 1.0f / frameHeight;
					xegtaoCB.FrameIndex =
						_settings.noiseFrozen ? 0u : static_cast<std::uint32_t>(state->frameCount);
					xegtaoCB.NumSlices = static_cast<std::uint32_t>(_settings.numSlices);
					xegtaoCB.NumSteps = static_cast<std::uint32_t>(_settings.numSteps);
					xegtaoCB.MinScreenRadius = 3.0f;
					xegtaoCB.AORadius = 1.0f;
					xegtaoCB.EffectRadius = _settings.effectRadius;
					xegtaoCB.Thickness = 32.0f;
					xegtaoCB.AOPower = _settings.aoPower;
					// FO4 view depth uses world units; metric-scale fades suppress nearly all AO.
					xegtaoCB.DepthFadeRange[0] = _settings.depthFadeStart;
					xegtaoCB.DepthFadeRange[1] = _settings.depthFadeEnd;
					const float depthFadeSpan = _settings.depthFadeEnd - _settings.depthFadeStart;
					xegtaoCB.DepthFadeScaleConst = depthFadeSpan > 1.0f ? 1.0f / depthFadeSpan : 1.0f;
					xegtaoCB.BlurRadius = _settings.denoiseRadius;
					xegtaoCB.DistanceNormalisation = 2.0f;
					xegtaoCB.CenterBeta = 1.0f;
					D3D11_TEXTURE2D_DESC radianceDesc{};
					const auto radianceTarget =
						static_cast<cs::engine::RenderTarget>(_settings.radianceSourceRT);
					auto* radianceSRV = cs::engine::GetRenderTargetSRV(radianceTarget);
					const bool bounceResourcesReady =
						_bounceCS &&
						_bounceSHRawTex && _bounceCoCgRawTex &&
						_bounceSHDenoisedTex && _bounceCoCgDenoisedTex;
					const bool radianceAvailable =
						sceneCamera &&
						bounceResourcesReady &&
						IsFullResolutionHDR(radianceSRV, _allocW, _allocH, radianceDesc);
					_radianceAvailableLastFrame.store(radianceAvailable, std::memory_order_relaxed);
					if (radianceAvailable) {
						xegtaoCB.RadianceScale[0] = frameWidth / static_cast<float>(radianceDesc.Width);
						xegtaoCB.RadianceScale[1] = frameHeight / static_cast<float>(radianceDesc.Height);
						StoreViewToWorld(sceneCamera->world.rotate, xegtaoCB.ViewToWorld);
					} else {
						CS_LOG_ONCE(
							L,
							spdlog::level::warn,
							"SSGI bounce unavailable: rt={} must expose a full-resolution R11G11B10_FLOAT SRV.",
							_settings.radianceSourceRT);
					}
					_xegtaoCB->Update(xegtaoCB);

					ID3D11ShaderResourceView* decodeSRVs[2] = { depthSRV, normalSRV };
					ID3D11Buffer* decodeBuffers[1] = { _decodeCB->CB() };
					ID3D11UnorderedAccessView* decodeUAVs[2] = {
						_linearDepthTex->uav.get(),
						_viewNormalTex->uav.get()
					};
					context->CSSetShaderResources(0, 2, decodeSRVs);
					context->CSSetConstantBuffers(0, 1, decodeBuffers);
					context->CSSetUnorderedAccessViews(0, 2, decodeUAVs, nullptr);
					context->CSSetShader(_decodeCS.get(), nullptr, 0);
					context->Dispatch(
						(static_cast<std::uint32_t>(frameW) + 7u) / 8u,
						(static_cast<std::uint32_t>(frameH) + 7u) / 8u,
						1);

					ID3D11ShaderResourceView* nullDecodeSRVs[2] = { nullptr, nullptr };
					ID3D11UnorderedAccessView* nullDecodeUAVs[2] = { nullptr, nullptr };
					ID3D11Buffer* nullBuffers[1] = { nullptr };
					context->CSSetShaderResources(0, 2, nullDecodeSRVs);
					context->CSSetUnorderedAccessViews(0, 2, nullDecodeUAVs, nullptr);
					context->CSSetConstantBuffers(0, 1, nullBuffers);

					ID3D11ShaderResourceView* prefilterSRVs[1] = { _linearDepthTex->srv.get() };
					ID3D11Buffer* xegtaoBuffers[1] = { _xegtaoCB->CB() };
					ID3D11SamplerState* pointClampSamplers[1] = { _pointClampSampler.get() };
					ID3D11UnorderedAccessView* prefilterUAVs[5] = {
						_workingDepthMipUAVs[0].get(),
						_workingDepthMipUAVs[1].get(),
						_workingDepthMipUAVs[2].get(),
						_workingDepthMipUAVs[3].get(),
						_workingDepthMipUAVs[4].get()
					};
					context->CSSetShaderResources(0, 1, prefilterSRVs);
					context->CSSetConstantBuffers(0, 1, xegtaoBuffers);
					context->CSSetSamplers(0, 1, pointClampSamplers);
					context->CSSetUnorderedAccessViews(0, 5, prefilterUAVs, nullptr);
					context->CSSetShader(_prefilterCS.get(), nullptr, 0);
					context->Dispatch(
						(static_cast<std::uint32_t>(frameW) + 15u) / 16u,
						(static_cast<std::uint32_t>(frameH) + 15u) / 16u,
						1);

					ID3D11ShaderResourceView* nullPrefilterSRVs[1] = { nullptr };
					ID3D11UnorderedAccessView* nullPrefilterUAVs[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
					context->CSSetShaderResources(0, 1, nullPrefilterSRVs);
					context->CSSetUnorderedAccessViews(0, 5, nullPrefilterUAVs, nullptr);

					ID3D11ShaderResourceView* aoSRVs[4] = {
						_workingDepthTex->srv.get(),
						_viewNormalTex->srv.get(),
						_noiseSRV.get(),
						radianceAvailable ? radianceSRV : nullptr
					};
					ID3D11UnorderedAccessView* aoUAVs[3] = {
						_aoRawTex->uav.get(),
						radianceAvailable ? _bounceSHRawTex->uav.get() : nullptr,
						radianceAvailable ? _bounceCoCgRawTex->uav.get() : nullptr
					};
					const UINT aoSRVCount = radianceAvailable ? 4u : 3u;
					const UINT aoUAVCount = radianceAvailable ? 3u : 1u;
					context->CSSetShaderResources(0, aoSRVCount, aoSRVs);
					context->CSSetConstantBuffers(0, 1, xegtaoBuffers);
					context->CSSetSamplers(0, 1, pointClampSamplers);
					context->CSSetUnorderedAccessViews(0, aoUAVCount, aoUAVs, nullptr);
					context->CSSetShader(radianceAvailable ? _bounceCS.get() : _aoCS.get(), nullptr, 0);
					context->Dispatch(
						(static_cast<std::uint32_t>(frameW) + 7u) / 8u,
						(static_cast<std::uint32_t>(frameH) + 7u) / 8u,
						1);
					aoProducedThisFrame = true;
					bounceProducedThisFrame = radianceAvailable;

					ID3D11ShaderResourceView* nullAOSRVs[5] = {
						nullptr, nullptr, nullptr, nullptr, nullptr
					};
					ID3D11UnorderedAccessView* nullAOUAVs[3] = { nullptr, nullptr, nullptr };
					ID3D11SamplerState* nullSamplers[1] = { nullptr };
					context->CSSetShaderResources(0, aoSRVCount, nullAOSRVs);
					context->CSSetUnorderedAccessViews(0, aoUAVCount, nullAOUAVs, nullptr);
					context->CSSetSamplers(0, 1, nullSamplers);
					context->CSSetConstantBuffers(0, 1, nullBuffers);
					context->CSSetShader(nullptr, nullptr, 0);

					if (_settings.denoiseEnabled && aoProducedThisFrame) {
						const bool denoiseBounce = bounceProducedThisFrame && _bounceDenoiseCS;
						ID3D11ShaderResourceView* denoiseSRVs[5] = {
							_workingDepthTex->srv.get(),
							_viewNormalTex->srv.get(),
							_aoRawTex->srv.get(),
							denoiseBounce ? _bounceSHRawTex->srv.get() : nullptr,
							denoiseBounce ? _bounceCoCgRawTex->srv.get() : nullptr
						};
						ID3D11UnorderedAccessView* denoiseUAVs[3] = {
							_aoDenoisedTex->uav.get(),
							denoiseBounce ? _bounceSHDenoisedTex->uav.get() : nullptr,
							denoiseBounce ? _bounceCoCgDenoisedTex->uav.get() : nullptr
						};
						const UINT denoiseSRVCount = denoiseBounce ? 5u : 3u;
						const UINT denoiseUAVCount = denoiseBounce ? 3u : 1u;
						context->CSSetShaderResources(0, denoiseSRVCount, denoiseSRVs);
						context->CSSetConstantBuffers(0, 1, xegtaoBuffers);
						context->CSSetSamplers(0, 1, pointClampSamplers);
						context->CSSetUnorderedAccessViews(0, denoiseUAVCount, denoiseUAVs, nullptr);
						context->CSSetShader(
							denoiseBounce ? _bounceDenoiseCS.get() : _denoiseCS.get(), nullptr, 0);
						context->Dispatch(
							(static_cast<std::uint32_t>(frameW) + 7u) / 8u,
							(static_cast<std::uint32_t>(frameH) + 7u) / 8u,
							1);
						denoisedThisFrame = true;
						bounceDenoisedThisFrame = denoiseBounce;

						context->CSSetShaderResources(0, denoiseSRVCount, nullAOSRVs);
						context->CSSetUnorderedAccessViews(0, denoiseUAVCount, nullAOUAVs, nullptr);
						context->CSSetSamplers(0, 1, nullSamplers);
						context->CSSetConstantBuffers(0, 1, nullBuffers);
						context->CSSetShader(nullptr, nullptr, 0);
					}
				}
			}

			_aoProducedLastFrame.store(aoProducedThisFrame, std::memory_order_relaxed);
			_denoisedLastFrame.store(denoisedThisFrame, std::memory_order_relaxed);

			if (_resolveCS && _resolveCB) {
				auto* albedoSRV =
					cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferAlbedo);
				const bool resolveBounce = bounceProducedThisFrame && albedoSRV && sceneCamera;
				ResolveCB cb{};
				cb.Extent[0] = _allocW;
				cb.Extent[1] = _allocH;
				cb.FrameIndex = static_cast<std::uint32_t>(state->frameCount);
				cb.HasAO = aoProducedThisFrame ? 1u : 0u;
				cb.AoPower = _settings.aoPower;
				cb.HasBounce = resolveBounce ? 1u : 0u;
				cb.BounceStrength = _settings.bounceStrength;
				if (sceneCamera) {
					StoreViewToWorld(sceneCamera->world.rotate, cb.ViewToWorld);
				}
				_resolveCB->Update(cb);

				ID3D11ShaderResourceView* resolveSRVs[5] = {
					aoProducedThisFrame ?
						((_settings.denoiseEnabled && denoisedThisFrame) ?
								_aoDenoisedTex->srv.get() :
								_aoRawTex->srv.get()) :
						nullptr,
					resolveBounce ?
						(bounceDenoisedThisFrame ?
								_bounceSHDenoisedTex->srv.get() :
								_bounceSHRawTex->srv.get()) :
						nullptr,
					resolveBounce ?
						(bounceDenoisedThisFrame ?
								_bounceCoCgDenoisedTex->srv.get() :
								_bounceCoCgRawTex->srv.get()) :
						nullptr,
					resolveBounce ? _viewNormalTex->srv.get() : nullptr,
					resolveBounce ? albedoSRV : nullptr
				};
				ID3D11Buffer* buffers[1] = { _resolveCB->CB() };
				ID3D11UnorderedAccessView* uavs[2] = {
					_bounceTexture->uav.get(),
					_aoTexture->uav.get()
				};
				context->CSSetShaderResources(0, 5, resolveSRVs);
				context->CSSetConstantBuffers(0, 1, buffers);
				context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
				context->CSSetShader(_resolveCS.get(), nullptr, 0);
				context->Dispatch((_allocW + 7u) / 8u, (_allocH + 7u) / 8u, 1);
				_resolveDispatchedLastFrame.fetch_add(1, std::memory_order_relaxed);
				_bounceProducedLastFrame.store(resolveBounce, std::memory_order_relaxed);

				ID3D11ShaderResourceView* nullResolveSRVs[5] = {
					nullptr, nullptr, nullptr, nullptr, nullptr
				};
				ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
				ID3D11Buffer* nullBuffers[1] = { nullptr };
				context->CSSetShaderResources(0, 5, nullResolveSRVs);
				context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
				context->CSSetShader(nullptr, nullptr, 0);
				context->CSSetConstantBuffers(0, 1, nullBuffers);

				if (resolveBounce && activeWidth > 0 && activeHeight > 0) {
					QueueBounceTelemetry(
						context,
						activeWidth,
						activeHeight,
						static_cast<std::uint32_t>(state->frameCount));
				}
			} else {
				const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				context->ClearUnorderedAccessViewFloat(_aoTexture->uav.get(), white);
				context->ClearUnorderedAccessViewFloat(_bounceTexture->uav.get(), black);
			}
		} catch (const std::exception& e) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Resolve dispatch failed: {}", e.what());
			}
		} catch (...) {
			if (L->should_log(spdlog::level::err)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::err, "Resolve dispatch failed.");
			}
		}
	}

	void ScreenSpaceGI::PollBounceTelemetry(ID3D11DeviceContext* a_context)
	{
		if (!_bounceTelemetryPending || !_bounceTelemetryReadback) {
			return;
		}

		D3D11_MAPPED_SUBRESOURCE mapped{};
		const HRESULT result = a_context->Map(
			_bounceTelemetryReadback.get(),
			0,
			D3D11_MAP_READ,
			D3D11_MAP_FLAG_DO_NOT_WAIT,
			&mapped);
		if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
			return;
		}
		if (FAILED(result)) {
			_bounceTelemetryPending = false;
			CS_LOG_EVERY_MS(
				L,
				2000,
				spdlog::level::warn,
				"SSGI bounce telemetry readback failed: 0x{:08X}.",
				static_cast<std::uint32_t>(result));
			return;
		}

		double sum = 0.0;
		double maximum = 0.0;
		double nonzero = 0.0;
		double count = 0.0;
		for (std::uint32_t y = 0; y < kBounceTelemetryHeight; ++y) {
			const auto* row = reinterpret_cast<const float*>(
				static_cast<const std::byte*>(mapped.pData) +
				static_cast<std::size_t>(y) * mapped.RowPitch);
			for (std::uint32_t x = 0; x < kBounceTelemetryWidth; ++x) {
				const float* stats = row + static_cast<std::size_t>(x) * 4;
				if (std::isfinite(stats[0]) &&
					std::isfinite(stats[1]) &&
					std::isfinite(stats[2]) &&
					std::isfinite(stats[3])) {
					sum += stats[0];
					maximum = std::max(maximum, static_cast<double>(stats[1]));
					nonzero += stats[2];
					count += stats[3];
				}
			}
		}
		a_context->Unmap(_bounceTelemetryReadback.get(), 0);
		_bounceTelemetryPending = false;

		if (count > 0.0) {
			_bounceMean.store(static_cast<float>(sum / count), std::memory_order_relaxed);
			_bounceMax.store(static_cast<float>(maximum), std::memory_order_relaxed);
			_bounceNonzeroFraction.store(
				static_cast<float>(nonzero / count), std::memory_order_relaxed);
			_bounceTelemetryReady.store(true, std::memory_order_release);
		}
	}

	void ScreenSpaceGI::QueueBounceTelemetry(
		ID3D11DeviceContext* a_context,
		std::uint32_t a_sourceWidth,
		std::uint32_t a_sourceHeight,
		std::uint32_t a_frameIndex)
	{
		if (_bounceTelemetryPending ||
			!_bounceTelemetryCS ||
			!_bounceTelemetryCB ||
			!_bounceTelemetryStats ||
			!_bounceTelemetryReadback) {
			return;
		}
		if (_bounceTelemetryReady.load(std::memory_order_acquire) &&
			a_frameIndex - _bounceTelemetryLastQueuedFrame < kBounceTelemetryIntervalFrames) {
			return;
		}

		BounceTelemetryCB cb{};
		cb.SourceExtent[0] = a_sourceWidth;
		cb.SourceExtent[1] = a_sourceHeight;
		cb.OutputExtent[0] = kBounceTelemetryWidth;
		cb.OutputExtent[1] = kBounceTelemetryHeight;
		_bounceTelemetryCB->Update(cb);

		ID3D11ShaderResourceView* srvs[1] = { _bounceTexture->srv.get() };
		ID3D11Buffer* buffers[1] = { _bounceTelemetryCB->CB() };
		ID3D11UnorderedAccessView* uavs[1] = { _bounceTelemetryStats->uav.get() };
		a_context->CSSetShaderResources(0, 1, srvs);
		a_context->CSSetConstantBuffers(0, 1, buffers);
		a_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		a_context->CSSetShader(_bounceTelemetryCS.get(), nullptr, 0);
		a_context->Dispatch(
			(kBounceTelemetryWidth + 7u) / 8u,
			(kBounceTelemetryHeight + 7u) / 8u,
			1);

		ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
		ID3D11Buffer* nullBuffers[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
		a_context->CSSetShaderResources(0, 1, nullSRVs);
		a_context->CSSetConstantBuffers(0, 1, nullBuffers);
		a_context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		a_context->CSSetShader(nullptr, nullptr, 0);
		a_context->CopyResource(
			_bounceTelemetryReadback.get(),
			_bounceTelemetryStats->resource.get());
		_bounceTelemetryPending = true;
		_bounceTelemetryLastQueuedFrame = a_frameIndex;
	}

	void ScreenSpaceGI::OnAOIntegration()
	{
		_aoIntegrationActiveLastFrame.store(false, std::memory_order_relaxed);
		_aoIntegrationDispatchedLastFrame.store(0, std::memory_order_relaxed);

		if (_settings.enabled && IsReady() && _aoTexture) {
			IntegrateAO(cs::engine::RenderTarget::kSSAOFinal);
			if (_settings.bounceDelivery == 2 &&
				_bounceProducedLastFrame.load(std::memory_order_relaxed)) {
				IntegrateBounce();
			}
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ?
			reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
			nullptr;
		if (context && _bounceTexture &&
			!ssgi_lifecycle::UsesDirectAmbientBounce(
				_settings.enabled, _settings.bounceDelivery)) {
			context->ClearUnorderedAccessViewFloat(
				_bounceTexture->uav.get(), ssgi_lifecycle::kBounceIdentity.data());
		}
		cs::engine::DispatchShaderInjections(
			cs::engine::ShaderInjectionTarget::kAmbientIblPass,
			context);
	}

	void ScreenSpaceGI::IntegrateAO(cs::engine::RenderTarget a_target)
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}

		auto& targetEntry = rendererData->renderTargets[static_cast<uint>(a_target)];
		auto* targetTexture = reinterpret_cast<ID3D11Texture2D*>(targetEntry.texture);
		auto* targetUAV = cs::engine::GetRenderTargetUAV(a_target);
		auto* targetSRV = cs::engine::GetRenderTargetSRV(a_target);
		auto* rtm = cs::engine::GetRenderTargetManager();
		const bool needSRV = _settings.mode == 2;
		if (!targetTexture || !targetUAV || !rtm || !_aoIntegrationCB || (needSRV && !targetSRV)) {
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"SSGI AO integration skipped: rt={} mode={} texture={} uav={} srv={} rtm={} cb={}.",
				static_cast<int>(a_target),
				_settings.mode,
				targetTexture != nullptr,
				targetUAV != nullptr,
				targetSRV != nullptr,
				rtm != nullptr,
				_aoIntegrationCB != nullptr);
			return;
		}

		D3D11_TEXTURE2D_DESC targetDesc{};
		targetTexture->GetDesc(&targetDesc);
		D3D11_UNORDERED_ACCESS_VIEW_DESC targetUAVDesc{};
		targetUAV->GetDesc(&targetUAVDesc);
		const std::uint32_t targetComponents = TargetComponents(targetUAVDesc.Format);
		if (targetComponents == 0 || targetUAVDesc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D ||
			targetDesc.SampleDesc.Count != 1) {
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"SSGI AO integration: unsupported target format={} view={} samples={}.",
				static_cast<int>(targetUAVDesc.Format),
				static_cast<int>(targetUAVDesc.ViewDimension),
				targetDesc.SampleDesc.Count);
			return;
		}
		auto* integrationCS = _aoIntegrationCS[targetComponents - 1].get();
		if (!integrationCS) {
			return;
		}

		const float widthRatio = rtm->GetDynamicWidthRatio();
		const float heightRatio = rtm->GetDynamicHeightRatio();
		const auto scaledExtent = [](std::uint32_t a_extent, float a_ratio) {
			if (a_ratio <= 0.0f) {
				return 0u;
			}
			return std::min(a_extent, static_cast<std::uint32_t>(static_cast<float>(a_extent) * a_ratio));
		};
		const std::uint32_t targetW = scaledExtent(targetDesc.Width, widthRatio);
		const std::uint32_t targetH = scaledExtent(targetDesc.Height, heightRatio);
		const std::uint32_t sourceW = scaledExtent(_allocW, widthRatio);
		const std::uint32_t sourceH = scaledExtent(_allocH, heightRatio);
		if (targetW == 0 || targetH == 0 || sourceW == 0 || sourceH == 0) {
			return;
		}

		const bool copyCompatible =
			_settings.mode == 1 &&
			targetW == targetDesc.Width && targetH == targetDesc.Height &&
			sourceW == _aoTexture->desc.Width && sourceH == _aoTexture->desc.Height &&
			targetDesc.Width == _aoTexture->desc.Width &&
			targetDesc.Height == _aoTexture->desc.Height &&
			targetDesc.MipLevels == _aoTexture->desc.MipLevels &&
			targetDesc.ArraySize == _aoTexture->desc.ArraySize &&
			targetDesc.Format == _aoTexture->desc.Format &&
			targetDesc.SampleDesc.Count == _aoTexture->desc.SampleDesc.Count &&
			targetDesc.SampleDesc.Quality == _aoTexture->desc.SampleDesc.Quality;
		if (copyCompatible) {
			_aoIntegrationActiveLastFrame.store(true, std::memory_order_relaxed);
			cs::engine::CopyResourcePreservingOM(
				context,
				targetTexture,
				_aoTexture->resource.get());
			return;
		}

		try {
			cs::engine::ComputeOMScope scope(context);

			ID3D11ShaderResourceView* engineAO = nullptr;
			if (_settings.mode == 2) {
				D3D11_SHADER_RESOURCE_VIEW_DESC engineSRVDesc{};
				targetSRV->GetDesc(&engineSRVDesc);
				if (engineSRVDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) {
					return;
				}

				// Avoid binding the engine AO as both SRV and UAV.
				if (!_aoIntegrationScratch ||
					_aoIntegrationScratchW != targetDesc.Width ||
					_aoIntegrationScratchH != targetDesc.Height ||
					_aoIntegrationScratchFormat != targetDesc.Format) {
					D3D11_TEXTURE2D_DESC scratchDesc{};
					scratchDesc.Width = targetDesc.Width;
					scratchDesc.Height = targetDesc.Height;
					scratchDesc.MipLevels = 1;
					scratchDesc.ArraySize = 1;
					scratchDesc.Format = targetDesc.Format;
					scratchDesc.SampleDesc.Count = 1;
					scratchDesc.Usage = D3D11_USAGE_DEFAULT;
					scratchDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

					winrt::com_ptr<ID3D11Texture2D> scratch;
					DX::ThrowIfFailed(reinterpret_cast<ID3D11Device*>(rendererData->device)->CreateTexture2D(
						&scratchDesc, nullptr, scratch.put()));

					engineSRVDesc.Texture2D.MostDetailedMip = 0;
					engineSRVDesc.Texture2D.MipLevels = 1;
					winrt::com_ptr<ID3D11ShaderResourceView> scratchSRV;
					DX::ThrowIfFailed(reinterpret_cast<ID3D11Device*>(rendererData->device)->CreateShaderResourceView(
						scratch.get(), &engineSRVDesc, scratchSRV.put()));

					_aoIntegrationScratch = std::move(scratch);
					_aoIntegrationScratchSRV = std::move(scratchSRV);
					_aoIntegrationScratchW = targetDesc.Width;
					_aoIntegrationScratchH = targetDesc.Height;
					_aoIntegrationScratchFormat = targetDesc.Format;
				}

				const D3D11_BOX sourceBox{ 0, 0, 0, targetW, targetH, 1 };
				context->CopySubresourceRegion(
					_aoIntegrationScratch.get(), 0, 0, 0, 0, targetTexture, 0, &sourceBox);
				engineAO = _aoIntegrationScratchSRV.get();
			}

			AOIntegrationCB cb{};
			cb.TargetExtent[0] = targetW;
			cb.TargetExtent[1] = targetH;
			cb.SourceExtent[0] = sourceW;
			cb.SourceExtent[1] = sourceH;
			cb.Mode = static_cast<std::uint32_t>(_settings.mode);
			_aoIntegrationCB->Update(cb);

			ID3D11ShaderResourceView* srvs[2] = { engineAO, _aoTexture ? _aoTexture->srv.get() : nullptr };
			ID3D11Buffer* buffers[1] = { _aoIntegrationCB->CB() };
			ID3D11UnorderedAccessView* uavs[1] = { targetUAV };
			context->CSSetShaderResources(0, 2, srvs);
			context->CSSetConstantBuffers(0, 1, buffers);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(integrationCS, nullptr, 0);
			_aoIntegrationActiveLastFrame.store(true, std::memory_order_relaxed);
			context->Dispatch((targetW + 7u) / 8u, (targetH + 7u) / 8u, 1);
			_aoIntegrationDispatchedLastFrame.fetch_add(1, std::memory_order_relaxed);
		} catch (const std::exception& e) {
			if (L->should_log(spdlog::level::warn)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "SSGI AO integration failed: {}.", e.what());
			}
		} catch (...) {
			if (L->should_log(spdlog::level::warn)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "SSGI AO integration failed.");
			}
		}
	}

	void ScreenSpaceGI::IntegrateBounce()
	{
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}

		constexpr auto target = cs::engine::RenderTarget::kDiffuseBuffer;
		auto& targetEntry = rendererData->renderTargets[static_cast<uint>(target)];
		auto* targetTexture = reinterpret_cast<ID3D11Texture2D*>(targetEntry.texture);
		auto* targetRTV = cs::engine::GetRenderTargetRTV(target);
		auto* rtm = cs::engine::GetRenderTargetManager();
		if (!targetTexture ||
			!targetRTV ||
			!rtm ||
			!_bounceTexture ||
			!_bounceIntegrationVS ||
			!_bounceIntegrationPS ||
			!_bounceIntegrationCB ||
			!_bounceIntegrationBlendState ||
			!_bounceIntegrationDepthStencilState ||
			!_bounceIntegrationRasterizerState) {
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"SSGI bounce integration skipped: rt={} texture={} rtv={} rtm={} shaders={} cb={} states={}.",
				static_cast<int>(target),
				targetTexture != nullptr,
				targetRTV != nullptr,
				rtm != nullptr,
				_bounceIntegrationVS != nullptr && _bounceIntegrationPS != nullptr,
				_bounceIntegrationCB != nullptr,
				_bounceIntegrationBlendState != nullptr &&
					_bounceIntegrationDepthStencilState != nullptr &&
					_bounceIntegrationRasterizerState != nullptr);
			return;
		}

		D3D11_TEXTURE2D_DESC targetDesc{};
		targetTexture->GetDesc(&targetDesc);
		D3D11_RENDER_TARGET_VIEW_DESC targetRTVDesc{};
		targetRTV->GetDesc(&targetRTVDesc);
		if (targetDesc.Format != DXGI_FORMAT_R11G11B10_FLOAT ||
			targetRTVDesc.Format != DXGI_FORMAT_R11G11B10_FLOAT ||
			targetRTVDesc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
			targetDesc.SampleDesc.Count != 1) {
			CS_LOG_ONCE(
				L,
				spdlog::level::warn,
				"SSGI bounce integration: unsupported target format={} view={} samples={}.",
				static_cast<int>(targetRTVDesc.Format),
				static_cast<int>(targetRTVDesc.ViewDimension),
				targetDesc.SampleDesc.Count);
			return;
		}

		const float widthRatio = rtm->GetDynamicWidthRatio();
		const float heightRatio = rtm->GetDynamicHeightRatio();
		const auto scaledExtent = [](std::uint32_t a_extent, float a_ratio) {
			if (a_ratio <= 0.0f) {
				return 0u;
			}
			return std::min(a_extent, static_cast<std::uint32_t>(static_cast<float>(a_extent) * a_ratio));
		};
		const std::uint32_t targetW = scaledExtent(targetDesc.Width, widthRatio);
		const std::uint32_t targetH = scaledExtent(targetDesc.Height, heightRatio);
		const std::uint32_t sourceW = scaledExtent(_allocW, widthRatio);
		const std::uint32_t sourceH = scaledExtent(_allocH, heightRatio);
		if (targetW == 0 || targetH == 0 || sourceW == 0 || sourceH == 0) {
			return;
		}

		try {
			BounceIntegrationCB cb{};
			cb.TargetExtent[0] = targetW;
			cb.TargetExtent[1] = targetH;
			cb.SourceExtent[0] = sourceW;
			cb.SourceExtent[1] = sourceH;
			_bounceIntegrationCB->Update(cb);

			cs::engine::OMScope omScope(context);
			FullscreenDrawScope drawScope(context);

			const D3D11_VIEWPORT viewport{
				0.0f,
				0.0f,
				static_cast<float>(targetW),
				static_cast<float>(targetH),
				0.0f,
				1.0f
			};
			const std::array<float, 4> blendFactor{};
			ID3D11ShaderResourceView* bounceSRV = _bounceTexture->srv.get();
			ID3D11Buffer* buffers[1] = { _bounceIntegrationCB->CB() };
			context->OMSetRenderTargets(1, &targetRTV, nullptr);
			context->OMSetBlendState(
				_bounceIntegrationBlendState.get(), blendFactor.data(), 0xffffffffu);
			context->OMSetDepthStencilState(_bounceIntegrationDepthStencilState.get(), 0);
			context->RSSetState(_bounceIntegrationRasterizerState.get());
			context->RSSetViewports(1, &viewport);
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(_bounceIntegrationVS.get(), nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->GSSetShader(nullptr, nullptr, 0);
			context->PSSetShader(_bounceIntegrationPS.get(), nullptr, 0);
			context->PSSetShaderResources(0, 1, &bounceSRV);
			context->PSSetConstantBuffers(0, 1, buffers);
			context->Draw(3, 0);

			_bounceRTVActiveLastFrame.store(true, std::memory_order_relaxed);
			_bounceRTVDrawsLastFrame.fetch_add(1, std::memory_order_relaxed);
			_bounceInjectedLastFrame.store(true, std::memory_order_relaxed);
		} catch (const std::exception& e) {
			if (L->should_log(spdlog::level::warn)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "SSGI bounce integration failed: {}.", e.what());
			}
		} catch (...) {
			if (L->should_log(spdlog::level::warn)) {
				CS_LOG_EVERY_MS(L, 2000, spdlog::level::warn, "SSGI bounce integration failed.");
			}
		}
	}

	void ScreenSpaceGI::OnAmbientPassInjection(ID3D11DeviceContext* a_context)
	{
		if (!_started.load(std::memory_order_acquire)) {
			return;
		}
		if (!_resourcesReady.load(std::memory_order_acquire) || !_bounceTexture || !_aoTexture) {
			return;
		}
		if (!a_context) {
			return;
		}
		ID3D11ShaderResourceView* bounce = _bounceTexture->srv.get();
		a_context->PSSetShaderResources(kBouncePSSlot, 1, &bounce);
		_bounceAnchorBindsLastFrame.fetch_add(1, std::memory_order_relaxed);
		_ssgiBound.store(true, std::memory_order_relaxed);
		_ssgiBoundLastFrame.store(true, std::memory_order_relaxed);
		_bounceInjectedLastFrame.store(true, std::memory_order_relaxed);
	}

	void ScreenSpaceGI::OnPostDeferredLights()
	{
		if (!_ssgiBound.exchange(false, std::memory_order_relaxed)) {
			return;
		}
		auto* rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(kBouncePSSlot, 1, &nullSRV);
	}

	void ScreenSpaceGI::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink
			.Field("enabled", _settings.enabled)
			.Field("mode", static_cast<std::int64_t>(_settings.mode))
			.Field("resources_ready", _resourcesReady.load(std::memory_order_acquire))
			.Field("resource_init_failed", _resourceInitFailed.load(std::memory_order_acquire))
			.Field("ssgi_bound", _ssgiBoundLastFrame.load(std::memory_order_relaxed))
			.Field("bounce_injected", _bounceInjectedLastFrame.load(std::memory_order_relaxed))
			.Field(
				"bounce_anchor_binds",
				static_cast<std::int64_t>(
					_bounceAnchorBindsLastFrame.load(std::memory_order_relaxed)))
			.Field("bounce_rtv_active", _bounceRTVActiveLastFrame.load(std::memory_order_relaxed))
			.Field(
				"bounce_rtv_draws",
				static_cast<std::int64_t>(
					_bounceRTVDrawsLastFrame.load(std::memory_order_relaxed)))
			.Field("ao_produced", _aoProducedLastFrame.load(std::memory_order_relaxed))
			.Field("radiance_available", _radianceAvailableLastFrame.load(std::memory_order_relaxed))
			.Field("bounce_produced", _bounceProducedLastFrame.load(std::memory_order_relaxed))
			.Field("bounce_readback_ready", _bounceTelemetryReady.load(std::memory_order_acquire))
			.Field("bounce_mean", static_cast<double>(_bounceMean.load(std::memory_order_relaxed)))
			.Field("bounce_max", static_cast<double>(_bounceMax.load(std::memory_order_relaxed)))
			.Field(
				"bounce_nonzero_frac",
				static_cast<double>(_bounceNonzeroFraction.load(std::memory_order_relaxed)))
			.Field("radiance_source_rt", static_cast<std::int64_t>(_settings.radianceSourceRT))
			.Field("bounce_delivery", static_cast<std::int64_t>(_settings.bounceDelivery))
			.Field(
				"bounce_target_rt",
				static_cast<std::int64_t>(cs::engine::RenderTarget::kDiffuseBuffer))
			.Field("denoised", _denoisedLastFrame.load(std::memory_order_relaxed))
			.Field("ao_integration_active", _aoIntegrationActiveLastFrame.load(std::memory_order_relaxed))
			.Field("resolve_dispatches", static_cast<std::int64_t>(
				_resolveDispatchedLastFrame.load(std::memory_order_relaxed)))
			.Field("ao_integration_dispatches", static_cast<std::int64_t>(
				_aoIntegrationDispatchedLastFrame.load(std::memory_order_relaxed)))
			.Dimensions("working", _allocW, _allocH)
			.Field("generation", static_cast<std::int64_t>(_generation));
	}

	void ScreenSpaceGI::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &_settings.enabled);

		const char* modes[] = { "Replace engine AO", "Min-blend with engine AO" };
		int modeIndex = std::clamp(_settings.mode, 1, 2) - 1;
		if (ImGui::Combo("AO integration mode", &modeIndex, modes, static_cast<int>(std::size(modes)))) {
			_settings.mode = modeIndex + 1;
			changed = true;
		}

		changed |= ImGui::SliderInt("Slices", &_settings.numSlices, 1, 8);
		changed |= ImGui::SliderInt("Steps", &_settings.numSteps, 4, 32);
		changed |= ImGui::SliderFloat("Effect radius (game units)", &_settings.effectRadius, 16.0f, 512.0f);
		changed |= ImGui::SliderFloat("AO power", &_settings.aoPower, 0.5f, 5.0f);
		changed |= ImGui::SliderFloat("Bounce strength", &_settings.bounceStrength, 0.0f, 8.0f);
		changed |= ImGui::SliderInt(
			"Radiance source RT",
			&_settings.radianceSourceRT,
			0,
			static_cast<int>(cs::engine::RenderTarget::kCount) - 1);
		const char* bounceDeliveryModes[] = {
			"Telemetry only",
			"Shader injection",
			"Engine RT additive"
		};
		changed |= ImGui::Combo(
			"Bounce delivery",
			&_settings.bounceDelivery,
			bounceDeliveryModes,
			static_cast<int>(std::size(bounceDeliveryModes)));
		changed |= ImGui::Checkbox("Denoise", &_settings.denoiseEnabled);
		changed |= ImGui::SliderFloat("Denoise radius", &_settings.denoiseRadius, 0.5f, 4.0f);
		changed |= ImGui::SliderFloat(
			"Depth fade start (game units)", &_settings.depthFadeStart, 0.0f, 60000.0f);
		changed |= ImGui::SliderFloat(
			"Depth fade end (game units)", &_settings.depthFadeEnd, 0.0f, 80000.0f);
		changed |= ImGui::Checkbox("Freeze noise", &_settings.noiseFrozen);

		if (changed) {
			SaveSettings();
		}

		const char* status = _resourceInitFailed.load(std::memory_order_acquire) ? "failed" :
			(_resourcesReady.load(std::memory_order_acquire) ? "ready" : "not ready");
		ImGui::TextDisabled(
			"Resources: %s | extent: %ux%u | generation: %u",
			status, _allocW, _allocH, _generation);

		// Debug preview: raw AO / bounce buffer in-game, no RenderDoc needed.
		// Mirrors ScreenSpaceShadows' mask preview. Watch while rotating over fixed
		// geometry: a correct buffer is world-locked (a surface holds its value).
		static bool s_showPreview = false;
		static int s_previewSource = 0;
		ImGui::Checkbox("Show GI buffer preview (debug)", &s_showPreview);
		if (s_showPreview) {
			ImGui::RadioButton("AO", &s_previewSource, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Bounce", &s_previewSource, 1);
			const auto& tex = (s_previewSource == 1) ? _bounceTexture : _aoTexture;
			const char* label = (s_previewSource == 1) ? "Bounce (indirect GI)" : "AO (bright = unoccluded)";
			if (tex && tex->srv && _allocW > 0 && _allocH > 0) {
				const float aspect = static_cast<float>(_allocW) / static_cast<float>(_allocH);
				const float previewWidth = 480.0f;
				const float previewHeight = previewWidth / aspect;
				ImGui::TextDisabled("%s %ux%u", label, _allocW, _allocH);
				ImGui::Image(reinterpret_cast<ImTextureID>(tex->srv.get()),
					ImVec2(previewWidth, previewHeight));
			} else {
				ImGui::TextDisabled("Buffer not allocated.");
			}
		}
	}

	void ScreenSpaceGI::RestoreDefaultSettings()
	{
		_settings = Settings{};
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
