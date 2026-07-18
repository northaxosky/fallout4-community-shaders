#include "ScreenSpaceGI.h"

#include <d3d11.h>
#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

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

		std::unique_ptr<cs::buffer::Texture2D> CreateOutputTexture(
			std::uint32_t a_width,
			std::uint32_t a_height)
		{
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = a_width;
			textureDesc.Height = a_height;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			auto texture = std::make_unique<cs::buffer::Texture2D>(textureDesc);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = textureDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			texture->CreateSRV(srvDesc);

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = textureDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			texture->CreateUAV(uavDesc);

			return texture;
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

			_resourcesReady.store(false, std::memory_order_release);
			_resolveCB = std::move(resolveCB);
			_bounceTexture = std::move(bounceTexture);
			_aoTexture = std::move(aoTexture);
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
