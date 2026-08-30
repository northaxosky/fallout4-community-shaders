#include "WaterEffects.h"

#include <DirectXTex.h>
#include <d3d11.h>
#include <imgui.h>

#include <array>
#include <exception>
#include <format>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/Engine.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/UI.h"

namespace cs::features
{
	namespace we = cs::features::water_effects;

	namespace
	{
		auto* L = cs::log::Get("cs.feature.watereffects");

		constexpr const wchar_t* kCausticsPath =
			L"Data\\Shaders\\WaterEffects\\watercaustics.dds";

		constexpr std::uint32_t kModeDisabled = 0;
		constexpr std::uint32_t kModeNormal = 1;
		constexpr std::uint32_t kModeCaustics = 2;
		constexpr std::uint32_t kModeSubmersion = 3;

		constexpr std::array<FeatureDebugView, 2> kDebugViews{ {
			{
				"water_caustics",
				"Caustics multiplier on submerged surfaces",
				FeatureDebugViewKind::kFullscreen
			},
			{
				"water_submersion",
				"Depth below the cell water plane",
				FeatureDebugViewKind::kFullscreen
			}
		} };

		ID3D11DeviceContext* GetImmediateContext() noexcept
		{
			auto* rendererData = RE::BSGraphics::GetRendererData();
			return rendererData ?
				reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
				nullptr;
		}

		std::string_view DebugVisualizationName(
			WaterEffects::DebugVisualization a_visualization) noexcept
		{
			switch (a_visualization) {
			case WaterEffects::DebugVisualization::kCaustics:
				return "water_caustics";
			case WaterEffects::DebugVisualization::kSubmersion:
				return "water_submersion";
			default:
				return "off";
			}
		}
	}

	WaterEffects* WaterEffects::GetSingleton()
	{
		static WaterEffects instance;
		return &instance;
	}

	std::span<const FeatureDebugView>
		WaterEffects::GetDebugViews() const noexcept
	{
		return kDebugViews;
	}

	void WaterEffects::SetDebugView(std::string_view a_view) noexcept
	{
		auto visualization = DebugVisualization::kOff;
		if (a_view == "water_caustics")
			visualization = DebugVisualization::kCaustics;
		else if (a_view == "water_submersion")
			visualization = DebugVisualization::kSubmersion;
		_debugVisualization.store(visualization, std::memory_order_release);
	}

	bool WaterEffects::Configure(
		const toml::table& a_config,
		std::string& a_error)
	{
		a_error.clear();
		const auto* settingsNode = a_config.get("settings");
		if (!settingsNode) {
			PublishSettings();
			return true;
		}
		const auto* settingsTable = settingsNode->as_table();
		if (!settingsTable) {
			a_error = "settings: expected table";
			return false;
		}

		auto candidate = _settings;
		const auto status =
			feature_config::ReadBool(*settingsTable, "enabled", candidate.enabled);
		if (status != feature_config::ScalarReadStatus::kMissing
			&& status != feature_config::ScalarReadStatus::kValid) {
			a_error = "settings.enabled: expected boolean";
			return false;
		}
		_settings = we::Clamp(candidate);
		return true;
	}

	void WaterEffects::PublishSettings() noexcept
	{
		_enabled.store(_settings.enabled, std::memory_order_release);
	}

