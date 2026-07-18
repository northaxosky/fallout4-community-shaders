#include "ScreenSpaceGI.h"

#include <d3d11.h>
#include <imgui.h>

#include <cfloat>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Settings/FeatureConfig.h"
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

			return AcceptSetting(
				feature_config::ReadBool(*settingsTable, "enabled", a_candidate.enabled),
				"enabled", "boolean", a_error);
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
		settings.insert_or_assign("enabled", _settings.enabled);

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
	}

	bool ScreenSpaceGI::IsReady()
	{
		// Phase 2: gate on _started && enabled && per-frame-valid outputs.
		return false;
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
				auto hash = [](std::uint32_t a_value) {
					a_value ^= a_value >> 16;
					a_value *= 0x7FEB352Du;
					a_value ^= a_value >> 15;
					a_value *= 0x846CA68Bu;
					a_value ^= a_value >> 16;
					return a_value;
				};
				for (std::uint32_t texel = 0; texel < kNoiseWidth * kNoiseHeight; ++texel) {
					noiseData[texel * 2] = static_cast<std::uint8_t>(hash(texel) >> 24);
					noiseData[texel * 2 + 1] = static_cast<std::uint8_t>(hash(texel ^ 0x9E3779B9u) >> 24);
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
		if (!_started.load(std::memory_order_acquire) || !_settings.enabled) {
			return;
		}
		if (!EnsureResources() || !_resourcesReady.load(std::memory_order_acquire)) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* state = cs::engine::GetGraphicsState();
		if (!rendererData || !state || !_resolveCB || !_bounceTexture || !_aoTexture || !_resolveCS) {
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		if (!context) {
			return;
		}

		try {
			cs::engine::ComputeOMScope scope(context);

			const bool xegtaoReady =
				_decodeCS && _prefilterCS && _aoCS &&
				_linearDepthTex && _workingDepthTex && _viewNormalTex && _aoRawTex &&
				_noiseSRV && _pointClampSampler && _xegtaoCB && _decodeCB &&
				_workingDepthMipUAVs[0] && _workingDepthMipUAVs[1] && _workingDepthMipUAVs[2] &&
				_workingDepthMipUAVs[3] && _workingDepthMipUAVs[4];
			auto* rtm = cs::engine::GetRenderTargetManager();
			cs::engine::CameraMatrices cam{};
			if (xegtaoReady && rtm && cs::engine::TryGetCameraMatrices(cam)) {
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
					decodeCB.InvProj = cam.invProj;
					decodeCB.RcpFrameDim[0] = 1.0f / frameWidth;
					decodeCB.RcpFrameDim[1] = 1.0f / frameHeight;
					decodeCB.FrameDim[0] = frameWidth;
					decodeCB.FrameDim[1] = frameHeight;
					_decodeCB->Update(decodeCB);

					XeGTAOCB xegtaoCB{};
					xegtaoCB.NDCToViewMul[0] = cam.ndcToViewMul.x;
					xegtaoCB.NDCToViewMul[1] = cam.ndcToViewMul.y;
					xegtaoCB.NDCToViewMul[2] = cam.ndcToViewMul.z;
					xegtaoCB.NDCToViewMul[3] = cam.ndcToViewMul.w;
					xegtaoCB.NDCToViewAdd[0] = cam.ndcToViewAdd.x;
					xegtaoCB.NDCToViewAdd[1] = cam.ndcToViewAdd.y;
					xegtaoCB.NDCToViewAdd[2] = cam.ndcToViewAdd.z;
					xegtaoCB.NDCToViewAdd[3] = cam.ndcToViewAdd.w;
					xegtaoCB.TexDim[0] = texWidth;
					xegtaoCB.TexDim[1] = texHeight;
					xegtaoCB.RcpTexDim[0] = 1.0f / texWidth;
					xegtaoCB.RcpTexDim[1] = 1.0f / texHeight;
					xegtaoCB.FrameDim[0] = frameWidth;
					xegtaoCB.FrameDim[1] = frameHeight;
					xegtaoCB.RcpFrameDim[0] = 1.0f / frameWidth;
					xegtaoCB.RcpFrameDim[1] = 1.0f / frameHeight;
					xegtaoCB.FrameIndex = static_cast<std::uint32_t>(state->frameCount);
					xegtaoCB.NumSlices = 3;
					xegtaoCB.NumSteps = 8;
					xegtaoCB.MinScreenRadius = 3.0f;
					xegtaoCB.AORadius = 1.0f;
					xegtaoCB.EffectRadius = 35.0f;
					xegtaoCB.Thickness = 8.0f;
					xegtaoCB.AOPower = 1.5f;
					// Synthetic scene tuning placeholders; retune to FO4 world scale at go-live.
					xegtaoCB.DepthFadeRange[0] = 60.0f;
					xegtaoCB.DepthFadeRange[1] = 90.0f;
					xegtaoCB.DepthFadeScaleConst = 0.025f;
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

					ID3D11ShaderResourceView* nullAOSRVs[3] = { nullptr, nullptr, nullptr };
					ID3D11UnorderedAccessView* nullAOUAVs[1] = { nullptr };
					ID3D11SamplerState* nullSamplers[1] = { nullptr };
					context->CSSetShaderResources(0, 3, nullAOSRVs);
					context->CSSetUnorderedAccessViews(0, 1, nullAOUAVs, nullptr);
					context->CSSetSamplers(0, 1, nullSamplers);
					context->CSSetConstantBuffers(0, 1, nullBuffers);
					context->CSSetShader(nullptr, nullptr, 0);
				}
			}

			ResolveCB cb{};
			cb.Extent[0] = _allocW;
			cb.Extent[1] = _allocH;
			cb.FrameIndex = static_cast<std::uint32_t>(state->frameCount);
			_resolveCB->Update(cb);

			ID3D11Buffer* buffers[1] = { _resolveCB->CB() };
			ID3D11UnorderedAccessView* uavs[2] = {
				_bounceTexture->uav.get(),
				_aoTexture->uav.get()
			};
			context->CSSetConstantBuffers(0, 1, buffers);
			context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
			context->CSSetShader(_resolveCS.get(), nullptr, 0);
			context->Dispatch((_allocW + 7u) / 8u, (_allocH + 7u) / 8u, 1);

			ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
			ID3D11Buffer* nullBuffers[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
			context->CSSetShader(nullptr, nullptr, 0);
			context->CSSetConstantBuffers(0, 1, nullBuffers);
		} catch (const std::exception& e) {
			L->error("Resolve dispatch failed: {}", e.what());
		} catch (...) {
			L->error("Resolve dispatch failed.");
		}
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
