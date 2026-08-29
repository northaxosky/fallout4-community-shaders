#include "TerrainShadows.h"

#include <DirectXTex.h>
#include <d3d11.h>
#include <imgui.h>

#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include "Log.h"
#include "LogThrottle.h"
#include "Menu/Menu.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "World/Sky.h"

namespace cs::features
{
	namespace ts = cs::features::terrain_shadows;

	namespace
	{
		auto* L = cs::log::Get("cs.feature.terrainshadows");

		constexpr const wchar_t* kShadowUpdatePath =
			L"Data\\Shaders\\TerrainShadows\\ShadowUpdate.cs.hlsl";
		constexpr const wchar_t* kXLodGenRoot = L"Data\\Textures\\Terrain";
		constexpr const wchar_t* kCustomRoot = L"Data\\Textures\\HeightMaps";

		constexpr std::uint32_t kMissingMapLogIntervalMs = 30000;
		constexpr std::array<FeatureDebugView, 2> kDebugViews{ {
			{ "shadow_term", "Shadow term" },
			{ "heightmap", "Raw heightmap sample" }
		} };

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

		std::string_view DebugVisualizationName(
			TerrainShadows::DebugVisualization a_visualization) noexcept
		{
			switch (a_visualization) {
			case TerrainShadows::DebugVisualization::kShadowTerm:
				return "shadow_term";
			case TerrainShadows::DebugVisualization::kHeightmap:
				return "heightmap";
			default:
				return "off";
			}
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			TerrainShadows::Settings& a_candidate,
			std::string& a_error)
		{
			a_error.clear();
			const auto* settingsNode = a_config.get("settings");
			if (!settingsNode)
				return true;

			const auto* settingsTable = settingsNode->as_table();
			if (!settingsTable) {
				a_error = "settings: expected table";
				return false;
			}

			if (!AcceptSetting(
					feature_config::ReadBool(
						*settingsTable, "enabled", a_candidate.enabled),
					"enabled",
					"boolean",
					a_error)) {
				return false;
			}

			auto factor = static_cast<std::uint64_t>(a_candidate.downsampleFactor);
			const auto factorStatus = feature_config::ReadUnsignedInteger(
				*settingsTable,
				"downsample_factor",
				factor,
				ts::kDownsampleFactors.front(),
				ts::kDownsampleFactors.back());
			if (!AcceptSetting(
					factorStatus, "downsample_factor", "integer", a_error)) {
				return false;
			}
			if (factorStatus == feature_config::ScalarReadStatus::kValid) {
				if (!ts::IsValidDownsampleFactor(factor)) {
					a_error = SettingError(
						"downsample_factor",
						"expected one of 1, 2, or 4");
					return false;
				}
				a_candidate.downsampleFactor = static_cast<std::uint32_t>(factor);
			}
			return true;
		}

		ID3D11DeviceContext* GetImmediateContext() noexcept
		{
			auto* rendererData = RE::BSGraphics::GetRendererData();
			return rendererData ?
				reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) :
				nullptr;
		}

		// Force light propagation downward.
		bool TryGetDescendingSunDirection(std::array<float, 3>& a_out) noexcept
		{
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
			if (!cs::engine::TryGetSunDirectionWS(x, y, z))
				return false;
			if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
				return false;
			if (z > 0.0f) {
				x = -x;
				y = -y;
				z = -z;
			}
			a_out = { x, y, z };
			return true;
		}

