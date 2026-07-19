#include "ScreenSpaceGI.h"
#include "OracleProjectionEmbed.h"

#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

#include <RE/T/TESForm.h>
#include <RE/T/TESObjectREFR.h>

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

		std::string CaptureSettingError(std::string_view a_key, std::string_view a_reason)
		{
			return "capture." + std::string(a_key) + ": " + std::string(a_reason);
		}

		bool AcceptCaptureSetting(
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
				a_error = CaptureSettingError(a_key, "expected " + std::string(a_expected));
				break;
			case feature_config::ScalarReadStatus::kInvalidValue:
				a_error = CaptureSettingError(a_key, "invalid value");
				break;
			case feature_config::ScalarReadStatus::kOutOfRange:
				a_error = CaptureSettingError(a_key, "value is out of range");
				break;
			}
			return false;
		}

		bool ParseCaptureTable(
			const toml::table& a_config,
			ScreenSpaceGI::CaptureConfig& a_candidate,
			std::string& a_error)
		{
			const auto* captureNode = a_config.get("capture");
			if (!captureNode) {
				return true;
			}

			const auto* captureTable = captureNode->as_table();
			if (!captureTable) {
				a_error = "capture: expected table";
				return false;
			}

			if (!AcceptCaptureSetting(
					feature_config::ReadBool(*captureTable, "enabled", a_candidate.enabled),
					"enabled", "boolean", a_error)) {
				return false;
			}
			if (!AcceptCaptureSetting(
					feature_config::ReadString(*captureTable, "output", a_candidate.output),
					"output", "string", a_error)) {
				return false;
			}

			auto readInteger = [&](std::string_view a_key, int& a_value, std::int64_t a_max) {
				auto value = static_cast<std::int64_t>(a_value);
				const auto status = feature_config::ReadSignedInteger(
					*captureTable, a_key, value, 0, a_max);
				if (!AcceptCaptureSetting(status, a_key, "integer", a_error)) {
					return false;
				}
				if (status == feature_config::ScalarReadStatus::kValid) {
					a_value = static_cast<int>(value);
				}
				return true;
			};
			constexpr auto maxInt = static_cast<std::int64_t>(std::numeric_limits<int>::max());
			if (!readInteger("settle_frames", a_candidate.settleFrames, maxInt) ||
				!readInteger("interval_frames", a_candidate.intervalFrames, maxInt) ||
				!readInteger("max_snapshots", a_candidate.maxSnapshots, maxInt) ||
				!readInteger("hotkey", a_candidate.hotkey, 0xFFFF)) {
				return false;
			}

			const auto* formIdsNode = captureTable->get("form_ids");
			if (!formIdsNode) {
				return true;
			}
			const auto* formIdsArray = formIdsNode->as_array();
			if (!formIdsArray) {
				a_error = CaptureSettingError("form_ids", "expected array of integers");
				return false;
			}

			std::vector<std::uint32_t> formIds;
			formIds.reserve(formIdsArray->size());
			for (const auto& node : *formIdsArray) {
				const auto formId = node.value<std::int64_t>();
				if (!formId || *formId < 0 ||
					*formId > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
					a_error = CaptureSettingError("form_ids", "expected uint32 integers");
					return false;
				}
				formIds.push_back(static_cast<std::uint32_t>(*formId));
			}
			a_candidate.formIds = std::move(formIds);
			return true;
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

		bool IsCaptureDepthFormat(DXGI_FORMAT a_format)
		{
			switch (a_format) {
			case DXGI_FORMAT_R24G8_TYPELESS:
			case DXGI_FORMAT_D24_UNORM_S8_UINT:
			case DXGI_FORMAT_R32_TYPELESS:
			case DXGI_FORMAT_R32_FLOAT:
			case DXGI_FORMAT_D32_FLOAT:
			case DXGI_FORMAT_R32G8X24_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
				return true;
			default:
				return false;
			}
		}

		std::size_t CaptureDepthTexelBytes(DXGI_FORMAT a_format)
		{
			switch (a_format) {
			case DXGI_FORMAT_R32G8X24_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
				return 8;  // 32-bit depth + 8-bit stencil + 24-bit pad
			default:
				return 4;
			}
		}

		float ReadCaptureDepth(const std::uint8_t* a_texel, DXGI_FORMAT a_format)
		{
			if (a_format == DXGI_FORMAT_R24G8_TYPELESS || a_format == DXGI_FORMAT_D24_UNORM_S8_UINT) {
				std::uint32_t packedDepth{};
				std::memcpy(&packedDepth, a_texel, sizeof(packedDepth));
				return static_cast<float>(packedDepth & 0x00FFFFFFu) / 16777215.0f;
			}

			float depth{};
			std::memcpy(&depth, a_texel, sizeof(depth));
			return depth;
		}

		void AppendPoint3(std::ostringstream& a_json, const RE::NiPoint3& a_point)
		{
			a_json << '[' << a_point.x << ',' << a_point.y << ',' << a_point.z << ']';
		}

		void AppendMatrix(std::ostringstream& a_json, const DirectX::XMFLOAT4X4& a_matrix)
		{
			a_json << '['
				<< a_matrix._11 << ',' << a_matrix._12 << ',' << a_matrix._13 << ',' << a_matrix._14 << ','
				<< a_matrix._21 << ',' << a_matrix._22 << ',' << a_matrix._23 << ',' << a_matrix._24 << ','
				<< a_matrix._31 << ',' << a_matrix._32 << ',' << a_matrix._33 << ',' << a_matrix._34 << ','
				<< a_matrix._41 << ',' << a_matrix._42 << ',' << a_matrix._43 << ',' << a_matrix._44
				<< ']';
		}

		void AppendMatrix3Rows(std::ostringstream& a_json, const RE::NiMatrix3& a_matrix)
		{
			a_json << "[[" << a_matrix[0].x << ',' << a_matrix[0].y << ',' << a_matrix[0].z
				<< "],[" << a_matrix[1].x << ',' << a_matrix[1].y << ',' << a_matrix[1].z
				<< "],[" << a_matrix[2].x << ',' << a_matrix[2].y << ',' << a_matrix[2].z << "]]";
		}

		bool s_loggedUnsupportedCaptureDepthFormat = false;
		bool s_loggedMissingCaptureCamera = false;
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
		CaptureConfig captureCandidate;
		if (!ParseCaptureTable(a_config, captureCandidate, a_error)) {
			return false;
		}

		_settings = candidate;
		_capture = std::move(captureCandidate);
		_captureArmed = false;
		_captureKeyDown = false;
		_snapshotCount = 0;
		_captureJson.clear();
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
		if (_capture.enabled) {
			cs::engine::RegisterPreDeferredLightsImpl([] {
				ScreenSpaceGI::GetSingleton()->OnAnchorDumpFrameBegin();
			});
			cs::engine::RegisterPreSunLightDraw([] {
				ScreenSpaceGI::GetSingleton()->OnAnchorDumpDraw();
			});
			cs::engine::RegisterPostDeferredLightsImpl([] {
				ScreenSpaceGI::GetSingleton()->OnAnchorDumpFrameEnd();
			});
		}
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

	void ScreenSpaceGI::CaptureOracle(ID3D11DeviceContext* a_context, RE::BSGraphics::State* a_state)
	{
		if (!_capture.enabled || _capture.formIds.empty()) {
			return;
		}

		const std::uint32_t frame = static_cast<std::uint32_t>(a_state->frameCount);
		if (!_captureArmed) {
			_captureArmed = true;
			_captureJson.clear();
			L->info("SSGI oracle capture armed at frame {} (hotkey {:#x}).", frame, _capture.hotkey);
		}

		const bool nowDown = (GetAsyncKeyState(_capture.hotkey) & 0x8000) != 0;
		const bool capturePressed = nowDown && !_captureKeyDown;
		_captureKeyDown = nowDown;
		if (!capturePressed || _snapshotCount >= _capture.maxSnapshots) {
			return;
		}

		try {
			cs::engine::CameraMatrices cam{};
			auto* rtm = cs::engine::GetRenderTargetManager();
			if (!rtm || !cs::engine::TryGetCameraMatrices(cam)) {
				return;
			}
			auto* sceneCamera = RE::Main::WorldRootCamera();
			if (!sceneCamera) {
				if (!s_loggedMissingCaptureCamera) {
					s_loggedMissingCaptureCamera = true;
					L->warn("SSGI oracle capture has no world-root camera.");
				}
				return;
			}
			const auto* worldToCam = reinterpret_cast<const DirectX::XMFLOAT4X4*>(&sceneCamera->worldToCam);
			const double embeddedWorldToCam[]{
				static_cast<double>(worldToCam->_11), static_cast<double>(worldToCam->_12), static_cast<double>(worldToCam->_13), static_cast<double>(worldToCam->_14),
				static_cast<double>(worldToCam->_21), static_cast<double>(worldToCam->_22), static_cast<double>(worldToCam->_23), static_cast<double>(worldToCam->_24),
				static_cast<double>(worldToCam->_31), static_cast<double>(worldToCam->_32), static_cast<double>(worldToCam->_33), static_cast<double>(worldToCam->_34),
				static_cast<double>(worldToCam->_41), static_cast<double>(worldToCam->_42), static_cast<double>(worldToCam->_43), static_cast<double>(worldToCam->_44)
			};
			const auto& rotation = sceneCamera->world.rotate;
			const double rotationRows[]{
				static_cast<double>(rotation[0].x), static_cast<double>(rotation[0].y), static_cast<double>(rotation[0].z),
				static_cast<double>(rotation[1].x), static_cast<double>(rotation[1].y), static_cast<double>(rotation[1].z),
				static_cast<double>(rotation[2].x), static_cast<double>(rotation[2].y), static_cast<double>(rotation[2].z)
			};
			const auto& translation = sceneCamera->world.translate;
			const double cameraWorld[]{
				static_cast<double>(translation.x),
				static_cast<double>(translation.y),
				static_cast<double>(translation.z)
			};
			double embeddedProjection[16]{};
			cs::ssgi::dev::EmbedProjectionFromWorldToCam(
				embeddedWorldToCam,
				rotationRows,
				cameraWorld,
				embeddedProjection);

			DirectX::XMFLOAT4X4 builtProjection{};
			DirectX::XMFLOAT4X4 inverseBuiltProjection{};
			DirectX::XMFLOAT4 ndcToViewMul{};
			DirectX::XMFLOAT4 ndcToViewAdd{};
			if (!cs::engine::TryGetWorldSceneProjection(
					builtProjection,
					inverseBuiltProjection,
					ndcToViewMul,
					ndcToViewAdd)) {
				L->warn("SSGI frustum extent-semantics assert: could not build world scene projection.");
			} else {
				const double builtProjectionRows[]{
					static_cast<double>(builtProjection._11), static_cast<double>(builtProjection._12), static_cast<double>(builtProjection._13), static_cast<double>(builtProjection._14),
					static_cast<double>(builtProjection._21), static_cast<double>(builtProjection._22), static_cast<double>(builtProjection._23), static_cast<double>(builtProjection._24),
					static_cast<double>(builtProjection._31), static_cast<double>(builtProjection._32), static_cast<double>(builtProjection._33), static_cast<double>(builtProjection._34),
					static_cast<double>(builtProjection._41), static_cast<double>(builtProjection._42), static_cast<double>(builtProjection._43), static_cast<double>(builtProjection._44)
				};
				double maxAbsElemDiff = 0.0;
				for (std::size_t index = 0; index < 16; ++index) {
					maxAbsElemDiff = std::max(
						maxAbsElemDiff,
						std::fabs(builtProjectionRows[index] - embeddedProjection[index]));
				}

				std::ostringstream message;
				message << std::setprecision(17)
						<< "SSGI frustum extent-semantics assert: maxDiff=" << maxAbsElemDiff
						<< " P_built=[";
				for (std::size_t row = 0; row < 4; ++row) {
					if (row != 0) {
						message << ',';
					}
					message << '[';
					for (std::size_t column = 0; column < 4; ++column) {
						if (column != 0) {
							message << ',';
						}
						message << builtProjectionRows[row * 4 + column];
					}
					message << ']';
				}
				message << "] P_emb=[";
				for (std::size_t row = 0; row < 4; ++row) {
					if (row != 0) {
						message << ',';
					}
					message << '[';
					for (std::size_t column = 0; column < 4; ++column) {
						if (column != 0) {
							message << ',';
						}
						message << embeddedProjection[row * 4 + column];
					}
					message << ']';
				}
				message << ']';
				L->info("{}", message.str());
			}
			const DirectX::XMMATRIX worldToCamMatrix = DirectX::XMLoadFloat4x4(worldToCam);
			const DirectX::XMMATRIX worldToCamTransform = DirectX::XMMatrixTranspose(worldToCamMatrix);

			const int frameW = static_cast<int>(
				static_cast<float>(_allocW) * cs::engine::dynres::GetWidthRatio(rtm));
			const int frameH = static_cast<int>(
				static_cast<float>(_allocH) * cs::engine::dynres::GetHeightRatio(rtm));
			if (frameW <= 0 || frameH <= 0) {
				return;
			}

			auto* depthSRV = cs::engine::GetSceneDepthSRV();
			auto* device = cs::util::GetD3DDevice();
			if (!depthSRV || !device) {
				return;
			}

			winrt::com_ptr<ID3D11Resource> depthResource;
			depthSRV->GetResource(depthResource.put());
			auto depthTexture = depthResource.try_as<ID3D11Texture2D>();
			if (!depthTexture) {
				return;
			}

			D3D11_TEXTURE2D_DESC depthDesc{};
			depthTexture->GetDesc(&depthDesc);
			if (!IsCaptureDepthFormat(depthDesc.Format)) {
				if (!s_loggedUnsupportedCaptureDepthFormat) {
					s_loggedUnsupportedCaptureDepthFormat = true;
					L->warn("SSGI oracle capture does not support depth format {}.", static_cast<int>(depthDesc.Format));
				}
				return;
			}
			if (depthDesc.SampleDesc.Count != 1 ||
				depthDesc.Width < static_cast<std::uint32_t>(frameW) ||
				depthDesc.Height < static_cast<std::uint32_t>(frameH)) {
				L->warn("SSGI oracle capture depth dimensions do not match the render frame.");
				return;
			}

			D3D11_TEXTURE2D_DESC stagingDesc = depthDesc;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.BindFlags = 0;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			stagingDesc.MiscFlags = 0;

			winrt::com_ptr<ID3D11Texture2D> staging;
			DX::ThrowIfFailed(device->CreateTexture2D(&stagingDesc, nullptr, staging.put()));
			a_context->CopyResource(staging.get(), depthTexture.get());

			D3D11_MAPPED_SUBRESOURCE mapped{};
			DX::ThrowIfFailed(a_context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
			bool mappedDepth = true;
			try {
				const RE::NiPoint3 posAdjust = a_state->cameraState.currentPosAdjust;
				std::ostringstream snapshot;
				snapshot << std::setprecision(std::numeric_limits<float>::max_digits10);
				snapshot << "{\"frame\":" << frame
					<< ",\"frameDim\":[" << frameW << ',' << frameH << ']'
					<< ",\"posAdjust\":";
				AppendPoint3(snapshot, posAdjust);
				snapshot << ",\"proj\":";
				AppendMatrix(snapshot, cam.proj);
				snapshot << ",\"invProj\":";
				AppendMatrix(snapshot, cam.invProj);
				snapshot << ",\"viewPerPass\":";
				AppendMatrix(snapshot, cam.view);
				snapshot << ",\"viewProjPerPass\":";
				AppendMatrix(snapshot, cam.viewProj);
				// Engine convention is column-vector clip = M * v.
				snapshot << ",\"worldToCam\":";
				AppendMatrix(snapshot, *worldToCam);
				snapshot << ",\"camWorldTranslate\":";
				AppendPoint3(snapshot, sceneCamera->world.translate);
				snapshot << ",\"camWorldRotateRows\":";
				AppendMatrix3Rows(snapshot, sceneCamera->world.rotate);
				snapshot << ",\"samples\":[";

				int sampleCount = 0;
				int resolvedCount = 0;
				for (const std::uint32_t formId : _capture.formIds) {
					auto appendUnresolved = [&](std::string_view a_reason, const RE::NiPoint3* a_worldPos) {
						if (sampleCount++ > 0) {
							snapshot << ',';
						}
						snapshot << "{\"formId\":" << formId
							<< ",\"resolved\":false,\"culledReason\":\"" << a_reason << "\",\"worldPos\":";
						if (a_worldPos) {
							AppendPoint3(snapshot, *a_worldPos);
							L->info(
								"SSGI oracle ref formId={:#010x} resolved=false worldPos=({:.3f},{:.3f},{:.3f}) culled:{}.",
								formId, a_worldPos->x, a_worldPos->y, a_worldPos->z, a_reason);
						} else {
							snapshot << "null";
							L->info("SSGI oracle ref formId={:#010x} resolved=false worldPos=null culled:{}.",
								formId, a_reason);
						}
						snapshot << '}';
					};

					auto* refr = RE::TESForm::GetFormByID<RE::TESObjectREFR>(formId);
					if (!refr) {
						appendUnresolved("no_ref", nullptr);
						continue;
					}

					const RE::NiPoint3 worldPos = refr->GetPosition();
					if (!refr->Get3D()) {
						appendUnresolved("no_3d", &worldPos);
						continue;
					}
					const DirectX::XMVECTOR rendererWorldPos = DirectX::XMVectorSet(
						worldPos.x - posAdjust.x,
						worldPos.y - posAdjust.y,
						worldPos.z - posAdjust.z,
						1.0f);
					DirectX::XMFLOAT4 clip{};
					DirectX::XMStoreFloat4(
						&clip, DirectX::XMVector4Transform(rendererWorldPos, worldToCamTransform));
					if (clip.w <= 0.0f) {
						appendUnresolved("behind_camera", &worldPos);
						continue;
					}

					const float ndcX = clip.x / clip.w;
					const float ndcY = clip.y / clip.w;
					if (!std::isfinite(ndcX) || !std::isfinite(ndcY) ||
						std::abs(ndcX) > 1.2f || std::abs(ndcY) > 1.2f) {
						appendUnresolved("offscreen", &worldPos);
						continue;
					}

					const float uvX = ndcX * 0.5f + 0.5f;
					const float uvY = (1.0f - ndcY) * 0.5f;
					const int px = std::clamp(static_cast<int>(std::floor(uvX * frameW)), 0, frameW - 1);
					const int py = std::clamp(static_cast<int>(std::floor(uvY * frameH)), 0, frameH - 1);
					const auto* texel = static_cast<const std::uint8_t*>(mapped.pData) +
						static_cast<std::size_t>(py) * mapped.RowPitch +
						static_cast<std::size_t>(px) * CaptureDepthTexelBytes(depthDesc.Format);
					const float storedDepth = ReadCaptureDepth(texel, depthDesc.Format);
					if (!std::isfinite(storedDepth)) {
						appendUnresolved("depth_read_failed", &worldPos);
						continue;
					}

					if (sampleCount++ > 0) {
						snapshot << ',';
					}
					++resolvedCount;
					snapshot << "{\"formId\":" << formId
						<< ",\"resolved\":true,\"culledReason\":\"\",\"worldPos\":";
					AppendPoint3(snapshot, worldPos);
					snapshot << ",\"uv\":[" << uvX << ',' << uvY << ']'
						<< ",\"pixel\":[" << px << ',' << py << ']'
						<< ",\"storedDepth\":" << storedDepth << '}';
					L->info(
						"SSGI oracle ref formId={:#010x} resolved=true worldPos=({:.3f},{:.3f},{:.3f}) uv=({:.6f},{:.6f}).",
						formId, worldPos.x, worldPos.y, worldPos.z, uvX, uvY);
				}
				snapshot << "]}";

				a_context->Unmap(staging.get(), 0);
				mappedDepth = false;

				if (!_captureJson.empty()) {
					_captureJson += ',';
				}
				_captureJson += snapshot.str();
				++_snapshotCount;

				const auto outputPath = std::filesystem::path("Data\\F4SE\\Plugins\\FO4CommunityShaders") / _capture.output;
				std::error_code error;
				std::filesystem::create_directories(outputPath.parent_path(), error);
				if (error) {
					L->warn("SSGI oracle capture could not create output directory: {}.", error.message());
					return;
				}
				std::ofstream output(outputPath);
				if (!output) {
					L->warn("SSGI oracle capture could not write {}.", outputPath.string());
					return;
				}
				output << "{\"schema\":\"ssgi-oracle-capture/2\",\"allocDim\":["
					<< _allocW << ',' << _allocH << "],\"snapshots\":[" << _captureJson << "]}";
				if (!output) {
					L->warn("SSGI oracle capture failed writing {}.", outputPath.string());
					return;
				}
				L->info(
					"SSGI oracle snapshot {}/{} frame {} wrote {} samples ({} resolved).",
					_snapshotCount, _capture.maxSnapshots, frame, sampleCount, resolvedCount);
			} catch (...) {
				if (mappedDepth) {
					a_context->Unmap(staging.get(), 0);
				}
				throw;
			}
		} catch (const std::exception& e) {
			L->warn("SSGI oracle capture failed: {}.", e.what());
		} catch (...) {
			L->warn("SSGI oracle capture failed.");
		}
	}

	void ScreenSpaceGI::OnAnchorDumpFrameBegin()
	{
		_dumpArmed.store(false, std::memory_order_relaxed);

		const bool nowDown = (GetAsyncKeyState(_capture.hotkey) & 0x8000) != 0;
		const bool dumpPressed = nowDown && !_dumpKeyDown;
		_dumpKeyDown = nowDown;
		const int frameCap = _capture.maxSnapshots > 0 ? _capture.maxSnapshots : 8;
		if (!dumpPressed || _dumpFramesLogged >= frameCap) {
			return;
		}

		_dumpOrdinal = 0;
		_dumpTripleMatches = 0;
		_dumpMatchOrdinal = -1;
		_dumpArmed.store(true, std::memory_order_relaxed);
		auto* state = cs::engine::GetGraphicsState();
		const auto frame = state ? static_cast<std::uint32_t>(state->frameCount) : 0u;
		L->info("SSGI anchor-dump FRAME BEGIN (frame {})", frame);
	}

	void ScreenSpaceGI::OnAnchorDumpDraw()
	{
		if (!_dumpArmed.load(std::memory_order_relaxed)) {
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

		ID3D11ShaderResourceView* srvs[16] = {};
		context->PSGetShaderResources(0, 16, srvs);
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 16> ownedSRVs;
		for (std::size_t index = 0; index < ownedSRVs.size(); ++index) {
			ownedSRVs[index].attach(srvs[index]);
		}

		std::array<char, 17> slotMap{};
		for (std::size_t index = 0; index < ownedSRVs.size(); ++index) {
			slotMap[index] = srvs[index] ? 'X' : '.';
		}
		const bool tripleMatch =
			srvs[kBouncePSSlot] == nullptr &&
			srvs[kAOPSSlot] == nullptr &&
			srvs[6] != nullptr;
		if (tripleMatch) {
			++_dumpTripleMatches;
			_dumpMatchOrdinal = _dumpOrdinal;
		}

		winrt::com_ptr<ID3D11Resource> slot6Resource;
		std::array<char, 256> slot6DebugName{};
		if (srvs[6]) {
			srvs[6]->GetResource(slot6Resource.put());
			if (slot6Resource) {
				UINT debugNameSize = static_cast<UINT>(slot6DebugName.size() - 1);
				if (SUCCEEDED(slot6Resource->GetPrivateData(
						WKPDID_D3DDebugObjectName,
						&debugNameSize,
						slot6DebugName.data()))) {
					slot6DebugName[std::min<std::size_t>(
						debugNameSize,
						slot6DebugName.size() - 1)] = '\0';
				}
			}
		}

		L->info(
			"SSGI anchor-dump draw {}: t: {} triple={} t0={:p} t6={:p} t13={:p} t6Resource={:p} t6Name=\"{}\" ambientMatch=n/a",
			_dumpOrdinal,
			slotMap.data(),
			tripleMatch,
			static_cast<const void*>(srvs[kBouncePSSlot]),
			static_cast<const void*>(srvs[6]),
			static_cast<const void*>(srvs[kAOPSSlot]),
			static_cast<const void*>(slot6Resource.get()),
			slot6DebugName.data());
		++_dumpOrdinal;
	}

	void ScreenSpaceGI::OnAnchorDumpFrameEnd()
	{
		if (!_dumpArmed.load(std::memory_order_relaxed)) {
			return;
		}
		L->info(
			"SSGI anchor-dump FRAME END: {} primCount==2 DLI draws; {} matched the triple; matchOrdinal={} lastOrdinal={} matchIsLast={}",
			_dumpOrdinal,
			_dumpTripleMatches,
			_dumpMatchOrdinal,
			_dumpOrdinal - 1,
			_dumpTripleMatches > 0 && _dumpMatchOrdinal == _dumpOrdinal - 1);
		_dumpArmed.store(false, std::memory_order_relaxed);
		++_dumpFramesLogged;
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

			CaptureOracle(context, state);

			const bool xegtaoReady =
				_decodeCS && _prefilterCS && _aoCS &&
				_linearDepthTex && _workingDepthTex && _viewNormalTex && _aoRawTex &&
				_noiseSRV && _pointClampSampler && _xegtaoCB && _decodeCB &&
				_workingDepthMipUAVs[0] && _workingDepthMipUAVs[1] && _workingDepthMipUAVs[2] &&
				_workingDepthMipUAVs[3] && _workingDepthMipUAVs[4];
			auto* rtm = cs::engine::GetRenderTargetManager();
			DirectX::XMFLOAT4X4 worldProj{};
			DirectX::XMFLOAT4X4 worldInvProj{};
			DirectX::XMFLOAT4 worldNdcToViewMul{};
			DirectX::XMFLOAT4 worldNdcToViewAdd{};
			if (xegtaoReady && rtm &&
				cs::engine::TryGetWorldSceneProjection(
					worldProj,
					worldInvProj,
					worldNdcToViewMul,
					worldNdcToViewAdd)) {
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