	void WaterEffects::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		if (const auto result =
				feature_config::UpdateFeatureSettings(GetConfigKey(), settings);
			!result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void WaterEffects::Load()
	{
		PublishSettings();

		const auto registerContribution = [this](
			cs::engine::ShaderInjectionTarget a_target,
			cs::engine::ShaderInjectionBindCallback a_bind,
			bool a_fullscreenDebug) {
			cs::engine::ShaderInjectionDefines defines{
				{ cs::engine::shader_injection_defines::kWaterEffects, "1" }
			};
			std::vector<cs::engine::ShaderSlotClaim> slotClaims{
				{
					.stage = cs::engine::ShaderStage::kPixel,
					.resourceType = cs::engine::ShaderResourceType::kShaderResource,
					.slot = kCausticsPSSlot
				},
				{
					.stage = cs::engine::ShaderStage::kPixel,
					.resourceType = cs::engine::ShaderResourceType::kShaderResource,
					.slot = kSceneDepthPSSlot
				}
			};
			if (a_fullscreenDebug) {
				defines.emplace(
					cs::engine::shader_injection_defines::kWaterEffectsFullscreenDebug,
					"1");
			} else {
				// See WaterCausticsSampler.hlsli.
				slotClaims.push_back({
					.stage = cs::engine::ShaderStage::kPixel,
					.resourceType = cs::engine::ShaderResourceType::kSampler,
					.slot = kCausticsSamplerPSSlot
				});
			}
			return cs::engine::RegisterReplacement({
				.targetId = a_target,
				.contributor = "WaterEffects",
				.defines = std::move(defines),
				.isReady = [this] {
					return _registrationsReady.load(std::memory_order_acquire)
						&& _resourcesReady.load(std::memory_order_acquire)
						&& cs::render::IsSharedDataReady();
				},
				.bind = std::move(a_bind),
				.slotClaims = std::move(slotClaims)
			});
		};

		if (!registerContribution(
				cs::engine::ShaderInjectionTarget::kBsdfLight,
				[this](ID3D11DeviceContext* a_context) {
					BindCaustics(a_context);
				},
				false)) {
			FailLoad(
				"Water caustics multiply through the reconstructed BSDFLight shader; "
				"registering that replacement failed, so there is no delivery path");
			return;
		}
		if (!registerContribution(
				cs::engine::ShaderInjectionTarget::kBsdfComposite,
				[this](ID3D11DeviceContext* a_context) {
					BindDebugTextures(a_context);
				},
				true)) {
			FailLoad(
				"Water caustics debug views replace BSDFComposite output; "
				"registering that replacement failed");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);

		cs::engine::RegisterPreDeferredLightsImpl(
			[] { WaterEffects::GetSingleton()->SaveEngineBindings(); },
			cs::engine::HookPriority::Early);
		cs::engine::RegisterPostDeferredLightsImpl(
			[] { WaterEffects::GetSingleton()->RestoreEngineBindings(); },
			cs::engine::HookPriority::Late);
		if (!cs::engine::RegisterPreDeferredComposite(
				[] { WaterEffects::GetSingleton()->SaveDebugBindings(); },
				cs::engine::HookPriority::Early)
			|| !cs::engine::RegisterPostDeferredComposite(
				[] { WaterEffects::GetSingleton()->RestoreDebugBindings(); },
				cs::engine::HookPriority::Late)) {
			FailLoad(
				"Water caustics debug views need a deferred-composite binding scope");
			return;
		}
		_renderCallbacksReady.store(true, std::memory_order_release);

		L->info(
			"Water caustics installed: hooks=deferred_lights+deferred_composite, "
			"consumers=BSDFLight+BSDFComposite t{}+t{}/s{}, enabled={}.",
			kCausticsPSSlot,
			kSceneDepthPSSlot,
			kCausticsSamplerPSSlot,
			_settings.enabled);
	}

	bool WaterEffects::BuildCausticsResources(
		ID3D11Device* a_device,
		std::string& a_error)
	{
		a_error.clear();
		if (!a_device) {
			a_error = "no D3D11 device";
			return false;
		}

		DirectX::ScratchImage loaded;
		DirectX::TexMetadata metadata{};
		const auto loadResult = DirectX::LoadFromDDSFile(
			kCausticsPath, DirectX::DDS_FLAGS_NONE, &metadata, loaded);
		if (FAILED(loadResult)) {
			a_error = std::format(
				"could not read Data\\Shaders\\WaterEffects\\watercaustics.dds "
				"(HRESULT 0x{:08X})",
				static_cast<std::uint32_t>(loadResult));
			return false;
		}
		if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D
			|| metadata.arraySize != 1) {
			a_error = "watercaustics.dds is not a single 2D image";
			return false;
		}

		const auto viewResult = DirectX::CreateShaderResourceView(
			a_device,
			loaded.GetImages(),
			loaded.GetImageCount(),
			metadata,
			_causticsSrv.put());
		if (FAILED(viewResult)) {
			a_error = std::format(
				"could not create the caustics shader resource view "
				"(HRESULT 0x{:08X})",
				static_cast<std::uint32_t>(viewResult));
			return false;
		}

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		const auto samplerResult = a_device->CreateSamplerState(
			&samplerDesc, _causticsSampler.put());
		if (FAILED(samplerResult)) {
			a_error = std::format(
				"could not create the caustics sampler (HRESULT 0x{:08X})",
				static_cast<std::uint32_t>(samplerResult));
			return false;
		}
		return true;
	}