		std::string DescribeMetadata(const ts::HeightMapMetadata& a_metadata)
		{
			return std::format(
				"extent=({:.0f},{:.0f})..({:.0f},{:.0f}) z_encode=({:.0f}..{:.0f}) z_range=({:.0f}..{:.0f})",
				a_metadata.pos0[0],
				a_metadata.pos0[1],
				a_metadata.pos1[0],
				a_metadata.pos1[1],
				a_metadata.pos0[2],
				a_metadata.pos1[2],
				a_metadata.zRange[0],
				a_metadata.zRange[1]);
		}
	}

	TerrainShadows* TerrainShadows::GetSingleton()
	{
		static TerrainShadows instance;
		return &instance;
	}

	std::span<const FeatureDebugView> TerrainShadows::GetDebugViews() const noexcept
	{
		return kDebugViews;
	}

	void TerrainShadows::SetDebugView(std::string_view a_view) noexcept
	{
		DebugVisualization visualization = DebugVisualization::kOff;
		if (a_view == "shadow_term")
			visualization = DebugVisualization::kShadowTerm;
		else if (a_view == "heightmap")
			visualization = DebugVisualization::kHeightmap;
		_debugVisualization.store(visualization, std::memory_order_release);
	}

	bool TerrainShadows::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error))
			return false;
		_settings = candidate;
		PublishSettings();
		return true;
	}

	void TerrainShadows::PublishSettings()
	{
		_enabled.store(_settings.enabled, std::memory_order_release);
		_requestedDownsampleFactor.store(
			ts::IsValidDownsampleFactor(_settings.downsampleFactor) ?
				_settings.downsampleFactor :
				1u,
			std::memory_order_release);
	}

	void TerrainShadows::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign(
			"downsample_factor",
			static_cast<std::int64_t>(_settings.downsampleFactor));
		if (const auto result =
				feature_config::UpdateFeatureSettings(GetConfigKey(), settings);
			!result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void TerrainShadows::Load()
	{
		const auto registerContribution = [this](
			cs::engine::ShaderInjectionTarget a_target,
			cs::engine::ShaderInjectionBindCallback a_bind) {
			return cs::engine::RegisterReplacement({
				.targetId = a_target,
				.contributor = "TerrainShadows",
				.defines = {
					{ cs::engine::shader_injection_defines::kTerrainShadows, "1" }
				},
				.isReady = [this] {
					return ts::IsReadyForInjectionFreeze(GetBootstrapReadiness());
				},
				.bind = std::move(a_bind),
				.slotClaims = {
					{
						.stage = cs::engine::ShaderStage::kPixel,
						.resourceType = cs::engine::ShaderResourceType::kShaderResource,
						.slot = kShadowHeightPSSlot
					},
					{
						.stage = cs::engine::ShaderStage::kPixel,
						.resourceType = cs::engine::ShaderResourceType::kSampler,
						.slot = kShadowHeightSamplerPSSlot
					}
				}
			});
		};

		if (!registerContribution(
				cs::engine::ShaderInjectionTarget::kBsdfLight,
				[this](ID3D11DeviceContext* a_context) {
					BindShadowHeights(a_context);
				})) {
			FailLoad(
				"Terrain shadows multiply through the reconstructed BSDFLight shader; "
				"registering that replacement failed, so there is no delivery path");
			return;
		}
		if (!registerContribution(
				cs::engine::ShaderInjectionTarget::kBsdfComposite,
				[this](ID3D11DeviceContext* a_context) {
					BindDebugTexture(a_context);
				})) {
			FailLoad(
				"Terrain shadow debug views replace BSDFComposite output; "
				"registering that replacement failed");
			return;
		}
		_registrationsReady.store(true, std::memory_order_release);

		if (!cs::engine::RegisterPostDeferredPrePass(
				[] { TerrainShadows::GetSingleton()->OnPostDeferredPrePass(); },
				cs::engine::HookPriority::Default)) {
			FailLoad(
				"Terrain shadows update after the deferred prepass; registering that "
				"anchor failed");
			return;
		}
		cs::engine::RegisterPreDeferredLightsImpl(
			[] { TerrainShadows::GetSingleton()->SaveEngineBindings(); },
			cs::engine::HookPriority::Early);
		cs::engine::RegisterPostDeferredLightsImpl(
			[] { TerrainShadows::GetSingleton()->RestoreEngineBindings(); },
			cs::engine::HookPriority::Late);
		if (!cs::engine::RegisterPreDeferredComposite(
				[] { TerrainShadows::GetSingleton()->SaveDebugBindings(); },
				cs::engine::HookPriority::Early)
			|| !cs::engine::RegisterPostDeferredComposite(
				[] { TerrainShadows::GetSingleton()->RestoreDebugBindings(); },
				cs::engine::HookPriority::Late)) {
			FailLoad(
				"Terrain shadow debug views need a deferred-composite binding scope");
			return;
		}
		_renderCallbacksReady.store(true, std::memory_order_release);
		_started.store(true, std::memory_order_release);

		L->info(
			"Terrain shadows installed: hooks=post_deferred_prepass+deferred_lights+"
			"deferred_composite, consumers=BSDFLight+BSDFComposite t{}/s{}, "
			"enabled={}, downsample_factor={}, debug_views=shadow_term+heightmap.",
			kShadowHeightPSSlot,
			kShadowHeightSamplerPSSlot,
			_settings.enabled,
			_settings.downsampleFactor);
	}

	terrain_shadows::BootstrapReadiness
		TerrainShadows::GetBootstrapReadiness() const noexcept
	{
		return {
			.registrationsInstalled =
				_registrationsReady.load(std::memory_order_acquire),
			.renderCallbacksInstalled =
				_renderCallbacksReady.load(std::memory_order_acquire),
			.computeShaderReady = _computeShaderReady.load(std::memory_order_acquire),
			.samplerReady = _samplerReady.load(std::memory_order_acquire),
			.constantBufferReady =
				_constantBufferReady.load(std::memory_order_acquire)
		};
	}

	void TerrainShadows::ConsiderHeightMapFile(
		const std::filesystem::path& a_path,
		ts::HeightMapSource a_source)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(a_path, ec) || ec)
			return;
		const auto extension = a_path.extension().string();
		if (!ts::EqualsIgnoreCase(extension, ".dds"))
			return;

		const auto stem = a_path.stem().string();
		auto metadata = ts::ParseHeightMapStem(stem, a_source);
		if (!metadata) {
			L->debug("Ignoring '{}': not a usable heightmap name.", stem);
			return;
		}

		const auto key = metadata->worldspace;
		// Custom maps override xLODGen output.
		if (const auto existing = _heightMaps.find(key);
			existing != _heightMaps.end()) {
			if (existing->second.metadata.source == ts::HeightMapSource::kCustom
				&& a_source == ts::HeightMapSource::kXLodGen) {
				return;
			}
			L->warn(
				"Worldspace '{}' has more than one {} heightmap; '{}' replaces '{}'.",
				key,
				ts::SourceName(a_source),
				a_path.filename().string(),
				existing->second.path.filename().string());
		}
		L->info(
			"Discovered {} heightmap for '{}': {} ({}).",
			ts::SourceName(a_source),
			key,
			a_path.filename().string(),
			DescribeMetadata(*metadata));
		_heightMaps[key] = HeightMapRecord{ std::move(*metadata), a_path };
	}

	void TerrainShadows::ScanHeightMapDirectory(
		const std::filesystem::path& a_directory,
		ts::HeightMapSource a_source,
		bool a_recurseOneLevel)
	{
		std::error_code ec;
		std::filesystem::directory_iterator iterator{ a_directory, ec };
		if (ec)
			return;
		for (const auto& entry : iterator) {
			const auto& path = entry.path();
			std::error_code entryEc;
			if (a_recurseOneLevel
				&& std::filesystem::is_directory(path, entryEc)
				&& !entryEc) {
				std::error_code innerEc;
				std::filesystem::directory_iterator inner{ path, innerEc };
				if (innerEc)
					continue;
				for (const auto& innerEntry : inner)
					ConsiderHeightMapFile(innerEntry.path(), a_source);
				continue;
			}
			if (!a_recurseOneLevel)
				ConsiderHeightMapFile(path, a_source);
		}
	}

	void TerrainShadows::DiscoverHeightMaps()
	{
		_heightMaps.clear();
		try {
			ScanHeightMapDirectory(
				kXLodGenRoot, ts::HeightMapSource::kXLodGen, true);
			ScanHeightMapDirectory(
				kCustomRoot, ts::HeightMapSource::kCustom, false);
		} catch (const std::exception& e) {
			L->error("Heightmap discovery failed: {}", e.what());
		} catch (...) {
			L->error("Heightmap discovery failed.");
		}
		_discoveredMaps.store(_heightMaps.size(), std::memory_order_relaxed);
		L->info("Heightmap discovery found {} worldspace map(s).", _heightMaps.size());
	}

	void TerrainShadows::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		if (!_started.load(std::memory_order_acquire) || !a_device)
			return;
		if (_resourceInitFailed)
			return;

		DiscoverHeightMaps();

		try {
			const auto bufferDesc =
				cs::buffer::ConstantBufferDesc<ShadowUpdateCB>();
			DX::ThrowIfFailed(a_device->CreateBuffer(
				&bufferDesc, nullptr, _shadowUpdateCB.put()));
			_constantBufferReady.store(true, std::memory_order_release);

			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxAnisotropy = 1;
			samplerDesc.MinLOD = 0.0f;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(a_device->CreateSamplerState(
				&samplerDesc, _linearClampSampler.put()));
			_samplerReady.store(true, std::memory_order_release);
		} catch (const std::exception& e) {
			_resourceInitFailed = true;
			L->error("Terrain shadow bootstrap resources failed: {}", e.what());
			return;
		} catch (...) {
			_resourceInitFailed = true;
			L->error("Terrain shadow bootstrap resources failed.");
			return;
		}

		_shadowUpdateCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
			cs::util::CompileShader(kShadowUpdatePath, {}, "cs_5_0")));
		if (!_shadowUpdateCS) {
			_resourceInitFailed = true;
			L->error(
				"Terrain shadow update compute shader failed to compile; the feature "
				"cannot produce shadow heights.");
			return;
		}
		_computeShaderReady.store(true, std::memory_order_release);
	}

	bool TerrainShadows::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		const auto readiness = GetBootstrapReadiness();
		if (!ts::IsReadyForInjectionFreeze(readiness)) {
			a_error = "terrain shadow bootstrap is incomplete ("
				+ ts::MissingBootstrapPrerequisites(readiness) + ")";
			_validationDetail = a_error;
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error =
				"the shared substrate is unavailable, so b5 and b6 carry no terrain shadow data";
			_validationDetail = a_error;
			return false;
		}

		constexpr std::array targets{
			cs::engine::ShaderInjectionTarget::kBsdfLight,
			cs::engine::ShaderInjectionTarget::kBsdfComposite
		};
		for (const auto target : targets) {
			const auto snapshot =
				cs::engine::GetShaderInjectionTargetSnapshot(target);
			const auto define = snapshot.defines.find(
				cs::engine::shader_injection_defines::kTerrainShadows);
			const bool contributed =
				define != snapshot.defines.end() && define->second == "1";
			if (!snapshot.requested
				|| !snapshot.compileComplete
				|| !snapshot.swappable
				|| snapshot.slotCollision
				|| !contributed) {
				a_error = "'" + snapshot.name
					+ "' cannot deliver terrain shadows (requested="
					+ std::to_string(snapshot.requested)
					+ " compile_complete=" + std::to_string(snapshot.compileComplete)
					+ " swappable=" + std::to_string(snapshot.swappable)
					+ " slot_collision=" + std::to_string(snapshot.slotCollision)
					+ " contributed=" + std::to_string(contributed) + ")";
				_validationDetail = a_error;
				return false;
			}
		}

		_validationDetail.clear();
		_injectionsOperational.store(true, std::memory_order_release);
		return true;
	}

	std::string TerrainShadows::ResolveWorldspaceEditorId()
	{
		auto* tes = RE::TES::GetSingleton();
		auto* worldspace = tes ? tes->worldSpace : nullptr;
		// Heightmaps follow inherited land data.
		while (worldspace) {
			auto* parent = worldspace->GetParentWorld(
				RE::TESWorldSpace::PARENT_USE_FLAG::kLand);
			if (!parent || parent == worldspace)
				break;
			worldspace = parent;
		}
		if (!worldspace)
			return {};
		return std::string(std::string_view(worldspace->editorID));
	}

	bool TerrainShadows::PollGameHourJump()
	{
		auto* calendar = RE::Calendar::GetSingleton();
		auto* gameHour = calendar ? calendar->gameHour : nullptr;
		if (!gameHour)
			return false;
		const float current = gameHour->value;
		if (!std::isfinite(current))
			return false;
		if (!_gameHourSeeded) {
			_gameHourSeeded = true;
			_lastGameHour = current;
			return false;
		}
		const bool jumped = ts::IsGameHourJump(_lastGameHour, current);
		_lastGameHour = current;
		return jumped;
	}

	void TerrainShadows::ReleaseLiveResources(ID3D11DeviceContext* a_context)
	{
		_shadowResourcesReady.store(false, std::memory_order_release);
		_shadowPopulated.store(false, std::memory_order_release);
		_mapLoaded.store(false, std::memory_order_release);
		if (a_context) {
			ID3D11ShaderResourceView* nullSRV = nullptr;
			a_context->PSSetShaderResources(kShadowHeightPSSlot, 1, &nullSRV);
		}
		_shadowTexture.reset();
		_heightTexture.reset();
		_loadedWorldspace.clear();
		_loadedMetadata = {};
		_plan = {};
		_shadowUpdateIndex = 0;
		_slicesSinceRebuild = 0;
		_allocatedBytes.store(0, std::memory_order_relaxed);
		_effectiveWidth.store(0, std::memory_order_relaxed);
		_effectiveHeight.store(0, std::memory_order_relaxed);
		_sourceWidth.store(0, std::memory_order_relaxed);
		_sourceHeight.store(0, std::memory_order_relaxed);
	}

	bool TerrainShadows::BuildHeightResources(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		const HeightMapRecord& a_record,
		std::uint32_t a_factor,
		std::string& a_error)
	{
		a_error.clear();
		DirectX::ScratchImage loaded;
		DirectX::TexMetadata metadata{};
		// Expanding L16 converts it to four-channel R16G16B16A16.
		const auto loadResult = DirectX::LoadFromDDSFile(
				a_record.path.c_str(),
				DirectX::DDS_FLAGS_NONE,
				&metadata,
				loaded);
		if (FAILED(loadResult)) {
			a_error = std::format(
				"DirectXTex could not read the DDS (HRESULT 0x{:08X})",
				static_cast<std::uint32_t>(loadResult));
			return false;
		}
		if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D
			|| metadata.arraySize != 1
			|| metadata.depth != 1) {
			a_error = "the DDS is not a single 2D image";
			return false;
		}
		if (metadata.format != DXGI_FORMAT_R16_UNORM) {
			a_error = std::format(
				"expected DXGI_FORMAT_R16_UNORM after DDS decode; saw DXGI format {}",
				static_cast<std::uint32_t>(metadata.format));
			return false;
		}
		if (metadata.width == 0
			|| metadata.height == 0
			|| metadata.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
			|| metadata.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
			a_error = "the DDS extent is outside the D3D11 texture limits";
			return false;
		}

		const auto sourceWidth = static_cast<std::uint32_t>(metadata.width);
		const auto sourceHeight = static_cast<std::uint32_t>(metadata.height);
		auto effectiveWidth = ts::ApplyDownsample(sourceWidth, a_factor);
		auto effectiveHeight = ts::ApplyDownsample(sourceHeight, a_factor);

		const auto* mip0 = loaded.GetImage(0, 0, 0);
		if (!mip0) {
			a_error = "the DDS carries no base image";
			return false;
		}
		const auto expectedRowPitch =
			static_cast<std::size_t>(sourceWidth) * sizeof(std::uint16_t);
		const auto expectedSlicePitch =
			expectedRowPitch * static_cast<std::size_t>(sourceHeight);
		if (mip0->rowPitch != expectedRowPitch
			|| mip0->slicePitch < expectedSlicePitch) {
			a_error = std::format(
				"unexpected R16 layout: row pitch {} (expected {}), "
				"slice pitch {} (expected at least {})",
				mip0->rowPitch,
				expectedRowPitch,
				mip0->slicePitch,
				expectedSlicePitch);
			return false;
		}

		DirectX::ScratchImage resized;
		if (effectiveWidth != sourceWidth || effectiveHeight != sourceHeight) {
			if (FAILED(DirectX::Resize(
					*mip0,
					effectiveWidth,
					effectiveHeight,
					DirectX::TEX_FILTER_FANT,
					resized))) {
				a_error = "the heightmap could not be downsampled";
				return false;
			}
			mip0 = resized.GetImage(0, 0, 0);
			if (!mip0) {
				a_error = "the resized image is unavailable";
				return false;
			}
		}

		DirectX::TexMetadata single = metadata;
		single.width = effectiveWidth;
		single.height = effectiveHeight;
		single.depth = 1;
		single.arraySize = 1;
		single.mipLevels = 1;

		winrt::com_ptr<ID3D11Resource> resource;
		if (FAILED(DirectX::CreateTexture(
				a_device, mip0, 1, single, resource.put()))) {
			a_error = "the heightmap texture could not be created";
			return false;
		}
		winrt::com_ptr<ID3D11Texture2D> heightTexture;
		{
			ID3D11Texture2D* raw = nullptr;
			if (FAILED(resource->QueryInterface(
					__uuidof(ID3D11Texture2D),
					reinterpret_cast<void**>(&raw)))) {
				a_error = "the heightmap resource is not a 2D texture";
				return false;
			}
			heightTexture.attach(raw);
		}

		ReleaseLiveResources(a_context);

		auto height = std::make_unique<cs::buffer::Texture2D>(heightTexture.detach());
		D3D11_SHADER_RESOURCE_VIEW_DESC heightSrv{};
		heightSrv.Format = height->desc.Format;
		heightSrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		heightSrv.Texture2D.MostDetailedMip = 0;
		heightSrv.Texture2D.MipLevels = 1;
		height->CreateSRV(heightSrv);

		D3D11_TEXTURE2D_DESC shadowDesc{};
		shadowDesc.Width = effectiveWidth;
		shadowDesc.Height = effectiveHeight;
		shadowDesc.MipLevels = 1;
		shadowDesc.ArraySize = 1;
		shadowDesc.Format = DXGI_FORMAT_R16G16_UNORM;
		shadowDesc.SampleDesc.Count = 1;
		shadowDesc.Usage = D3D11_USAGE_DEFAULT;
		shadowDesc.BindFlags =
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		auto shadow = std::make_unique<cs::buffer::Texture2D>(shadowDesc);
		D3D11_SHADER_RESOURCE_VIEW_DESC shadowSrv{};
		shadowSrv.Format = shadowDesc.Format;
		shadowSrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		shadowSrv.Texture2D.MostDetailedMip = 0;
		shadowSrv.Texture2D.MipLevels = 1;
		shadow->CreateSRV(shadowSrv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC shadowUav{};
		shadowUav.Format = shadowDesc.Format;
		shadowUav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		shadowUav.Texture2D.MipSlice = 0;
		shadow->CreateUAV(shadowUav);
		// D3D leaves UAV contents undefined.
		if (a_context && shadow->uav) {
			const float clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			a_context->ClearUnorderedAccessViewFloat(shadow->uav.get(), clear);
		}

		_heightTexture = std::move(height);
		_shadowTexture = std::move(shadow);
		_loadedMetadata = a_record.metadata;
		_loadedWorldspace = a_record.metadata.worldspace;
		_appliedFactor = a_factor;
		_shadowUpdateIndex = 0;
		_slicesSinceRebuild = 0;
		_pendingFullRefresh = true;
		_plan = {};

		const auto cost = ts::ComputeVramCost(effectiveWidth, effectiveHeight);
		_sourceWidth.store(sourceWidth, std::memory_order_relaxed);
		_sourceHeight.store(sourceHeight, std::memory_order_relaxed);
		_effectiveWidth.store(effectiveWidth, std::memory_order_relaxed);
		_effectiveHeight.store(effectiveHeight, std::memory_order_relaxed);
		_appliedFactorTelemetry.store(a_factor, std::memory_order_relaxed);
		_allocatedBytes.store(cost.totalBytes, std::memory_order_relaxed);
		_mapLoaded.store(true, std::memory_order_release);
		_shadowResourcesReady.store(true, std::memory_order_release);

		L->info(
			"Loaded {} heightmap for '{}': source={}x{}, effective={}x{}, "
			"downsample_factor={}, height={:.2f} MiB, shadow={:.2f} MiB, total={:.2f} MiB.",
			ts::SourceName(a_record.metadata.source),
			a_record.metadata.worldspace,
			sourceWidth,
			sourceHeight,
			effectiveWidth,
			effectiveHeight,
			a_factor,
			ts::BytesToMiB(cost.heightBytes),
			ts::BytesToMiB(cost.shadowBytes),
			ts::BytesToMiB(cost.totalBytes));
		return true;
	}

	void TerrainShadows::EnsureLiveResources(ID3D11DeviceContext* a_context)
	{
		const auto worldspace = ResolveWorldspaceEditorId();
		if (worldspace.empty()) {
			if (_shadowResourcesReady.load(std::memory_order_acquire))
				ReleaseLiveResources(a_context);
			PublishStatus({}, "no exterior worldspace");
			return;
		}

		const auto factor = _requestedDownsampleFactor.load(std::memory_order_acquire);
		if (_shadowResourcesReady.load(std::memory_order_acquire)
			&& _loadedWorldspace == worldspace
			&& _appliedFactor == factor) {
			PublishStatus(worldspace, "loaded");
			return;
		}

		const auto record = _heightMaps.find(worldspace);
		if (record == _heightMaps.end()) {
			PublishStatus(worldspace, "no heightmap for this worldspace");
			CS_LOG_EVERY_MS(
				L,
				kMissingMapLogIntervalMs,
				spdlog::level::info,
				"No terrain heightmap for worldspace '{}'; terrain shadows publish identity.",
				worldspace);
			if (_shadowResourcesReady.load(std::memory_order_acquire))
				ReleaseLiveResources(a_context);
			return;
		}

		auto* device = cs::util::GetD3DDevice();
		if (!device) {
			PublishStatus(worldspace, "no D3D11 device");
			return;
		}
		// Do not retry the same failed DDS every frame.
		if (_failedWorldspace == worldspace && _failedFactor == factor) {
			PublishStatus(worldspace, "heightmap load failed");
			return;
		}

		std::string error;
		bool built = false;
		try {
			built = BuildHeightResources(
				device, a_context, record->second, factor, error);
		} catch (const std::exception& e) {
			error = e.what();
		} catch (...) {
			error = "unknown D3D failure";
		}
		if (!built) {
			_failedWorldspace = worldspace;
			_failedFactor = factor;
			PublishStatus(worldspace, "heightmap load failed: " + error);
			CS_LOG_EVERY_MS(
				L,
				kMissingMapLogIntervalMs,
				spdlog::level::err,
				"Terrain heightmap '{}' for worldspace '{}' could not be loaded: {}.",
				record->second.path.filename().string(),
				worldspace,
				error);
			ReleaseLiveResources(a_context);
			return;
		}
		_failedWorldspace.clear();
		PublishStatus(worldspace, "loaded");
	}

	bool TerrainShadows::UpdateShadow(
		ID3D11DeviceContext* a_context,
		bool a_refreshImmediately)
	{
		if (!a_context
			|| !_shadowResourcesReady.load(std::memory_order_acquire)
			|| !_heightTexture
			|| !_shadowTexture
			|| !_heightTexture->srv
			|| !_shadowTexture->uav
			|| !_shadowUpdateCS
			|| !_shadowUpdateCB) {
			return false;
		}

		std::array<float, 3> sunDirection{};
		if (!TryGetDescendingSunDirection(sunDirection))
			return false;

		const auto width = _heightTexture->desc.Width;
		const auto height = _heightTexture->desc.Height;
		if (a_refreshImmediately)
			_shadowUpdateIndex = 0;
		// Keep the sweep direction stable across a cycle.
		if (_shadowUpdateIndex == 0 || !_plan.valid)
			_plan = ts::BuildDdaPlan(sunDirection, _loadedMetadata, width, height);
		if (!_plan.valid)
			return false;
		if (_plan.dispatchCount == 0
			|| _plan.dispatchCount
				> D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION) {
			CS_LOG_EVERY_MS(
				L,
				kMissingMapLogIntervalMs,
				spdlog::level::err,
				"Terrain shadow dispatch width {} exceeds the D3D11 limit; skipping updates.",
				_plan.dispatchCount);
			return false;
		}

		ShadowUpdateCB data{};
		data.LightPxDir[0] = _plan.lightPxDir[0];
		data.LightPxDir[1] = _plan.lightPxDir[1];
		data.LightDeltaZ[0] = _plan.lightDeltaZ[0];
		data.LightDeltaZ[1] = _plan.lightDeltaZ[1];
		data.PxSize[0] = 1.0f / static_cast<float>(width);
		data.PxSize[1] = 1.0f / static_cast<float>(height);
		data.BlendWeight = a_refreshImmediately ? 1.0f : 0.5f;
		data.PosRange[0] = _loadedMetadata.pos0[2];
		data.PosRange[1] = _loadedMetadata.pos1[2];
		data.ZRange[0] = _loadedMetadata.zRange[0];
		data.ZRange[1] = _loadedMetadata.zRange[1];

		const std::uint32_t updateCount =
			a_refreshImmediately ? _plan.maxUpdates : 1u;
		cs::engine::ComputeOMScope scope(a_context, 1, 0, 1, 1);
		ID3D11ShaderResourceView* srvs[1] = { _heightTexture->srv.get() };
		ID3D11UnorderedAccessView* uavs[1] = { _shadowTexture->uav.get() };
		ID3D11Buffer* buffers[1] = { _shadowUpdateCB.get() };
		a_context->CSSetShaderResources(0, 1, srvs);
		a_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		a_context->CSSetConstantBuffers(0, 1, buffers);
		a_context->CSSetShader(_shadowUpdateCS.get(), nullptr, 0);

		for (std::uint32_t update = 0; update < updateCount; ++update) {
			data.StartPxCoord = ts::SliceStartCoord(_plan, _shadowUpdateIndex);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(a_context->Map(
					_shadowUpdateCB.get(),
					0,
					D3D11_MAP_WRITE_DISCARD,
					0,
					&mapped))) {
				CS_LOG_EVERY_MS(
					L,
					kMissingMapLogIntervalMs,
					spdlog::level::err,
					"Terrain shadow constant-buffer map failed; skipping this slice.");
				return false;
			}
			std::memcpy(mapped.pData, &data, sizeof(data));
			a_context->Unmap(_shadowUpdateCB.get(), 0);
			a_context->Dispatch(_plan.dispatchCount, 1, 1);
			_shadowUpdateIndex = (_shadowUpdateIndex + 1) % _plan.maxUpdates;
			if (_slicesSinceRebuild < _plan.maxUpdates)
				++_slicesSinceRebuild;
			_dispatches.fetch_add(1, std::memory_order_relaxed);
		}

		_updates.fetch_add(1, std::memory_order_relaxed);
		if (a_refreshImmediately)
			_fullRefreshes.fetch_add(1, std::memory_order_relaxed);
		if (_slicesSinceRebuild >= _plan.maxUpdates)
			_shadowPopulated.store(true, std::memory_order_release);
		return true;
	}

	void TerrainShadows::OnPostDeferredPrePass()
	{
		if (!_started.load(std::memory_order_acquire))
			return;
		auto* context = GetImmediateContext();
		if (!context)
			return;
		_prepassRuns.fetch_add(1, std::memory_order_relaxed);

		// Render hooks do not catch callback exceptions.
		try {
			const bool timeJump = PollGameHourJump();
			EnsureLiveResources(context);

			const bool enabled = _enabled.load(std::memory_order_acquire)
				&& _injectionsOperational.load(std::memory_order_acquire);
			if (enabled && _shadowResourcesReady.load(std::memory_order_acquire)) {
				const bool refresh = _pendingFullRefresh || timeJump;
				if (UpdateShadow(context, refresh))
					_pendingFullRefresh = false;
				else
					_pendingFullRefresh = _pendingFullRefresh || refresh;
			} else if (timeJump) {
				_pendingFullRefresh = true;
			}

		} catch (const std::exception& e) {
			CS_LOG_EVERY_MS(
				L,
				kMissingMapLogIntervalMs,
				spdlog::level::err,
				"Terrain shadow update failed: {}.",
				e.what());
		} catch (...) {
			CS_LOG_EVERY_MS(
				L,
				kMissingMapLogIntervalMs,
				spdlog::level::err,
				"Terrain shadow update failed.");
		}
	}

	void TerrainShadows::SaveEngineBindings()
	{
		auto* context = GetImmediateContext();
		if (!context)
			return;
		if (!_engineShadowBinding.Save(context, kShadowHeightPSSlot)
			&& _engineShadowBinding.IsSaved()) {
			CS_LOG_ONCE(
				L,
				spdlog::level::err,
				"Terrain shadow t{} binding scopes overlap; preserving the active snapshot.",
				kShadowHeightPSSlot);
		}
		_engineSamplerBinding.Save(context, kShadowHeightSamplerPSSlot);
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(kShadowHeightPSSlot, 1, &nullSRV);
		ID3D11SamplerState* nullSampler = nullptr;
		context->PSSetSamplers(kShadowHeightSamplerPSSlot, 1, &nullSampler);
	}

	void TerrainShadows::BindShadowHeights(ID3D11DeviceContext* a_context)
	{
		if (!a_context
			|| !_injectionsOperational.load(std::memory_order_acquire)
			|| !_enabled.load(std::memory_order_acquire)
			|| !_mapLoaded.load(std::memory_order_acquire)
			|| !_shadowResourcesReady.load(std::memory_order_acquire)
			|| !_shadowPopulated.load(std::memory_order_acquire)
			|| !_shadowTexture
			|| !_shadowTexture->srv
			|| !_linearClampSampler) {
			return;
		}
		ID3D11ShaderResourceView* srv = _shadowTexture->srv.get();
		a_context->PSSetShaderResources(kShadowHeightPSSlot, 1, &srv);
		ID3D11SamplerState* sampler = _linearClampSampler.get();
		a_context->PSSetSamplers(kShadowHeightSamplerPSSlot, 1, &sampler);
		_binds.fetch_add(1, std::memory_order_relaxed);
		_samplerBinds.fetch_add(1, std::memory_order_relaxed);
	}

	void TerrainShadows::RestoreEngineBindings()
	{
		auto* context = GetImmediateContext();
		_engineSamplerBinding.Restore(context);
		_engineShadowBinding.Restore(context);
	}

	void TerrainShadows::SaveDebugBindings()
	{
		if (_debugVisualization.load(std::memory_order_acquire)
			== DebugVisualization::kOff) {
			return;
		}
		auto* context = GetImmediateContext();
		if (!context)
			return;
		_debugShadowBinding.Save(context, kShadowHeightPSSlot);
		_debugSamplerBinding.Save(context, kShadowHeightSamplerPSSlot);
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(kShadowHeightPSSlot, 1, &nullSRV);
		ID3D11SamplerState* nullSampler = nullptr;
		context->PSSetSamplers(kShadowHeightSamplerPSSlot, 1, &nullSampler);
	}

	void TerrainShadows::BindDebugTexture(ID3D11DeviceContext* a_context)
	{
		const auto visualization =
			_debugVisualization.load(std::memory_order_acquire);
		const bool heightmap =
			visualization == DebugVisualization::kHeightmap;
		if (!a_context
			|| visualization == DebugVisualization::kOff
			|| !_injectionsOperational.load(std::memory_order_acquire)
			|| !_enabled.load(std::memory_order_acquire)
			|| !_mapLoaded.load(std::memory_order_acquire)
			|| !_shadowResourcesReady.load(std::memory_order_acquire)
			|| (!heightmap
				&& !_shadowPopulated.load(std::memory_order_acquire))
			|| (heightmap ?
					(!_heightTexture || !_heightTexture->srv) :
					(!_shadowTexture || !_shadowTexture->srv))
			|| !_linearClampSampler) {
			return;
		}
		ID3D11ShaderResourceView* srv = heightmap ?
			_heightTexture->srv.get() :
			_shadowTexture->srv.get();
		a_context->PSSetShaderResources(kShadowHeightPSSlot, 1, &srv);
		ID3D11SamplerState* sampler = _linearClampSampler.get();
		a_context->PSSetSamplers(kShadowHeightSamplerPSSlot, 1, &sampler);
		_debugBinds.fetch_add(1, std::memory_order_relaxed);
	}

	void TerrainShadows::RestoreDebugBindings()
	{
		auto* context = GetImmediateContext();
		_debugSamplerBinding.Restore(context);
		_debugShadowBinding.Restore(context);
	}

	cs::TerrainShadowsFeatureData TerrainShadows::GetCommonBufferData() const
	{
		const auto debugVisualization =
			_debugVisualization.load(std::memory_order_acquire);
		if (!_injectionsOperational.load(std::memory_order_acquire)
			|| !_enabled.load(std::memory_order_acquire)
			|| !_mapLoaded.load(std::memory_order_acquire)
			|| !_shadowResourcesReady.load(std::memory_order_acquire)
			|| (debugVisualization != DebugVisualization::kHeightmap
				&& !_shadowPopulated.load(std::memory_order_acquire))) {
			return {};
		}

		const auto block = ts::BuildFeatureBlock(_loadedMetadata, true);
		cs::TerrainShadowsFeatureData data{};
		data.TerrainShadowMode =
			static_cast<std::uint32_t>(debugVisualization) + 1;
		data.Scale[0] = block.scale[0];
		data.Scale[1] = block.scale[1];
		data.Scale[2] = block.scale[2];
		data.ZRange[0] = block.zRange[0];
		data.ZRange[1] = block.zRange[1];
		data.Offset[0] = block.offset[0];
		data.Offset[1] = block.offset[1];
		data.HeightRange[0] = _loadedMetadata.pos0[2];
		data.HeightRange[1] = _loadedMetadata.pos1[2];
		return data;
	}

	void TerrainShadows::PublishStatus(
		const std::string& a_worldspace,
		std::string_view a_detail)
	{
		_worldspaceResolved.store(!a_worldspace.empty(), std::memory_order_release);
		const std::lock_guard<std::mutex> guard(_statusMutex);
		if (_statusWorldspace != a_worldspace)
			_statusWorldspace = a_worldspace;
		if (_statusDetail != a_detail)
			_statusDetail.assign(a_detail);
	}

	void TerrainShadows::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		std::string worldspace;
		std::string detail;
		{
			const std::lock_guard<std::mutex> guard(_statusMutex);
			worldspace = _statusWorldspace;
			detail = _statusDetail;
		}
		a_sink
			.Field("enabled", _enabled.load(std::memory_order_relaxed))
			.Field("operational", _injectionsOperational.load(std::memory_order_relaxed))
			.Field("worldspace", worldspace.empty() ? "none" : worldspace)
			.Field("status", detail.empty() ? "unknown" : detail)
			.Field("map_loaded", _mapLoaded.load(std::memory_order_relaxed))
			.Field("shadow_populated", _shadowPopulated.load(std::memory_order_relaxed))
			.Field(
				"discovered_maps",
				static_cast<std::int64_t>(
					_discoveredMaps.load(std::memory_order_relaxed)))
			.Dimensions(
				"source",
				_sourceWidth.load(std::memory_order_relaxed),
				_sourceHeight.load(std::memory_order_relaxed))
			.Dimensions(
				"effective",
				_effectiveWidth.load(std::memory_order_relaxed),
				_effectiveHeight.load(std::memory_order_relaxed))
			.Field(
				"downsample_factor",
				static_cast<std::int64_t>(
					_appliedFactorTelemetry.load(std::memory_order_relaxed)))
			.Field(
				"allocated_mib",
				ts::BytesToMiB(_allocatedBytes.load(std::memory_order_relaxed)))
			.Field(
				"prepass_runs",
				static_cast<std::int64_t>(
					_prepassRuns.load(std::memory_order_relaxed)))
			.Field(
				"dispatches",
				static_cast<std::int64_t>(_dispatches.load(std::memory_order_relaxed)))
			.Field(
				"updates",
				static_cast<std::int64_t>(_updates.load(std::memory_order_relaxed)))
			.Field(
				"full_refreshes",
				static_cast<std::int64_t>(
					_fullRefreshes.load(std::memory_order_relaxed)))
			.Field(
				"binds",
				static_cast<std::int64_t>(_binds.load(std::memory_order_relaxed)))
			.Field(
				"sampler_binds",
				static_cast<std::int64_t>(
					_samplerBinds.load(std::memory_order_relaxed)))
			.Field(
				"debug_visualization",
				DebugVisualizationName(
					_debugVisualization.load(std::memory_order_relaxed)))
			.Field(
				"debug_binds",
				static_cast<std::int64_t>(
					_debugBinds.load(std::memory_order_relaxed)));
	}

	void TerrainShadows::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &_settings.enabled);
		ImGui::TextDisabled("Off publishes zero terrain shadow, which is shader identity.");
		Menu::Get().DrawDebugViewSelector(*this);

		const auto factorLabel = [](std::uint32_t a_factor) {
			return a_factor == 1 ? "1 (full resolution)" :
				a_factor == 2	 ? "2 (quarter memory)" :
									 "4 (sixteenth memory)";
		};
		if (ImGui::BeginCombo(
				"Downsample factor", factorLabel(_settings.downsampleFactor))) {
			for (const auto factor : ts::kDownsampleFactors) {
				const bool selected = factor == _settings.downsampleFactor;
				if (ImGui::Selectable(factorLabel(factor), selected)
					&& !selected) {
					_settings.downsampleFactor = factor;
					changed = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled(
			"Higher factors trade shadow detail for VRAM; the map reloads in place.");

		if (changed) {
			PublishSettings();
			SaveSettings();
		}

		std::string worldspace;
		std::string detail;
		{
			const std::lock_guard<std::mutex> guard(_statusMutex);
			worldspace = _statusWorldspace;
			detail = _statusDetail;
		}
		ImGui::Separator();
		ImGui::TextDisabled(
			"Worldspace: %s | map: %s",
			worldspace.empty() ? "none" : worldspace.c_str(),
			detail.empty() ? "unknown" : detail.c_str());
		const auto sourceWidth = _sourceWidth.load(std::memory_order_relaxed);
		const auto sourceHeight = _sourceHeight.load(std::memory_order_relaxed);
		const auto effectiveWidth = _effectiveWidth.load(std::memory_order_relaxed);
		const auto effectiveHeight = _effectiveHeight.load(std::memory_order_relaxed);
		if (effectiveWidth != 0 && effectiveHeight != 0) {
			ImGui::TextDisabled(
				"Source %ux%u -> effective %ux%u | %.2f MiB",
				sourceWidth,
				sourceHeight,
				effectiveWidth,
				effectiveHeight,
				ts::BytesToMiB(_allocatedBytes.load(std::memory_order_relaxed)));
		} else {
			ImGui::TextDisabled(
				"No heightmap resident (%zu discovered).",
				_discoveredMaps.load(std::memory_order_relaxed));
		}
		if (!_injectionsOperational.load(std::memory_order_relaxed)) {
			ImGui::TextDisabled(
				"Inactive: %s",
				_validationDetail.empty() ?
					"shader delivery path unavailable" :
					_validationDetail.c_str());
		}
	}

	void TerrainShadows::RestoreDefaultSettings()
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
				cs::FeatureManager::Get().Register(TerrainShadows::GetSingleton());
			}
		};
		static AutoRegister _autoRegister;
	}
}
