#include "ScreenSpaceGI.h"

#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <REL/Version.h>
#include <toml++/toml.hpp>

#include "Log.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Settings/FeatureConfig.h"
#include "ShaderReplacement.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.screenspacegi");

		constexpr const char* kConfigPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI.toml";
		constexpr const wchar_t* kResolvePath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\ResolveCS.hlsl";
		constexpr const wchar_t* kDecodePath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\XeGTAO\\decode.cs.hlsl";
		constexpr const wchar_t* kPrefilterPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\XeGTAO\\prefilterDepths.cs.hlsl";
		constexpr const wchar_t* kAOPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\XeGTAO\\gi.cs.hlsl";
		constexpr const wchar_t* kDenoisePath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\XeGTAO\\denoise.cs.hlsl";
		constexpr const wchar_t* kAOIntegrationPath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\AOIntegrationCS.hlsl";

		bool SupportsAOIntegration()
		{
			static const bool supported = [] {
				const auto runtime = REL::GetFileVersion(L"Fallout4.exe");
				return runtime &&
					runtime->major() == 1 &&
					runtime->minor() == 11 &&
					runtime->patch() == 221;
			}();
			return supported;
		}

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
				readInteger("mode", a_candidate.mode, 1, 2);
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

		std::uint32_t AOTargetComponents(DXGI_FORMAT a_format)
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
		toml::table table;
		try {
			table = toml::parse_file(kConfigPath);
		} catch (const toml::parse_error&) {
			table = toml::table{};
		}

		auto& settings = table.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
		settings.insert_or_assign("denoise_enabled", _settings.denoiseEnabled);
		settings.insert_or_assign("denoise_radius", _settings.denoiseRadius);
		settings.insert_or_assign("effect_radius", _settings.effectRadius);
		settings.insert_or_assign("ao_power", _settings.aoPower);
		settings.insert_or_assign("depth_fade_start", _settings.depthFadeStart);
		settings.insert_or_assign("depth_fade_end", _settings.depthFadeEnd);
		settings.insert_or_assign("num_slices", _settings.numSlices);
		settings.insert_or_assign("num_steps", _settings.numSteps);
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("mode", _settings.mode);
		settings.insert_or_assign("noise_frozen", _settings.noiseFrozen);

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(kConfigPath).parent_path(), ec);
		std::ofstream out(kConfigPath);
		if (out) {
			out << table;
		}
	}

	void ScreenSpaceGI::Load()
	{
		cs::engine::RegisterPostDeferredPrePass([] {
			ScreenSpaceGI::GetSingleton()->OnComputeResolve();
		});
		cs::engine::RegisterPreSunLightDraw([] {
			ScreenSpaceGI::GetSingleton()->OnPreSunLightDraw();
		});
		cs::engine::RegisterPostDeferredLightsImpl([] {
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
		if (_settings.enabled) EnsureResources();

		_resolveCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kResolvePath, {}, "cs_5_0")));
		if (_resolveCS) {
			L->info("Compiled neutral resolve shader.");
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

		_denoiseCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kDenoisePath, {}, "cs_5_0")));
		if (!_denoiseCS) {
			L->warn("Failed to compile XeGTAO denoise shader.");
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
	}

	bool ScreenSpaceGI::IsReady()
	{
		return _started.load(std::memory_order_acquire) && _settings.enabled && EnsureResources();
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
				// A transient invalid graphics state (0 dims during a menu/loading/alt-tab blip)
				// must not invalidate an already-valid full-res allocation: keep the existing
				// resources so IsReady() stays true and every frame writes our AO (no fallback-
				// to-engine-AO flicker). Only fail when there is nothing allocated yet.
				if (hadResources) {
					return true;
				}
				throw std::runtime_error("graphics state has no screen dimensions");
			}

			// Allocate at full display resolution (DRS-invariant), reallocating only on a
			// true resolution change, mirroring ScreenSpaceShadows' mask. Sizing to the
			// dynres-scaled extent would reallocate every frame as the ratio jitters.
			const std::uint32_t width = state->screenWidth;
			const std::uint32_t height = state->screenHeight;

			if (hadResources && width == _allocW && height == _allocH) {
				return true;
			}

			auto resolveCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<ResolveCB>());
			auto bounceTexture = CreateOutputTexture(width, height);
			auto aoTexture = CreateOutputTexture(width, height);
			auto linearDepthTex = CreateTexture(width, height, DXGI_FORMAT_R32_FLOAT);
			auto workingDepthTex = CreateTexture(width, height, DXGI_FORMAT_R32_FLOAT, 5, false);
			auto viewNormalTex = CreateTexture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
			auto aoRawTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM);
			auto aoDenoisedTex = CreateTexture(width, height, DXGI_FORMAT_R8_UNORM);
			auto xegtaoCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<XeGTAOCB>());
			auto decodeCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<DecodeCB>());

			auto* device = cs::util::GetD3DDevice();
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
		if (!_started.load(std::memory_order_acquire)) {
			return;
		}
		if (_settings.enabled) {
			if (!EnsureResources()) {
				return;
			}
		} else if (!_resourcesReady.load(std::memory_order_acquire)) {
			return;
		} else if (!EnsureResources()) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* state = cs::engine::GetGraphicsState();
		if (!rendererData || !state || !_bounceTexture || !_aoTexture) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}

		try {
			cs::engine::ComputeOMScope scope(context);

			bool aoProducedThisFrame = false;
			bool denoisedThisFrame = false;
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
			const bool projOk = rtm &&
				cs::engine::TryGetWorldSceneProjection(
					worldProj,
					worldInvProj,
					worldNdcToViewMul,
					worldNdcToViewAdd);
			if (_settings.enabled && xegtaoReady && projOk) {
				const float widthRatio = cs::engine::dynres::GetWidthRatio(rtm);
				const float heightRatio = cs::engine::dynres::GetHeightRatio(rtm);
				const int frameW = static_cast<int>(static_cast<float>(_allocW) * widthRatio);
				const int frameH = static_cast<int>(static_cast<float>(_allocH) * heightRatio);
				auto* depthSRV = cs::engine::GetSceneDepthSRV();
				auto* normalSRV = cs::engine::GetRenderTargetSRV(cs::engine::RenderTarget::kGbufferNormal);
				if (frameW > 0 && frameH > 0 && depthSRV && normalSRV) {
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
					// Defaults use FO4 game-unit scale (~70 units/m): radius 256 is about 3.6m.
					xegtaoCB.EffectRadius = _settings.effectRadius;
					xegtaoCB.Thickness = 32.0f;
					xegtaoCB.AOPower = _settings.aoPower;
					// View-space depth (decode.cs) is raw FO4 game units (~70/m). Fade AO out over a
					// large game-unit range (default 40000-50000) so indoor / near-exterior geometry
					// (tens of metres) isn't culled; 60/90 zeroed everything past ~1.3m. Live knobs.
					xegtaoCB.DepthFadeRange[0] = _settings.depthFadeStart;
					xegtaoCB.DepthFadeRange[1] = _settings.depthFadeEnd;
					const float depthFadeSpan = _settings.depthFadeEnd - _settings.depthFadeStart;
					xegtaoCB.DepthFadeScaleConst = depthFadeSpan > 1.0f ? 1.0f / depthFadeSpan : 1.0f;
					xegtaoCB.BlurRadius = _settings.denoiseRadius;
					xegtaoCB.DistanceNormalisation = 2.0f;
					xegtaoCB.CenterBeta = 1.0f;
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

					ID3D11ShaderResourceView* aoSRVs[3] = {
						_workingDepthTex->srv.get(),
						_viewNormalTex->srv.get(),
						_noiseSRV.get()
					};
					ID3D11UnorderedAccessView* aoUAVs[1] = { _aoRawTex->uav.get() };
					context->CSSetShaderResources(0, 3, aoSRVs);
					context->CSSetConstantBuffers(0, 1, xegtaoBuffers);
					context->CSSetSamplers(0, 1, pointClampSamplers);
					context->CSSetUnorderedAccessViews(0, 1, aoUAVs, nullptr);
					context->CSSetShader(_aoCS.get(), nullptr, 0);
					context->Dispatch(
						(static_cast<std::uint32_t>(frameW) + 7u) / 8u,
						(static_cast<std::uint32_t>(frameH) + 7u) / 8u,
						1);
					aoProducedThisFrame = true;

					ID3D11ShaderResourceView* nullAOSRVs[3] = { nullptr, nullptr, nullptr };
					ID3D11UnorderedAccessView* nullAOUAVs[1] = { nullptr };
					ID3D11SamplerState* nullSamplers[1] = { nullptr };
					context->CSSetShaderResources(0, 3, nullAOSRVs);
					context->CSSetUnorderedAccessViews(0, 1, nullAOUAVs, nullptr);
					context->CSSetSamplers(0, 1, nullSamplers);
					context->CSSetConstantBuffers(0, 1, nullBuffers);
					context->CSSetShader(nullptr, nullptr, 0);

					if (_settings.denoiseEnabled && aoProducedThisFrame) {
						ID3D11ShaderResourceView* denoiseSRVs[3] = {
							_workingDepthTex->srv.get(),
							_viewNormalTex->srv.get(),
							_aoRawTex->srv.get()
						};
						ID3D11UnorderedAccessView* denoiseUAVs[1] = { _aoDenoisedTex->uav.get() };
						context->CSSetShaderResources(0, 3, denoiseSRVs);
						context->CSSetConstantBuffers(0, 1, xegtaoBuffers);
						context->CSSetSamplers(0, 1, pointClampSamplers);
						context->CSSetUnorderedAccessViews(0, 1, denoiseUAVs, nullptr);
						context->CSSetShader(_denoiseCS.get(), nullptr, 0);
						context->Dispatch(
							(static_cast<std::uint32_t>(frameW) + 7u) / 8u,
							(static_cast<std::uint32_t>(frameH) + 7u) / 8u,
							1);
						denoisedThisFrame = true;

						context->CSSetShaderResources(0, 3, nullAOSRVs);
						context->CSSetUnorderedAccessViews(0, 1, nullAOUAVs, nullptr);
						context->CSSetSamplers(0, 1, nullSamplers);
						context->CSSetConstantBuffers(0, 1, nullBuffers);
						context->CSSetShader(nullptr, nullptr, 0);
					}
				}
			}

			if (_resolveCS && _resolveCB) {
				ResolveCB cb{};
				cb.Extent[0] = _allocW;
				cb.Extent[1] = _allocH;
				cb.FrameIndex = static_cast<std::uint32_t>(state->frameCount);
				cb.HasAO = aoProducedThisFrame ? 1u : 0u;
				cb.AoPower = _settings.aoPower;
				_resolveCB->Update(cb);

				ID3D11ShaderResourceView* resolveSRVs[1] = {
					aoProducedThisFrame ?
						((_settings.denoiseEnabled && denoisedThisFrame) ?
								_aoDenoisedTex->srv.get() :
								_aoRawTex->srv.get()) :
						nullptr
				};
				ID3D11Buffer* buffers[1] = { _resolveCB->CB() };
				ID3D11UnorderedAccessView* uavs[2] = {
					_bounceTexture->uav.get(),
					_aoTexture->uav.get()
				};
				context->CSSetShaderResources(0, 1, resolveSRVs);
				context->CSSetConstantBuffers(0, 1, buffers);
				context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
				context->CSSetShader(_resolveCS.get(), nullptr, 0);
				context->Dispatch((_allocW + 7u) / 8u, (_allocH + 7u) / 8u, 1);

				ID3D11ShaderResourceView* nullResolveSRVs[1] = { nullptr };
				ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
				ID3D11Buffer* nullBuffers[1] = { nullptr };
				context->CSSetShaderResources(0, 1, nullResolveSRVs);
				context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
				context->CSSetShader(nullptr, nullptr, 0);
				context->CSSetConstantBuffers(0, 1, nullBuffers);
			} else {
				const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				context->ClearUnorderedAccessViewFloat(_aoTexture->uav.get(), white);
				context->ClearUnorderedAccessViewFloat(_bounceTexture->uav.get(), black);
			}
		} catch (const std::exception& e) {
			L->error("Resolve dispatch failed: {}", e.what());
		} catch (...) {
			L->error("Resolve dispatch failed.");
		}
	}

	void ScreenSpaceGI::OnAOIntegration()
	{
		if (!_settings.enabled) {
			return;
		}
		if (!SupportsAOIntegration()) {
			if (!_aoIntegrationUnsupportedRuntimeLogged) {
				_aoIntegrationUnsupportedRuntimeLogged = true;
				L->warn(
					"SSGI AO integration is AE 1.11.221-only until OG/NG render-target indices are validated; skipping.");
			}
			return;
		}
		if (!IsReady() || !_aoTexture) {
			return;
		}
		IntegrateAO(cs::engine::RenderTarget::kSSAOFinal);
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
		const bool needSRV = _settings.mode == 2;  // only the min-blend path reads the engine AO SRV
		if (!targetTexture || !targetUAV || !rtm || !_aoIntegrationCB || (needSRV && !targetSRV)) {
			if (!_aoIntegrationSkipLogged) {
				_aoIntegrationSkipLogged = true;
				L->warn(
					"SSGI AO integration skipped: rt={} mode={} texture={} uav={} srv={} rtm={} cb={}.",
					static_cast<int>(a_target),
					_settings.mode,
					targetTexture != nullptr,
					targetUAV != nullptr,
					targetSRV != nullptr,
					rtm != nullptr,
					_aoIntegrationCB != nullptr);
			}
			return;
		}

		D3D11_TEXTURE2D_DESC targetDesc{};
		targetTexture->GetDesc(&targetDesc);
		D3D11_UNORDERED_ACCESS_VIEW_DESC targetUAVDesc{};
		targetUAV->GetDesc(&targetUAVDesc);
		const std::uint32_t targetComponents = AOTargetComponents(targetUAVDesc.Format);
		if (targetComponents == 0 || targetUAVDesc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D ||
			targetDesc.SampleDesc.Count != 1) {
			if (!_aoIntegrationUnsupportedLogged) {
				_aoIntegrationUnsupportedLogged = true;
				L->warn(
					"SSGI AO integration: unsupported target format={} view={} samples={}.",
					static_cast<int>(targetUAVDesc.Format),
					static_cast<int>(targetUAVDesc.ViewDimension),
					targetDesc.SampleDesc.Count);
			}
			return;
		}
		auto* integrationCS = _aoIntegrationCS[targetComponents - 1].get();
		if (!integrationCS) {
			return;
		}

		const float widthRatio = cs::engine::dynres::GetWidthRatio(rtm);
		const float heightRatio = cs::engine::dynres::GetHeightRatio(rtm);
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

		// Mode 1 (replace) with a matched full-res target skips the compute pass entirely.
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

				// Scratch copy: mode 2 both reads the engine AO and writes its UAV, so snapshot
				// the engine AO into a private SRV before overwriting the target in place.
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
			context->Dispatch((targetW + 7u) / 8u, (targetH + 7u) / 8u, 1);
		} catch (const std::exception& e) {
			L->warn("SSGI AO integration failed: {}.", e.what());
		} catch (...) {
			L->warn("SSGI AO integration failed.");
		}
	}

	void ScreenSpaceGI::OnPreSunLightDraw()
	{
		if (!_started.load(std::memory_order_acquire)) {
			return;
		}
		if (!_resourcesReady.load(std::memory_order_acquire) || !_bounceTexture || !_aoTexture) {
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

		ID3D11PixelShader* ambientPS =
			cs::features::ShaderReplacement::GetSingleton()->GetReplacementPixelShader("ambient_ibl_pass");
		ID3D11PixelShader* boundPS = nullptr;
		context->PSGetShader(&boundPS, nullptr, nullptr);
		const bool isAmbientPass = ambientPS != nullptr && boundPS == ambientPS;
		if (boundPS) {
			boundPS->Release();
		}
		if (!isAmbientPass) {
			return;
		}

		ID3D11ShaderResourceView* bounce = _bounceTexture->srv.get();
		ID3D11ShaderResourceView* ao = _aoTexture->srv.get();
		context->PSSetShaderResources(kBouncePSSlot, 1, &bounce);
		context->PSSetShaderResources(kAOPSSlot, 1, &ao);
		_ssgiBound.store(true, std::memory_order_relaxed);
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
		context->PSSetShaderResources(kAOPSSlot, 1, &nullSRV);
	}

	void ScreenSpaceGI::DrawSettings()
	{
		if (ImGui::Checkbox("Enabled", &_settings.enabled)) {
			SaveSettings();
		}

		const char* status = _resourceInitFailed.load(std::memory_order_acquire) ? "failed" :
			(_resourcesReady.load(std::memory_order_acquire) ? "ready" : "not ready");
		ImGui::TextDisabled(
			"Resources: %s | extent: %ux%u | generation: %u",
			status, _allocW, _allocH, _generation);
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