	void WaterEffects::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		std::string error;
		bool built = false;
		try {
			built = BuildCausticsResources(a_device, error);
		} catch (const std::exception& e) {
			error = e.what();
		} catch (...) {
			error = "unknown failure";
		}
		if (!built) {
			_causticsSrv = nullptr;
			_causticsSampler = nullptr;
			SetValidationDetail(error);
			L->error("Water caustics resources failed: {}", error);
			return;
		}
		_resourcesReady.store(true, std::memory_order_release);
		L->info("Water caustics texture and sampler ready.");
	}

	void WaterEffects::SetValidationDetail(std::string a_detail)
	{
		const std::lock_guard lock(_validationMutex);
		_validationDetail = std::move(a_detail);
	}

	std::string WaterEffects::GetValidationDetail() const
	{
		const std::lock_guard lock(_validationMutex);
		return _validationDetail;
	}

	bool WaterEffects::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the shader contribution did not register";
			SetValidationDetail(a_error);
			return false;
		}
		if (!_renderCallbacksReady.load(std::memory_order_acquire)) {
			a_error = "the deferred binding scopes did not install";
			SetValidationDetail(a_error);
			return false;
		}
		if (!_resourcesReady.load(std::memory_order_acquire)) {
			const auto detail = GetValidationDetail();
			a_error = detail.empty() ?
				"the caustics texture is unavailable" :
				"the caustics texture is unavailable: " + detail;
			SetValidationDetail(a_error);
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error =
				"the shared substrate is unavailable, so b6 carries no water plane";
			SetValidationDetail(a_error);
			return false;
		}

		for (const auto target : {
				 cs::engine::ShaderInjectionTarget::kBsdfLight,
				 cs::engine::ShaderInjectionTarget::kBsdfComposite }) {
			const auto snapshot =
				cs::engine::GetShaderInjectionTargetSnapshot(target);
			const auto define = snapshot.defines.find(
				cs::engine::shader_injection_defines::kWaterEffects);
			const bool contributed =
				define != snapshot.defines.end() && define->second == "1";
			if (!snapshot.requested
				|| !snapshot.compileComplete
				|| !snapshot.swappable
				|| snapshot.slotCollision
				|| !contributed) {
				a_error = "'" + snapshot.name
					+ "' cannot deliver water caustics (requested="
					+ std::to_string(snapshot.requested)
					+ " compile_complete="
					+ std::to_string(snapshot.compileComplete)
					+ " swappable=" + std::to_string(snapshot.swappable)
					+ " slot_collision=" + std::to_string(snapshot.slotCollision)
					+ " contributed=" + std::to_string(contributed) + ")";
				SetValidationDetail(a_error);
				return false;
			}
		}

		SetValidationDetail({});
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	bool WaterEffects::CanBind() const noexcept
	{
		return _injectionsOperational.load(std::memory_order_acquire)
			&& _enabled.load(std::memory_order_acquire)
			&& _resourcesReady.load(std::memory_order_acquire)
			&& _causticsSrv
			&& _causticsSampler;
	}

	void WaterEffects::SaveEngineBindings()
	{
		auto* context = GetImmediateContext();
		if (!context)
			return;
		_engineBinding.Save(context, kCausticsPSSlot);
		_engineSamplerBinding.Save(context, kCausticsSamplerPSSlot);
		ID3D11ShaderResourceView* nullSRVs[2]{};
		context->PSSetShaderResources(kCausticsPSSlot, 2, nullSRVs);
		ID3D11SamplerState* nullSampler = nullptr;
		context->PSSetSamplers(kCausticsSamplerPSSlot, 1, &nullSampler);
	}

	void WaterEffects::BindCaustics(ID3D11DeviceContext* a_context)
	{
		if (!a_context || !CanBind())
			return;
		ID3D11ShaderResourceView* srvs[2]{ _causticsSrv.get(), nullptr };
		a_context->PSSetShaderResources(kCausticsPSSlot, 2, srvs);
		ID3D11SamplerState* sampler = _causticsSampler.get();
		a_context->PSSetSamplers(kCausticsSamplerPSSlot, 1, &sampler);
		_binds.fetch_add(1, std::memory_order_relaxed);
	}

	void WaterEffects::RestoreEngineBindings()
	{
		auto* context = GetImmediateContext();
		_engineSamplerBinding.Restore(context);
		_engineBinding.Restore(context);
	}

	void WaterEffects::SaveDebugBindings()
	{
		if (_debugVisualization.load(std::memory_order_acquire)
			== DebugVisualization::kOff) {
			return;
		}
		auto* context = GetImmediateContext();
		if (!context)
			return;
		_debugBinding.Save(context, kCausticsPSSlot);
		ID3D11ShaderResourceView* nullSRVs[2]{};
		context->PSSetShaderResources(kCausticsPSSlot, 2, nullSRVs);
	}

	void WaterEffects::BindDebugTextures(ID3D11DeviceContext* a_context)
	{
		if (!a_context
			|| _debugVisualization.load(std::memory_order_acquire)
				== DebugVisualization::kOff
			|| !CanBind()) {
			return;
		}
		auto* depthSrv = cs::engine::GetSceneDepthSRV();
		ID3D11ShaderResourceView* srvs[2]{ _causticsSrv.get(), depthSrv };
		a_context->PSSetShaderResources(kCausticsPSSlot, 2, srvs);
		_debugBinds.fetch_add(1, std::memory_order_relaxed);
		if (!depthSrv)
			_debugDepthMissing.fetch_add(1, std::memory_order_relaxed);
	}

	void WaterEffects::RestoreDebugBindings()
	{
		auto* context = GetImmediateContext();
		_debugBinding.Restore(context);
	}

	cs::WaterEffectsFeatureData WaterEffects::GetCommonBufferData() const
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->GetParentCell() : nullptr;
		bool hasWater = cell && cell->HasWater();
		// Cells that inherit water height from the worldspace store a sentinel
		// in waterHeight; the engine's own accessor resolves the inheritance.
		float waterHeight = hasWater ? player->GetRelevantWaterHeight() : we::kNoWaterHeight;
		if (hasWater && !we::IsUsableWaterHeight(waterHeight)) {
			hasWater = false;
			waterHeight = we::kNoWaterHeight;
		}
		_hasWater.store(hasWater, std::memory_order_relaxed);
		_waterHeight.store(waterHeight, std::memory_order_relaxed);

		// Same predicate as the bind path: publishing a live mode while the
		// texture stays unbound would multiply sunlight by zero.
		if (!CanBind())
			return {};

		std::uint32_t mode = kModeNormal;
		switch (_debugVisualization.load(std::memory_order_acquire)) {
		case DebugVisualization::kCaustics:
			mode = kModeCaustics;
			break;
		case DebugVisualization::kSubmersion:
			mode = kModeSubmersion;
			break;
		default:
			break;
		}

		return {
			.Mode = mode,
			.HasWater = hasWater ? 1U : 0U,
			.WaterHeight = waterHeight
		};
	}

	void WaterEffects::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const auto lightSnapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto detail = GetValidationDetail();
		a_sink
			.Field("configured_enabled", _enabled.load(std::memory_order_relaxed))
			.Field("has_water", _hasWater.load(std::memory_order_relaxed))
			.Field(
				"water_height",
				static_cast<double>(_waterHeight.load(std::memory_order_relaxed)))
			.Field(
				"debug_mode",
				DebugVisualizationName(
					_debugVisualization.load(std::memory_order_relaxed)))
			.Field(
				"registrations_ready",
				_registrationsReady.load(std::memory_order_relaxed))
			.Field(
				"render_callbacks_ready",
				_renderCallbacksReady.load(std::memory_order_relaxed))
			.Field("resources_ready", _resourcesReady.load(std::memory_order_relaxed))
			.Field("shared_data_ready", cs::render::IsSharedDataReady())
			.Field(
				"injection_operational",
				_injectionsOperational.load(std::memory_order_relaxed))
			.Field("injection_requested", lightSnapshot.requested)
			.Field("injection_compile_complete", lightSnapshot.compileComplete)
			.Field(
				"injection_compile_error",
				lightSnapshot.compileError.empty() ?
					"none" :
					lightSnapshot.compileError)
			.Field("injection_swappable", lightSnapshot.swappable)
			.Field("injection_slot_collision", lightSnapshot.slotCollision)
			.Field(
				"caustics_binds",
				static_cast<std::int64_t>(_binds.load(std::memory_order_relaxed)))
			.Field(
				"debug_binds",
				static_cast<std::int64_t>(
					_debugBinds.load(std::memory_order_relaxed)))
			.Field(
				"debug_depth_missing",
				static_cast<std::int64_t>(
					_debugDepthMissing.load(std::memory_order_relaxed)))
			.Field(
				"validation_detail",
				detail.empty() ? "operational" : detail);
	}

	void WaterEffects::DrawSettings()
	{
		if (ImGui::Checkbox("Enabled", &_settings.enabled)) {
			_settings = we::Clamp(_settings);
			PublishSettings();
			SaveSettings();
		}
		ImGui::TextDisabled(
			"Upstream ships no caustics tunables; every constant is fixed.");

		if (_injectionsOperational.load(std::memory_order_relaxed)) {
			if (_hasWater.load(std::memory_order_relaxed)) {
				ImGui::TextDisabled(
					"Cell water plane: z = %.1f",
					_waterHeight.load(std::memory_order_relaxed));
			} else {
				ImGui::TextDisabled("Current cell has no water plane.");
			}
		} else {
			const auto detail = GetValidationDetail();
			ImGui::TextDisabled(
				"Inactive: %s",
				detail.empty() ?
					"shader delivery path unavailable" :
					detail.c_str());
		}

		Menu::Get().DrawDebugViewSelector(*this);
		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s",
				"Debug views fetch the caustics texture manually because the "
				"composite has no free sampler slot, so they do not reproduce "
				"the light path's mip selection.");
		}
	}

	void WaterEffects::RestoreDefaultSettings()
	{
		_settings = Settings{};
		PublishSettings();
		SaveSettings();
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(WaterEffects::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
