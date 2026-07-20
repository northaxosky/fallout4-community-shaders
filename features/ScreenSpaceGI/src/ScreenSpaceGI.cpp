#include "ScreenSpaceGI.h"
#include "OracleProjectionEmbed.h"

#include <DirectXPackedVector.h>
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
#include "ShaderCatalog.h"
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
		constexpr const wchar_t* kKssaoOverwritePath = L"Data\\F4SE\\Plugins\\FO4CommunityShaders\\ScreenSpaceGI\\Shaders\\KssaoOverwriteCS.hlsl";
		constexpr std::uint32_t kLumaSampleWidth = 1024;
		constexpr std::uint32_t kLumaSampleHeight = 128;
		constexpr std::uint32_t kLumaReadbackInterval = 60;

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
					feature_config::ReadBool(*settingsTable, "kssao_probe_enabled", a_candidate.kssaoProbeEnabled),
					"kssao_probe_enabled", "boolean", a_error) ||
				!AcceptSetting(
					feature_config::ReadBool(*settingsTable, "kssao_probe_all_final", a_candidate.kssaoProbeAllFinal),
					"kssao_probe_all_final", "boolean", a_error) ||
				!AcceptSetting(
					feature_config::ReadFloat(*settingsTable, "kssao_probe_value", a_candidate.kssaoProbeValue),
					"kssao_probe_value", "number", a_error) ||
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
				readInteger("kssao_probe_mode", a_candidate.kssaoProbeMode, 0, 3) &&
				readInteger(
					"kssao_probe_rt",
					a_candidate.kssaoProbeRt,
					0,
					static_cast<std::int64_t>(cs::engine::RenderTarget::kCount) - 1) &&
				readInteger("kssao_probe_anchor", a_candidate.kssaoProbeAnchor, 0, 2) &&
				readInteger(
					"kssao_probe_luma_rt",
					a_candidate.kssaoProbeLumaRt,
					0,
					static_cast<std::int64_t>(cs::engine::RenderTarget::kCount) - 1);
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

		std::uint32_t KssaoTargetComponents(DXGI_FORMAT a_format)
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

		std::size_t LumaTexelBytes(DXGI_FORMAT a_format)
		{
			switch (a_format) {
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			case DXGI_FORMAT_R11G11B10_FLOAT:
				return 4;
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_UNORM:
				return 8;
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
				return 16;
			default:
				return 0;
			}
		}

		float SrgbToLinear(float a_value)
		{
			return a_value <= 0.04045f ?
				a_value / 12.92f :
				std::pow((a_value + 0.055f) / 1.055f, 2.4f);
		}

		bool ReadLuminance(const std::uint8_t* a_texel, DXGI_FORMAT a_format, float& a_luminance)
		{
			DirectX::XMFLOAT3 rgb{};
			bool srgb = false;
			switch (a_format) {
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				rgb = {
					static_cast<float>(a_texel[0]) / 255.0f,
					static_cast<float>(a_texel[1]) / 255.0f,
					static_cast<float>(a_texel[2]) / 255.0f
				};
				srgb = a_format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				break;
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
				rgb = {
					static_cast<float>(a_texel[2]) / 255.0f,
					static_cast<float>(a_texel[1]) / 255.0f,
					static_cast<float>(a_texel[0]) / 255.0f
				};
				srgb =
					a_format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
					a_format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
				break;
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			{
				std::uint32_t packed{};
				std::memcpy(&packed, a_texel, sizeof(packed));
				rgb = {
					static_cast<float>(packed & 0x3FFu) / 1023.0f,
					static_cast<float>((packed >> 10) & 0x3FFu) / 1023.0f,
					static_cast<float>((packed >> 20) & 0x3FFu) / 1023.0f
				};
				break;
			}
			case DXGI_FORMAT_R11G11B10_FLOAT:
			{
				DirectX::PackedVector::XMFLOAT3PK packed{};
				std::memcpy(&packed, a_texel, sizeof(packed));
				DirectX::XMStoreFloat3(
					&rgb,
					DirectX::PackedVector::XMLoadFloat3PK(&packed));
				break;
			}
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			{
				DirectX::PackedVector::XMHALF4 packed{};
				std::memcpy(&packed, a_texel, sizeof(packed));
				DirectX::XMStoreFloat3(
					&rgb,
					DirectX::PackedVector::XMLoadHalf4(&packed));
				break;
			}
			case DXGI_FORMAT_R16G16B16A16_UNORM:
			{
				std::uint16_t channels[4]{};
				std::memcpy(channels, a_texel, sizeof(channels));
				rgb = {
					static_cast<float>(channels[0]) / 65535.0f,
					static_cast<float>(channels[1]) / 65535.0f,
					static_cast<float>(channels[2]) / 65535.0f
				};
				break;
			}
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
			{
				float channels[4]{};
				std::memcpy(channels, a_texel, sizeof(channels));
				rgb = { channels[0], channels[1], channels[2] };
				break;
			}
			default:
				return false;
			}

			if (srgb) {
				rgb.x = SrgbToLinear(rgb.x);
				rgb.y = SrgbToLinear(rgb.y);
				rgb.z = SrgbToLinear(rgb.z);
			}
			a_luminance = rgb.x * 0.2126f + rgb.y * 0.7152f + rgb.z * 0.0722f;
			return std::isfinite(a_luminance);
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
		settings.insert_or_assign("denoise_enabled", _settings.denoiseEnabled);
		settings.insert_or_assign("denoise_radius", _settings.denoiseRadius);
		settings.insert_or_assign("effect_radius", _settings.effectRadius);
		settings.insert_or_assign("ao_power", _settings.aoPower);
		settings.insert_or_assign("depth_fade_start", _settings.depthFadeStart);
		settings.insert_or_assign("depth_fade_end", _settings.depthFadeEnd);
		settings.insert_or_assign("num_slices", _settings.numSlices);
		settings.insert_or_assign("num_steps", _settings.numSteps);
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign("kssao_probe_enabled", _settings.kssaoProbeEnabled);
		settings.insert_or_assign("kssao_probe_mode", _settings.kssaoProbeMode);
		settings.insert_or_assign("kssao_probe_value", _settings.kssaoProbeValue);
		settings.insert_or_assign("kssao_probe_rt", _settings.kssaoProbeRt);
		settings.insert_or_assign("kssao_probe_anchor", _settings.kssaoProbeAnchor);
		settings.insert_or_assign("kssao_probe_luma_rt", _settings.kssaoProbeLumaRt);
		settings.insert_or_assign("kssao_probe_all_final", _settings.kssaoProbeAllFinal);
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
		cs::engine::RegisterPostDeferredPrePass([] {
			ScreenSpaceGI::GetSingleton()->OnKssaoOverwrite(0);
		});
		cs::engine::RegisterPreDeferredLightsImpl([] {
			ScreenSpaceGI::GetSingleton()->OnKssaoOverwrite(1);
		});
		cs::engine::RegisterPreSunLightDraw([] {
			ScreenSpaceGI::GetSingleton()->OnPreSunLightDraw();
		});
		cs::engine::RegisterPostDeferredLightsImpl([] {
			ScreenSpaceGI::GetSingleton()->OnPostDeferredLights();
		});
		cs::engine::RegisterPostDeferredLightsImpl([] {
			ScreenSpaceGI::GetSingleton()->OnKssaoOverwrite(2);
		});
		cs::engine::RegisterPostDeferredComposite([] {
			ScreenSpaceGI::GetSingleton()->OnKssaoReadback();
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
			if (auto* catalog = ShaderCatalog::GetSingleton()) {
				catalog->RegisterPixelShaderBindObserver(&ScreenSpaceGI::PixelShaderBindTrampoline);
			}
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

		if (_settings.kssaoProbeEnabled) {
			static constexpr const char* componentCounts[] = { "1", "2", "3", "4" };
			for (std::size_t index = 0; index < _kssaoOverwriteCS.size(); ++index) {
				_kssaoOverwriteCS[index].attach(reinterpret_cast<ID3D11ComputeShader*>(
					cs::util::CompileShader(
						kKssaoOverwritePath,
						{ { "TARGET_COMPONENTS", componentCounts[index] } },
						"cs_5_0")));
			}
			_kssaoOverwriteCB = std::make_unique<cs::buffer::ConstantBuffer>(
				cs::buffer::ConstantBufferDesc<KssaoOverwriteCB>());
			if (std::ranges::any_of(_kssaoOverwriteCS, [](const auto& a_shader) { return !a_shader; })) {
				L->warn("Failed to compile one or more kSSAO overwrite shader variants.");
			}
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
		_bindTraceArmed.store(false, std::memory_order_relaxed);

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
		_dumpIdentityMatches = 0;
		{
			std::scoped_lock lk(_bindTraceMutex);
			_bindTraceSeen.clear();
		}
		_insideDLI.store(true, std::memory_order_relaxed);
		_bindTraceArmed.store(true, std::memory_order_relaxed);
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

		winrt::com_ptr<ID3D11PixelShader> boundPS;
		context->PSGetShader(boundPS.put(), nullptr, nullptr);
		std::string boundSha;
		if (auto* catalog = ShaderCatalog::GetSingleton()) {
			boundSha = catalog->GetShaForPixelShader(boundPS.get());
		}

		L->info(
			"SSGI anchor-dump draw {}: t: {} triple={} t0={:p} t6={:p} t13={:p} t6Resource={:p} t6Name=\"{}\" boundPS={:p} boundSha=\"{}\"",
			_dumpOrdinal,
			slotMap.data(),
			tripleMatch,
			static_cast<const void*>(srvs[kBouncePSSlot]),
			static_cast<const void*>(srvs[6]),
			static_cast<const void*>(srvs[kAOPSSlot]),
			static_cast<const void*>(slot6Resource.get()),
			slot6DebugName.data(),
			static_cast<const void*>(boundPS.get()),
			boundSha.empty() ? "unmapped" : boundSha.c_str());
		++_dumpOrdinal;
	}

	void ScreenSpaceGI::OnAnchorDumpFrameEnd()
	{
		if (!_dumpArmed.load(std::memory_order_relaxed)) {
			return;
		}
		L->info(
			"SSGI anchor-dump FRAME END: {} primCount==2 DLI draws; {} matched the triple; matchOrdinal={} lastOrdinal={} matchIsLast={}; identityMatches={}",
			_dumpOrdinal,
			_dumpTripleMatches,
			_dumpMatchOrdinal,
			_dumpOrdinal - 1,
			_dumpTripleMatches > 0 && _dumpMatchOrdinal == _dumpOrdinal - 1,
			_dumpIdentityMatches);
		_dumpArmed.store(false, std::memory_order_relaxed);
		_insideDLI.store(false, std::memory_order_relaxed);
		++_dumpFramesLogged;
	}

	void ScreenSpaceGI::PixelShaderBindTrampoline(ID3D11PixelShader* a_bound)
	{
		if (auto* self = GetSingleton())
			self->OnPixelShaderBind(a_bound);
	}

	// Dev diagnostic: during the armed dump window, log each distinct bound-PS sha + DLI phase to locate the fullscreen ambient/IBL draw the primCount==2 anchor cannot see.
	void ScreenSpaceGI::OnPixelShaderBind(ID3D11PixelShader* a_bound)
	{
		if (!_bindTraceArmed.load(std::memory_order_relaxed) || !a_bound)
			return;
		auto* catalog = ShaderCatalog::GetSingleton();
		if (!catalog)
			return;
		const std::string sha = catalog->GetShaForPixelShader(a_bound);
		if (sha.empty())
			return;
		const bool insideDLI = _insideDLI.load(std::memory_order_relaxed);
		{
			std::scoped_lock lk(_bindTraceMutex);
			if (!_bindTraceSeen.insert(insideDLI ? sha + "#dli" : sha).second)
				return;
		}
		L->info("SSGI bind-trace: sha={} insideDLI={} ptr={:p}",
			sha, insideDLI, static_cast<const void*>(a_bound));

		// RT->slot probe: match this PS's bound SRVs against the ENTIRE engine
		// render-target pool (0..kCount-1), not just the SAO family, so we learn the
		// exact engine RT index feeding each slot of the ambient/IBL composite -- in
		// particular which enum the AO input (t9) resolves to. Dims/format are logged so
		// a resource that is NOT a managed pool RT is still characterized.
		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* context = rendererData ? reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) : nullptr;
		if (!context) {
			return;
		}
		ID3D11ShaderResourceView* srvs[16] = {};
		context->PSGetShaderResources(0, 16, srvs);
		std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 16> ownedSRVs;
		for (std::size_t index = 0; index < ownedSRVs.size(); ++index) {
			ownedSRVs[index].attach(srvs[index]);
		}

		const uint rtCount = static_cast<uint>(cs::engine::RenderTarget::kCount);
		std::string slotMap;
		for (std::size_t slot = 0; slot < ownedSRVs.size(); ++slot) {
			if (!ownedSRVs[slot]) {
				continue;
			}
			winrt::com_ptr<ID3D11Resource> boundResource;
			ownedSRVs[slot]->GetResource(boundResource.put());
			if (!boundResource) {
				continue;
			}
			int matchedRt = -1;
			for (uint rt = 0; rt < rtCount; ++rt) {
				auto* rtResource = reinterpret_cast<ID3D11Resource*>(
					rendererData->renderTargets[rt].texture);
				if (rtResource && rtResource == boundResource.get()) {
					matchedRt = static_cast<int>(rt);
					break;
				}
			}
			uint w = 0;
			uint h = 0;
			uint fmt = 0;
			winrt::com_ptr<ID3D11Texture2D> tex;
			if (SUCCEEDED(boundResource->QueryInterface(IID_PPV_ARGS(tex.put())))) {
				D3D11_TEXTURE2D_DESC desc{};
				tex->GetDesc(&desc);
				w = desc.Width;
				h = desc.Height;
				fmt = static_cast<uint>(desc.Format);
			}
			slotMap += " t" + std::to_string(slot) + "=RT" +
			           (matchedRt >= 0 ? std::to_string(matchedRt) : std::string("none")) +
			           "(" + std::to_string(w) + "x" + std::to_string(h) + " fmt" + std::to_string(fmt) + ")";
		}
		if (!slotMap.empty()) {
			L->info("SSGI rt-slot-map: sha={} insideDLI={}{}", sha, insideDLI, slotMap);
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

			CaptureOracle(context, state);

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
			if (_settings.enabled && !_xegtaoGateLogged) {
				_xegtaoGateLogged = true;
				L->info(
					"SSGI XeGTAO gate: xegtaoReady={} rtm={} proj={} (decodeCS={} prefilterCS={} aoCS={} noise={} aoRaw={} linDepth={} workDepth={} viewNrm={}).",
					xegtaoReady, rtm != nullptr, projOk,
					_decodeCS != nullptr, _prefilterCS != nullptr, _aoCS != nullptr,
					_noiseSRV != nullptr, _aoRawTex != nullptr,
					_linearDepthTex != nullptr, _workingDepthTex != nullptr, _viewNormalTex != nullptr);
			}
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
					// EffectRadius/AOPower are live toml knobs. Defaults are FO4 game-unit scale
					// (~70 units/m): EffectRadius 256 ~= 3.6m, matching the config that measured
					// indoor occ mean ~0.3. Sweep in-game against the one-shot occ histogram below.
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

					if (!_xegtaoCbLogged) {
						_xegtaoCbLogged = true;
						L->info(
							"SSGI XeGTAOCB: EffectRadius={:.1f} AORadius={:.3f} Thickness={:.1f} AOPower={:.3f} "
							"DepthFade=[{:.1f},{:.1f}] DepthFadeScale={:.6f} NumSlices={} NumSteps={} MinScreenRadius={:.2f}",
							xegtaoCB.EffectRadius, xegtaoCB.AORadius, xegtaoCB.Thickness, xegtaoCB.AOPower,
							xegtaoCB.DepthFadeRange[0], xegtaoCB.DepthFadeRange[1], xegtaoCB.DepthFadeScaleConst,
							xegtaoCB.NumSlices, xegtaoCB.NumSteps, xegtaoCB.MinScreenRadius);
					}

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
					if (!_xegtaoProducedLogged) {
						_xegtaoProducedLogged = true;
						L->info("SSGI XeGTAO PRODUCED real AO (frame {}x{}).", frameW, frameH);
					}

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

					const auto* aoStatsTex = denoisedThisFrame ? _aoDenoisedTex.get() : _aoRawTex.get();
					// One-shot occlusion histogram over the resolved AO input (0=open, 1=occluded): ground-
					// truths the AO strength so tuning EffectRadius/AOPower/NumSlices is measured,
					// not guessed. A single full-frame copy+map stall is fine for a launch-once
					// diagnostic. Uses the immediate context (same one OnKssaoReadback maps).
					if (!_aoStatsLogged && aoStatsTex) {
						_aoStatsLogged = true;
						const auto fw = static_cast<std::uint32_t>(frameW);
						const auto fh = static_cast<std::uint32_t>(frameH);
						auto* device = cs::util::GetD3DDevice();
						if (device && fw > 0u && fh > 0u) {
							D3D11_TEXTURE2D_DESC sd{};
							sd.Width = fw;
							sd.Height = fh;
							sd.MipLevels = 1;
							sd.ArraySize = 1;
							sd.Format = DXGI_FORMAT_R8_UNORM;
							sd.SampleDesc.Count = 1;
							sd.Usage = D3D11_USAGE_STAGING;
							sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
							winrt::com_ptr<ID3D11Texture2D> staging;
							if (SUCCEEDED(device->CreateTexture2D(&sd, nullptr, staging.put()))) {
								const D3D11_BOX box{ 0, 0, 0, fw, fh, 1 };
								context->CopySubresourceRegion(
									staging.get(), 0, 0, 0, 0, aoStatsTex->resource.get(), 0, &box);
								D3D11_MAPPED_SUBRESOURCE m{};
								if (SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &m))) {
									double rawSum = 0.0;
									double finalSum = 0.0;
									double occludedSum = 0.0;
									std::uint32_t n = 0;
									std::uint32_t occludedN = 0;
									std::uint8_t lo = 255;
									std::uint8_t hi = 0;
									for (std::uint32_t y = 0; y < fh; y += 8u) {
										const auto* rowPtr = static_cast<const std::uint8_t*>(m.pData) +
											static_cast<std::size_t>(y) * m.RowPitch;
										for (std::uint32_t x = 0; x < fw; x += 8u) {
											const std::uint8_t v = rowPtr[x];
											const double occ = static_cast<double>(v) / 255.0;
											const double finalOcc = 1.0 - std::pow(1.0 - occ, static_cast<double>(_settings.aoPower));
											lo = (std::min)(lo, v);
											hi = (std::max)(hi, v);
											rawSum += occ;
											finalSum += finalOcc;
											if (occ > 0.01) {
												occludedSum += occ;
												++occludedN;
											}
											++n;
										}
									}
									context->Unmap(staging.get(), 0);
									if (n > 0u) {
										L->info(
											"SSGI AO occ stats (one-shot): rawMean={:.3f} finalMean={:.3f} occludedMean={:.3f} min={:.3f} max={:.3f} (0=open 1=occluded, {} samples).",
											rawSum / static_cast<double>(n),
											finalSum / static_cast<double>(n),
											occludedN > 0u ? occludedSum / static_cast<double>(occludedN) : 0.0,
											static_cast<double>(lo) / 255.0,
											static_cast<double>(hi) / 255.0,
											n);
									}
								}
							}
						}
					}
				} else if (!_xegtaoInnerFailLogged) {
					_xegtaoInnerFailLogged = true;
					L->warn(
						"SSGI XeGTAO inner-gate fail (real AO skipped): frameW={} frameH={} depthSRV={} normalSRV={}.",
						frameW, frameH, depthSRV != nullptr, normalSRV != nullptr);
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

	void ScreenSpaceGI::OnKssaoOverwrite(int a_anchor)
	{
		if (!_settings.kssaoProbeEnabled || a_anchor != _settings.kssaoProbeAnchor) {
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

		if (!_kssaoOverwriteFiredLogged) {
			_kssaoOverwriteFiredLogged = true;
			L->info(
				"SSGI kSSAO-probe: overwrite fired anchor={} mode={} allFinal={}.",
				a_anchor,
				_settings.kssaoProbeMode,
				_settings.kssaoProbeAllFinal ? 1 : 0);
		}

		const auto target = static_cast<cs::engine::RenderTarget>(_settings.kssaoProbeRt);

		if (_settings.kssaoProbeMode == 0) {
			const float value = _settings.kssaoProbeValue;
			const float color[4] = { value, value, value, value };
			const bool logThis = !_kssaoOverwriteMode0Logged;
			_kssaoOverwriteMode0Logged = true;
			// SAO buffers are compute-written (UAV-only), so their cached RTV is usually
			// null; fall back to a UAV clear so the write actually lands. Mirrors the UAV
			// path the real-AO overwrite (OverwriteRt) already uses.
			auto clearRt = [&](cs::engine::RenderTarget a_rt) {
				auto* rtv = cs::engine::GetRenderTargetRTV(a_rt);
				auto* uav = cs::engine::GetRenderTargetUAV(a_rt);
				const char* via = "SKIPPED";
				if (rtv) {
					context->ClearRenderTargetView(rtv, color);
					via = "RTV";
				} else if (uav) {
					context->ClearUnorderedAccessViewFloat(uav, color);
					via = "UAV";
				}
				if (logThis) {
					L->info(
						"SSGI kSSAO-probe: mode0 rt={} rtv={} uav={} cleared-via={}.",
						static_cast<int>(a_rt),
						rtv != nullptr,
						uav != nullptr,
						via);
				}
			};
			if (_settings.kssaoProbeAllFinal) {
				clearRt(cs::engine::RenderTarget::kSSAOFinal);
				clearRt(cs::engine::RenderTarget::kSSAOFinalSwap);
				clearRt(cs::engine::RenderTarget::kSSAOFinalSwap2);
			} else {
				clearRt(target);
			}
			return;
		}

		// mode 3 (diagnostic pattern) writes a fixed spatial signal and does not need real AO.
		if (_settings.kssaoProbeMode != 3 && (!IsReady() || !_aoTexture)) {
			if (!_kssaoNotReadyLogged) {
				_kssaoNotReadyLogged = true;
				L->warn("SSGI kSSAO-probe: AO is not ready; skipping mode {}.", _settings.kssaoProbeMode);
			}
			return;
		}
		_kssaoNotReadyLogged = false;

		// The SAO-Final family (45/46/47) is triple-buffered and the buffer the ambient
		// composite samples can rotate per frame, so honor kssao_probe_all_final by writing
		// all three; otherwise write the single configured target.
		if (_settings.kssaoProbeAllFinal) {
			for (const auto rt : {
					 cs::engine::RenderTarget::kSSAOFinal,
					 cs::engine::RenderTarget::kSSAOFinalSwap,
					 cs::engine::RenderTarget::kSSAOFinalSwap2 }) {
				OverwriteRt(rt);
			}
		} else {
			OverwriteRt(target);
		}
	}

	void ScreenSpaceGI::OverwriteRt(cs::engine::RenderTarget a_target)
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
		const bool needSRV = _settings.kssaoProbeMode == 2;  // only the blend path reads the engine SRV
		if (!targetTexture || !targetUAV || !rtm || !_kssaoOverwriteCB || (needSRV && !targetSRV)) {
			if (!_kssaoOverwriteSkipLogged) {
				_kssaoOverwriteSkipLogged = true;
				L->warn(
					"SSGI kSSAO-probe: OverwriteRt SKIPPED rt={} mode={} texture={} uav={} srv={} rtm={} cb={}.",
					static_cast<int>(a_target),
					_settings.kssaoProbeMode,
					targetTexture != nullptr,
					targetUAV != nullptr,
					targetSRV != nullptr,
					rtm != nullptr,
					_kssaoOverwriteCB != nullptr);
			}
			return;
		}

		D3D11_TEXTURE2D_DESC targetDesc{};
		targetTexture->GetDesc(&targetDesc);
		D3D11_UNORDERED_ACCESS_VIEW_DESC targetUAVDesc{};
		targetUAV->GetDesc(&targetUAVDesc);
		const std::uint32_t targetComponents = KssaoTargetComponents(targetUAVDesc.Format);
		if (targetComponents == 0 || targetUAVDesc.ViewDimension != D3D11_UAV_DIMENSION_TEXTURE2D ||
			targetDesc.SampleDesc.Count != 1) {
			if (!_kssaoUnsupportedLogged) {
				_kssaoUnsupportedLogged = true;
				L->warn(
					"SSGI kSSAO-probe: unsupported target format={} view={} samples={}.",
					static_cast<int>(targetUAVDesc.Format),
					static_cast<int>(targetUAVDesc.ViewDimension),
					targetDesc.SampleDesc.Count);
			}
			return;
		}
		auto* overwriteCS = _kssaoOverwriteCS[targetComponents - 1].get();
		if (!overwriteCS) {
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

		const bool copyCompatible =
			_settings.kssaoProbeMode == 1 &&
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
			if (_settings.kssaoProbeMode == 2) {
				D3D11_SHADER_RESOURCE_VIEW_DESC engineSRVDesc{};
				targetSRV->GetDesc(&engineSRVDesc);
				if (engineSRVDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) {
					return;
				}

				if (!_kssaoScratch ||
					_kssaoScratchW != targetDesc.Width ||
					_kssaoScratchH != targetDesc.Height ||
					_kssaoScratchFormat != targetDesc.Format) {
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

					_kssaoScratch = std::move(scratch);
					_kssaoScratchSRV = std::move(scratchSRV);
					_kssaoScratchW = targetDesc.Width;
					_kssaoScratchH = targetDesc.Height;
					_kssaoScratchFormat = targetDesc.Format;
				}

				const D3D11_BOX sourceBox{ 0, 0, 0, targetW, targetH, 1 };
				context->CopySubresourceRegion(
					_kssaoScratch.get(), 0, 0, 0, 0, targetTexture, 0, &sourceBox);
				engineAO = _kssaoScratchSRV.get();
			}

			KssaoOverwriteCB cb{};
			cb.TargetExtent[0] = targetW;
			cb.TargetExtent[1] = targetH;
			cb.SourceExtent[0] = sourceW;
			cb.SourceExtent[1] = sourceH;
			cb.Mode = static_cast<std::uint32_t>(_settings.kssaoProbeMode);
			_kssaoOverwriteCB->Update(cb);

			ID3D11ShaderResourceView* srvs[2] = { engineAO, _aoTexture ? _aoTexture->srv.get() : nullptr };
			ID3D11Buffer* buffers[1] = { _kssaoOverwriteCB->CB() };
			ID3D11UnorderedAccessView* uavs[1] = { targetUAV };
			context->CSSetShaderResources(0, 2, srvs);
			context->CSSetConstantBuffers(0, 1, buffers);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetShader(overwriteCS, nullptr, 0);
			context->Dispatch((targetW + 7u) / 8u, (targetH + 7u) / 8u, 1);
			if (!_kssaoDispatchLoggedOnce) {
				_kssaoDispatchLoggedOnce = true;
				L->info(
					"SSGI kSSAO-probe: dispatch wrote {}x{} to rt={} mode={}.",
					targetW,
					targetH,
					static_cast<int>(a_target),
					_settings.kssaoProbeMode);
			}
		} catch (const std::exception& e) {
			L->warn("SSGI kSSAO-probe overwrite failed: {}.", e.what());
		} catch (...) {
			L->warn("SSGI kSSAO-probe overwrite failed.");
		}
	}

	void ScreenSpaceGI::OnKssaoReadback()
	{
		if (!_settings.kssaoProbeEnabled) {
			return;
		}
		if (!_kssaoReadbackEnteredLogged) {
			_kssaoReadbackEnteredLogged = true;
			L->info("SSGI kSSAO-probe: readback ENTERED (composite anchor fired).");
		}

		// One-shot: dump the PS SRV bindings live at the post-composite anchor (the
		// composite draw has just run, so t9=AO is still bound here -- unlike the
		// PSSetShader-time observer, which sees a stale/partial set). Match each bound
		// SRV against the full engine RT pool so we learn the exact enum feeding t9.
		if (!_compositeSlotMapLogged) {
			auto* rd = RE::BSGraphics::GetRendererData();
			auto* ctx = rd ? reinterpret_cast<ID3D11DeviceContext*>(rd->context) : nullptr;
			if (ctx) {
				_compositeSlotMapLogged = true;
				ID3D11ShaderResourceView* srvs[16] = {};
				ctx->PSGetShaderResources(0, 16, srvs);
				const uint rtCount = static_cast<uint>(cs::engine::RenderTarget::kCount);
				std::string slotMap;
				for (uint slot = 0; slot < 16; ++slot) {
					if (!srvs[slot]) {
						continue;
					}
					winrt::com_ptr<ID3D11ShaderResourceView> ownedSrv;
					ownedSrv.attach(srvs[slot]);
					winrt::com_ptr<ID3D11Resource> res;
					ownedSrv->GetResource(res.put());
					if (!res) {
						continue;
					}
					int matchedRt = -1;
					for (uint rt = 0; rt < rtCount; ++rt) {
						auto* rtRes = reinterpret_cast<ID3D11Resource*>(rd->renderTargets[rt].texture);
						if (rtRes && rtRes == res.get()) {
							matchedRt = static_cast<int>(rt);
							break;
						}
					}
					uint w = 0;
					uint h = 0;
					uint fmt = 0;
					winrt::com_ptr<ID3D11Texture2D> tex;
					if (SUCCEEDED(res->QueryInterface(IID_PPV_ARGS(tex.put())))) {
						D3D11_TEXTURE2D_DESC d{};
						tex->GetDesc(&d);
						w = d.Width;
						h = d.Height;
						fmt = static_cast<uint>(d.Format);
					}
					std::string views;
					if (matchedRt >= 0) {
						const auto& e = rd->renderTargets[static_cast<uint>(matchedRt)];
						views = std::string(" rtv") + (e.rtView ? "1" : "0") +
						        " srv" + (e.srView ? "1" : "0") +
						        " uav" + (e.uaView ? "1" : "0");
					}
					slotMap += " t" + std::to_string(slot) + "=RT" +
					           (matchedRt >= 0 ? std::to_string(matchedRt) : std::string("none")) +
					           "(" + std::to_string(w) + "x" + std::to_string(h) + " fmt" + std::to_string(fmt) + views + ")";
				}
					// Also capture the composite's OUTPUT RTV(s) -> pool enum. The AO
					// multiplies only the composite's output, never the inputs it samples
					// (e.g. RT3 at t10 is prior-lit INPUT), so the luma readback must target
					// this output RT to see any AO effect.
					{
						ID3D11RenderTargetView* rtvs[8] = {};
						ID3D11DepthStencilView* dsv = nullptr;
						ctx->OMGetRenderTargets(8, rtvs, &dsv);
						for (uint o = 0; o < 8; ++o) {
							if (!rtvs[o]) {
								continue;
							}
							winrt::com_ptr<ID3D11RenderTargetView> ownedRtv;
							ownedRtv.attach(rtvs[o]);
							winrt::com_ptr<ID3D11Resource> ores;
							ownedRtv->GetResource(ores.put());
							if (!ores) {
								continue;
							}
							int matchedOut = -1;
							for (uint rt = 0; rt < rtCount; ++rt) {
								auto* rtRes = reinterpret_cast<ID3D11Resource*>(rd->renderTargets[rt].texture);
								if (rtRes && rtRes == ores.get()) {
									matchedOut = static_cast<int>(rt);
									break;
								}
							}
							uint ow = 0;
							uint oh = 0;
							uint ofmt = 0;
							winrt::com_ptr<ID3D11Texture2D> otex;
							if (SUCCEEDED(ores->QueryInterface(IID_PPV_ARGS(otex.put())))) {
								D3D11_TEXTURE2D_DESC d{};
								otex->GetDesc(&d);
								ow = d.Width;
								oh = d.Height;
								ofmt = static_cast<uint>(d.Format);
							}
							slotMap += " o" + std::to_string(o) + "=RT" +
							           (matchedOut >= 0 ? std::to_string(matchedOut) : std::string("none")) +
							           "(" + std::to_string(ow) + "x" + std::to_string(oh) + " fmt" + std::to_string(ofmt) + ")";
						}
						if (dsv) {
							dsv->Release();
						}
					}
					L->info("SSGI composite-slot-map (post-composite):{}", slotMap);
			}
		}
		if ((_kssaoReadbackFrame++ % kLumaReadbackInterval) != 0) {
			return;
		}

		auto* rendererData = RE::BSGraphics::GetRendererData();
		auto* rtm = cs::engine::GetRenderTargetManager();
		if (!rendererData || !rtm) {
			if (!_kssaoReadbackNullRtmLogged) {
				_kssaoReadbackNullRtmLogged = true;
				L->warn(
					"SSGI kSSAO-probe: readback abort (rendererData_null={} rtm_null={}).",
					!rendererData,
					!rtm);
			}
			return;
		}
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		const auto lumaRt = static_cast<cs::engine::RenderTarget>(_settings.kssaoProbeLumaRt);
		const auto lumaRtIndex = static_cast<uint>(_settings.kssaoProbeLumaRt);
		auto* frameBuffer = (lumaRtIndex < static_cast<uint>(cs::engine::RenderTarget::kCount))
			? reinterpret_cast<ID3D11Texture2D*>(rendererData->renderTargets[lumaRtIndex].texture)
			: nullptr;
		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		if (!context || !frameBuffer || !device) {
			if (!_kssaoReadbackNullDeviceLogged) {
				_kssaoReadbackNullDeviceLogged = true;
				L->warn(
					"SSGI kSSAO-probe: readback abort (lumaRt={} context_null={} frameBuffer_null={} device_null={}).",
					_settings.kssaoProbeLumaRt,
					!context,
					!frameBuffer,
					!device);
			}
			return;
		}

		D3D11_TEXTURE2D_DESC frameDesc{};
		frameBuffer->GetDesc(&frameDesc);
		DXGI_FORMAT readFormat = frameDesc.Format;
		if (auto* frameBufferRTV = cs::engine::GetRenderTargetRTV(lumaRt)) {
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			frameBufferRTV->GetDesc(&rtvDesc);
			if (rtvDesc.Format != DXGI_FORMAT_UNKNOWN) {
				readFormat = rtvDesc.Format;
			}
		}
		const std::size_t texelBytes = LumaTexelBytes(readFormat);
		if (texelBytes == 0 || frameDesc.SampleDesc.Count != 1) {
			if (!_kssaoReadbackUnsupportedLogged) {
				_kssaoReadbackUnsupportedLogged = true;
				L->warn(
					"SSGI kSSAO-probe: unsupported framebuffer format={} viewFormat={} samples={}.",
					static_cast<int>(frameDesc.Format),
					static_cast<int>(readFormat),
					frameDesc.SampleDesc.Count);
			}
			return;
		}

		const std::uint32_t activeW = std::min(
			frameDesc.Width,
			static_cast<std::uint32_t>(
				static_cast<float>(frameDesc.Width) * cs::engine::dynres::GetWidthRatio(rtm)));
		const std::uint32_t activeH = std::min(
			frameDesc.Height,
			static_cast<std::uint32_t>(
				static_cast<float>(frameDesc.Height) * cs::engine::dynres::GetHeightRatio(rtm)));
		const std::uint32_t sampleW = std::min(kLumaSampleWidth, activeW);
		const std::uint32_t sampleH = std::min(kLumaSampleHeight, activeH);
		if (sampleW == 0 || sampleH == 0) {
			if (!_kssaoReadbackZeroExtentLogged) {
				_kssaoReadbackZeroExtentLogged = true;
				L->warn(
					"SSGI kSSAO-probe: readback abort (activeW={} activeH={} frameW={} frameH={}).",
					activeW,
					activeH,
					frameDesc.Width,
					frameDesc.Height);
			}
			return;
		}

		try {
			if (!_kssaoLumaStaging ||
				_kssaoLumaW != sampleW ||
				_kssaoLumaH != sampleH ||
				_kssaoLumaFormat != frameDesc.Format) {
				D3D11_TEXTURE2D_DESC stagingDesc{};
				stagingDesc.Width = sampleW;
				stagingDesc.Height = sampleH;
				stagingDesc.MipLevels = 1;
				stagingDesc.ArraySize = 1;
				stagingDesc.Format = frameDesc.Format;
				stagingDesc.SampleDesc.Count = 1;
				stagingDesc.Usage = D3D11_USAGE_STAGING;
				stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

				winrt::com_ptr<ID3D11Texture2D> staging;
				DX::ThrowIfFailed(device->CreateTexture2D(&stagingDesc, nullptr, staging.put()));
				_kssaoLumaStaging = std::move(staging);
				_kssaoLumaW = sampleW;
				_kssaoLumaH = sampleH;
				_kssaoLumaFormat = frameDesc.Format;
			}

			const std::uint32_t left = (activeW - sampleW) / 2;
			const std::uint32_t top = (activeH - sampleH) / 2;
			const D3D11_BOX sourceBox{ left, top, 0, left + sampleW, top + sampleH, 1 };
			{
				cs::engine::OMScope scope(context);
				context->CopySubresourceRegion(
					_kssaoLumaStaging.get(), 0, 0, 0, 0, frameBuffer, 0, &sourceBox);
			}

			D3D11_MAPPED_SUBRESOURCE mapped{};
			DX::ThrowIfFailed(context->Map(_kssaoLumaStaging.get(), 0, D3D11_MAP_READ, 0, &mapped));
			bool mappedStaging = true;
			try {
				double luminanceSum = 0.0;
				const std::uint32_t seamColumn = sampleW / 2;
				std::vector<float> leftLum;
				std::vector<float> rightLum;
				leftLum.reserve(static_cast<std::size_t>(seamColumn) * sampleH);
				rightLum.reserve(static_cast<std::size_t>(sampleW - seamColumn) * sampleH);
				for (std::uint32_t y = 0; y < sampleH; ++y) {
					const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
						static_cast<std::size_t>(y) * mapped.RowPitch;
					for (std::uint32_t x = 0; x < sampleW; ++x) {
						float luminance{};
						if (!ReadLuminance(row + static_cast<std::size_t>(x) * texelBytes, readFormat, luminance)) {
							throw std::runtime_error("framebuffer luminance decode failed");
						}
						luminanceSum += static_cast<double>(luminance);
						(x < seamColumn ? leftLum : rightLum).push_back(luminance);
					}
				}
				context->Unmap(_kssaoLumaStaging.get(), 0);
				mappedStaging = false;

				const double luminanceMean =
					luminanceSum / static_cast<double>(sampleW * sampleH);
				// Within-frame half-split: the readback band is centered on screen, so its left
				// columns fall on the left half of the frame (screen x < center) and its right
				// columns on the right half. When a probe writes a left/right AO pattern (or an
				// injected AO the composite consumes), lumaLeft < lumaRight IN THE SAME FRAME
				// proves the target/anchor reaches the composite; weather / time-of-day / sun are
				// common-mode and cancel. p10 per half isolates the shadowed, ambient-dominated
				// tail, which moves most when the ambient AO term changes.
				const auto halfStats = [](std::vector<float>& a_v, double& a_mean, float& a_p10) {
					if (a_v.empty()) {
						a_mean = 0.0;
						a_p10 = 0.0f;
						return;
					}
					double sum = 0.0;
					for (const float v : a_v) {
						sum += static_cast<double>(v);
					}
					a_mean = sum / static_cast<double>(a_v.size());
					std::sort(a_v.begin(), a_v.end());
					a_p10 = a_v[static_cast<std::size_t>(0.1 * static_cast<double>(a_v.size() - 1))];
				};
				double leftMean = 0.0;
				double rightMean = 0.0;
				float leftP10 = 0.0f;
				float rightP10 = 0.0f;
				halfStats(leftLum, leftMean, leftP10);
				halfStats(rightLum, rightMean, rightP10);
				L->info(
					"SSGI kSSAO-probe: enabled=1 mode={} rt={} allFinal={} anchor={} value={:.3f} lumaRt={} lumaMean={:.6f} lumaLeftMean={:.6f} lumaLeftP10={:.6f} lumaRightMean={:.6f} lumaRightP10={:.6f} frame={}",
					_settings.kssaoProbeMode,
					_settings.kssaoProbeRt,
					_settings.kssaoProbeAllFinal ? 1 : 0,
					_settings.kssaoProbeAnchor,
					_settings.kssaoProbeValue,
					_settings.kssaoProbeLumaRt,
					luminanceMean,
					leftMean,
					leftP10,
					rightMean,
					rightP10,
					_kssaoReadbackFrame);
			} catch (...) {
				if (mappedStaging) {
					context->Unmap(_kssaoLumaStaging.get(), 0);
				}
				throw;
			}
		} catch (const std::exception& e) {
			L->warn("SSGI kSSAO-probe readback failed: {}.", e.what());
		} catch (...) {
			L->warn("SSGI kSSAO-probe readback failed.");
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
		if (_dumpArmed.load(std::memory_order_relaxed)) {
			L->info(
				"SSGI identity-match draw {}: boundPS={:p} ambientPS={:p} identityMatch={}",
				_dumpOrdinal,
				static_cast<const void*>(boundPS),
				static_cast<const void*>(ambientPS),
				isAmbientPass);
			if (isAmbientPass) {
				++_dumpIdentityMatches;
			}
		}
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
