#include "Skylighting.h"

#include <d3d11.h>
#include <imgui.h>

#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include "Log.h"
#include "Menu/Menu.h"
#include "RE/N/NiNode.h"
#include "Render/Annotation.h"
#include "Render/ComputeScope.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/SharedData.h"
#include "Settings/FeatureConfig.h"
#include "Telemetry/Telemetry.h"
#include "Utils/CSUtil.h"
#include "Utils/UI.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting");

		constexpr float kEngineDefaultExtent = 4096.0f;
		constexpr float kMinOcclusionExtent = 2048.0f;
		constexpr float kMaxOcclusionExtent = 16384.0f;
		constexpr float kMinConsumerSetting = 0.0f;
		constexpr float kMaxConsumerSetting = 1.0f;
		constexpr std::uint32_t kConsumerEnabledFlag = 1U << 0;
		constexpr std::uint32_t kVisibilityDebugFlag = 1U << 1;
		constexpr float kUnavailableDiagnosticFloat =
			std::numeric_limits<float>::lowest();
		constexpr std::uint32_t kOcclusionSize = 512;
		constexpr std::uint32_t kDebugThreadGroupSize = 8;
		constexpr std::uint32_t kOcclusionRenderMode = 14;
		constexpr const wchar_t* kNormalizedDebugShaderPath =
			L"Data\\Shaders\\Skylighting\\OcclusionDepthDebug.cs.hlsl";

		inline constexpr REL::ID kPrecipOcclusionMatrix{
			817636, 2713095, 2713095
		};
		// NG and AE genuinely use different IDs; the AE ID covers 1.11.221 and 1.11.240.
		inline constexpr REL::ID kPrecipOcclusionExtent{
			153127, 2692285, 4799577
		};
		// These are three floats. NG and AE use different IDs; AE covers 1.11.221 and 1.11.240.
		inline constexpr REL::ID kPrecipOcclusionDirX{
			348873, 2692288, 4799580
		};
		inline constexpr REL::ID kPrecipOcclusionDirY{
			1250021, 2692289, 4799581
		};
		inline constexpr REL::ID kPrecipOcclusionDirZ{
			568563, 2692290, 4799582
		};
		inline constexpr REL::ID kShadowSceneNode{
			1327069, 2712479, 2712479
		};
		inline constexpr REL::ID kPreCulledQEnabled{
			917969, 2317322, 2317322
		};
		inline constexpr REL::ID kPreCulledWantEnabled{
			493183, 2712638, 2712638
		};
		// NG and AE genuinely use different IDs for this Display setting.
		inline constexpr REL::ID kPreCulledDisplaySetting{
			1252712, 2677864, 4784532
		};
		inline constexpr REL::ID kPreCulledTempDisabled{
			718924, 2712639, 2712639
		};

		RE::BSGraphics::DepthStencilTarget*
			ResolvePrecipitationOcclusionTarget(
				RE::BSGraphics::RendererData* a_rendererData,
				std::int32_t& a_platformID) noexcept
		{
			a_platformID = -1;
			if (!a_rendererData) {
				return nullptr;
			}

			try {
				auto* renderTargetManager =
					cs::engine::GetRenderTargetManager();
				if (!renderTargetManager) {
					return nullptr;
				}

				const auto platformID =
					renderTargetManager->GetDepthStencilTargetPlatformID(
						static_cast<std::size_t>(
							cs::engine::DepthStencilTarget::
								kPrecipitationOcclusion));
				if (platformID >=
					std::size(a_rendererData->depthStencilTargets)) {
					return nullptr;
				}

				a_platformID = static_cast<std::int32_t>(platformID);
				return std::addressof(
					a_rendererData->depthStencilTargets[platformID]);
			} catch (...) {
				return nullptr;
			}
		}

		DXGI_FORMAT GetTypelessDepthFormat(DXGI_FORMAT a_format) noexcept
		{
			switch (a_format) {
			case DXGI_FORMAT_D16_UNORM:
			case DXGI_FORMAT_R16_TYPELESS:
				return DXGI_FORMAT_R16_TYPELESS;
			case DXGI_FORMAT_D24_UNORM_S8_UINT:
			case DXGI_FORMAT_R24G8_TYPELESS:
				return DXGI_FORMAT_R24G8_TYPELESS;
			case DXGI_FORMAT_D32_FLOAT:
			case DXGI_FORMAT_R32_TYPELESS:
				return DXGI_FORMAT_R32_TYPELESS;
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			case DXGI_FORMAT_R32G8X24_TYPELESS:
				return DXGI_FORMAT_R32G8X24_TYPELESS;
			default:
				return DXGI_FORMAT_UNKNOWN;
			}
		}

		DXGI_FORMAT GetShaderReadableDepthFormat(
			DXGI_FORMAT a_format) noexcept
		{
			switch (a_format) {
			case DXGI_FORMAT_D16_UNORM:
			case DXGI_FORMAT_R16_TYPELESS:
				return DXGI_FORMAT_R16_UNORM;
			case DXGI_FORMAT_D24_UNORM_S8_UINT:
			case DXGI_FORMAT_R24G8_TYPELESS:
				return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			case DXGI_FORMAT_D32_FLOAT:
			case DXGI_FORMAT_R32_TYPELESS:
				return DXGI_FORMAT_R32_FLOAT;
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			case DXGI_FORMAT_R32G8X24_TYPELESS:
				return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
			default:
				return DXGI_FORMAT_UNKNOWN;
			}
		}

		bool BuildDepthSRVDesc(
			const D3D11_TEXTURE2D_DESC& a_textureDesc,
			D3D11_SHADER_RESOURCE_VIEW_DESC& a_srvDesc) noexcept
		{
			a_srvDesc = {};
			a_srvDesc.Format =
				GetShaderReadableDepthFormat(a_textureDesc.Format);
			if (a_srvDesc.Format == DXGI_FORMAT_UNKNOWN) {
				return false;
			}

			if (a_textureDesc.SampleDesc.Count > 1) {
				if (a_textureDesc.ArraySize > 1) {
					a_srvDesc.ViewDimension =
						D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
					a_srvDesc.Texture2DMSArray.FirstArraySlice = 0;
					a_srvDesc.Texture2DMSArray.ArraySize =
						a_textureDesc.ArraySize;
				} else {
					a_srvDesc.ViewDimension =
						D3D11_SRV_DIMENSION_TEXTURE2DMS;
				}
			} else if (a_textureDesc.ArraySize > 1) {
				a_srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				a_srvDesc.Texture2DArray.MostDetailedMip = 0;
				a_srvDesc.Texture2DArray.MipLevels = a_textureDesc.MipLevels;
				a_srvDesc.Texture2DArray.FirstArraySlice = 0;
				a_srvDesc.Texture2DArray.ArraySize = a_textureDesc.ArraySize;
			} else {
				a_srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				a_srvDesc.Texture2D.MostDetailedMip = 0;
				a_srvDesc.Texture2D.MipLevels = a_textureDesc.MipLevels;
			}
			return true;
		}

		bool ParseSettingsTable(
			const toml::table& a_config,
			Skylighting::Settings& a_candidate,
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
			const auto accept = [&](feature_config::ScalarReadStatus a_status,
									std::string_view a_key,
									std::string_view a_type) {
				switch (a_status) {
				case feature_config::ScalarReadStatus::kMissing:
				case feature_config::ScalarReadStatus::kValid:
					return true;
				case feature_config::ScalarReadStatus::kWrongType:
					a_error = std::format(
						"settings.{}: expected {}", a_key, a_type);
					break;
				case feature_config::ScalarReadStatus::kInvalidValue:
					a_error = std::format(
						"settings.{}: invalid value", a_key);
					break;
				case feature_config::ScalarReadStatus::kOutOfRange:
					a_error = std::format(
						"settings.{}: value is out of range", a_key);
					break;
				}
				return false;
			};

			return accept(
					   feature_config::ReadBool(
						   *settingsTable, "enabled", a_candidate.enabled),
					   "enabled",
					   "boolean") &&
				accept(
					   feature_config::ReadBool(
						   *settingsTable,
						   "force_scene_traversal",
						   a_candidate.forceSceneTraversal),
					   "force_scene_traversal",
					   "boolean") &&
				accept(
					   feature_config::ReadFloat(
						   *settingsTable,
						   "occlusion_extent",
						   a_candidate.occlusionExtent,
						   kMinOcclusionExtent,
						   kMaxOcclusionExtent),
					   "occlusion_extent",
					   "number") &&
				accept(
					   feature_config::ReadFloat(
						   *settingsTable,
						   "strength",
						   a_candidate.strength,
						   kMinConsumerSetting,
						   kMaxConsumerSetting),
					   "strength",
					   "number") &&
				accept(
					   feature_config::ReadFloat(
						   *settingsTable,
						   "min_diffuse_visibility",
						   a_candidate.minDiffuseVisibility,
						   kMinConsumerSetting,
						   kMaxConsumerSetting),
					   "min_diffuse_visibility",
					   "number") &&
				accept(
					   feature_config::ReadFloat(
						   *settingsTable,
						   "min_specular_visibility",
						   a_candidate.minSpecularVisibility,
						   kMinConsumerSetting,
						   kMaxConsumerSetting),
					   "min_specular_visibility",
					   "number");
		}

		struct AutoRegister
		{
			AutoRegister()
			{
				FeatureManager::Get().Register(Skylighting::GetSingleton());
			}
		} autoRegister;

		class OcclusionRenderScope
		{
		public:
			explicit OcclusionRenderScope(std::atomic_bool& a_flag) :
				_flag(a_flag)
			{
				_flag.store(true, std::memory_order_release);
			}

			~OcclusionRenderScope()
			{
				_flag.store(false, std::memory_order_release);
			}

			OcclusionRenderScope(const OcclusionRenderScope&) = delete;
			OcclusionRenderScope& operator=(const OcclusionRenderScope&) = delete;

		private:
			std::atomic_bool& _flag;
		};

		class DepthTargetOverride
		{
		public:
			DepthTargetOverride(
				RE::BSGraphics::DepthStencilTarget& a_target,
				ID3D11Texture2D* a_texture,
				ID3D11ShaderResourceView* a_srv,
				ID3D11DepthStencilView* a_dsv) :
				_target(a_target),
				_texture(a_target.texture),
				_srv(a_target.srViewDepth),
				_dsv(a_target.dsView[0])
			{
				_target.texture =
					reinterpret_cast<REX::W32::ID3D11Texture2D*>(a_texture);
				_target.srViewDepth =
					reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(a_srv);
				_target.dsView[0] =
					reinterpret_cast<REX::W32::ID3D11DepthStencilView*>(a_dsv);
			}

			~DepthTargetOverride()
			{
				_target.texture = _texture;
				_target.srViewDepth = _srv;
				_target.dsView[0] = _dsv;
			}

			DepthTargetOverride(const DepthTargetOverride&) = delete;
			DepthTargetOverride& operator=(const DepthTargetOverride&) = delete;

		private:
			RE::BSGraphics::DepthStencilTarget& _target;
			REX::W32::ID3D11Texture2D* _texture;
			REX::W32::ID3D11ShaderResourceView* _srv;
			REX::W32::ID3D11DepthStencilView* _dsv;
		};

		class FloatOverride
		{
		public:
			FloatOverride(float* a_target, float a_value) :
				_target(a_target)
			{
				if (_target) {
					_original = *_target;
					*_target = a_value;
				}
			}

			~FloatOverride()
			{
				if (_target) {
					*_target = _original;
				}
			}

			FloatOverride(const FloatOverride&) = delete;
			FloatOverride& operator=(const FloatOverride&) = delete;

		private:
			float* _target = nullptr;
			float _original = 0.0f;
		};

		class ByteOverride
		{
		public:
			ByteOverride(
				std::uint8_t* a_target,
				std::uint8_t a_value,
				bool a_enabled) :
				_target(a_enabled ? a_target : nullptr)
			{
				if (_target) {
					_original = *_target;
					*_target = a_value;
				}
			}

			~ByteOverride()
			{
				if (_target) {
					*_target = _original;
				}
			}

			ByteOverride(const ByteOverride&) = delete;
			ByteOverride& operator=(const ByteOverride&) = delete;

		private:
			std::uint8_t* _target = nullptr;
			std::uint8_t _original = 0;
		};

		class DirectionOverride
		{
		public:
			DirectionOverride(float* a_x, float* a_y, float* a_z) :
				_x(a_x),
				_y(a_y),
				_z(a_z)
			{
				if (_x && _y && _z) {
					_originalX = *_x;
					_originalY = *_y;
					_originalZ = *_z;

					*_x = 0.0f;
					*_y = 0.0f;
					// If the map is inverted or tilted in game, suspect this sign first.
					*_z = -1.0f;
				}
			}

			~DirectionOverride()
			{
				if (_x && _y && _z) {
					*_x = _originalX;
					*_y = _originalY;
					*_z = _originalZ;
				}
			}

			DirectionOverride(const DirectionOverride&) = delete;
			DirectionOverride& operator=(const DirectionOverride&) = delete;

		private:
			float* _x = nullptr;
			float* _y = nullptr;
			float* _z = nullptr;
			float _originalX = 0.0f;
			float _originalY = 0.0f;
			float _originalZ = 0.0f;
		};

		bool IsFiniteMatrix(const DirectX::XMFLOAT4X4& a_matrix)
		{
			bool nonZero = false;
			const auto* values = &a_matrix.m[0][0];
			for (std::size_t index = 0; index < 16; ++index) {
				if (!std::isfinite(values[index])) {
					return false;
				}
				nonZero = nonZero || values[index] != 0.0f;
			}
			return nonZero;
		}

		bool IsFiniteDirection(const DirectX::XMFLOAT4& a_direction)
		{
			if (!std::isfinite(a_direction.x) ||
				!std::isfinite(a_direction.y) ||
				!std::isfinite(a_direction.z)) {
				return false;
			}
			const float lengthSquared =
				a_direction.x * a_direction.x +
				a_direction.y * a_direction.y +
				a_direction.z * a_direction.z;
			return lengthSquared > 0.5f && lengthSquared < 1.5f;
		}

		std::uint64_t CountPasses(RE::BSRenderPass* a_pass)
		{
			std::uint64_t count = 0;
			while (a_pass && count < 64) {
				++count;
				a_pass = a_pass->propertyNext;
			}
			return count;
		}

	}

	struct Skylighting::BSLightingShaderProperty_GetOcclusionPasses
	{
		static RE::BSShaderProperty::RenderPassArray* thunk(
			RE::BSLightingShaderProperty* a_property,
			RE::BSGeometry* a_geometry,
			std::uint32_t a_renderMode,
			RE::BSShaderAccumulator* a_accumulator)
		{
			auto* feature = Skylighting::GetSingleton();
			feature->_vfuncCallsTotal.fetch_add(1, std::memory_order_relaxed);
			if (!feature->_inOcclusionRender.load(std::memory_order_acquire)) {
				return func(
					a_property, a_geometry, a_renderMode, a_accumulator);
			}

			feature->_workingVfuncCallsDuringRender.fetch_add(
				1, std::memory_order_relaxed);
			feature->_workingVfuncLastRenderModeDuringRender.store(
				static_cast<std::int64_t>(a_renderMode),
				std::memory_order_relaxed);
			if (a_renderMode != kOcclusionRenderMode) {
				return func(
					a_property, a_geometry, a_renderMode, a_accumulator);
			}

			feature->_workingGeometryCount.fetch_add(
				1, std::memory_order_relaxed);
			if (!a_property || !a_geometry || !a_accumulator) {
				return nullptr;
			}

			using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;
			const bool valid =
				a_property->flags.any(ShaderFlag::kZBufferWrite) &&
				a_property->flags.none(
					ShaderFlag::kSkinned,
					ShaderFlag::kRefraction,
					ShaderFlag::kTempRefraction,
					ShaderFlag::kLODLandscape,
					ShaderFlag::kEyeReflect,
					ShaderFlag::kDecal,
					ShaderFlag::kDynamicDecal);
			if (!valid || a_geometry->worldBound.fRadius <= 32.0f) {
				return nullptr;
			}

			auto* passes =
				func(a_property, a_geometry, a_renderMode, a_accumulator);
			if (passes) {
				feature->_workingPassCount.fetch_add(
					CountPasses(passes->passList),
					std::memory_order_relaxed);
			}
			return passes;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Skylighting::Precipitation_RenderOcclusionMap
	{
		static void thunk()
		{
			func();
			auto* feature = Skylighting::GetSingleton();
			feature->_producerRanThisFrame.store(
				false, std::memory_order_release);
			feature->PublishConsumerData();
			feature->_normalizedViewDispatchedLastFrame.store(
				false, std::memory_order_release);
			if (feature->_enabled.load(std::memory_order_acquire) &&
				feature->EnsureResources()) {
				feature->RenderProducer();
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	Skylighting* Skylighting::GetSingleton()
	{
		static Skylighting instance;
		return &instance;
	}

	std::span<const FeatureDebugView> Skylighting::GetDebugViews() const noexcept
	{
		static constexpr std::array views{
			FeatureDebugView{
				.id = "occlusion_depth",
				.label = "Occlusion depth (raw, configurable footprint)",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const Skylighting&>(a_feature)
						.GetOcclusionDebugTexture();
				}
			},
			FeatureDebugView{
				.id = "occlusion_depth_normalized",
				.label =
					"Occlusion depth (frame-normalized readability aid)",
				.kind = FeatureDebugViewKind::kTexturePreview,
				.textureProvider = [](const Feature& a_feature) {
					return static_cast<const Skylighting&>(a_feature)
						.GetNormalizedOcclusionDebugTexture();
				}
			},
			FeatureDebugView{
				.id = "skylighting_visibility",
				.label =
					"Skylighting visibility (world projection, raw greyscale)",
				.kind = FeatureDebugViewKind::kFullscreen
			}
		};
		return views;
	}

	void Skylighting::SetDebugView(std::string_view a_view) noexcept
	{
		_debugPreviewEnabled.store(
			a_view == "occlusion_depth", std::memory_order_release);
		_normalizedDebugPreviewEnabled.store(
			a_view == "occlusion_depth_normalized",
			std::memory_order_release);
		_visibilityDebugEnabled.store(
			a_view == "skylighting_visibility",
			std::memory_order_release);
		if (a_view != "occlusion_depth_normalized") {
			_normalizedViewDispatchedLastFrame.store(
				false, std::memory_order_release);
		}
		PublishConsumerData();
	}

	FeatureDebugTexture Skylighting::GetOcclusionDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText = "Occlusion depth is unavailable."
		};
		if (!_debugPreviewEnabled.load(std::memory_order_acquire) ||
			!_enabled.load(std::memory_order_acquire) ||
			!_resourcesReady.load(std::memory_order_acquire) ||
			!_producerRanThisFrame.load(std::memory_order_acquire) ||
			!_occlusionSRV) {
			return texture;
		}

		texture.texture = _occlusionSRV.get();
		texture.width = kOcclusionSize;
		texture.height = kOcclusionSize;
		const auto data = GetOcclusionData();
		texture.caption = std::format(
			"Raw D24 hardware depth, no linearization (white = far/clear); "
			"{:.0f}-unit full-width footprint ({:.2f} units/texel)",
			data.extent,
			data.extent / static_cast<float>(kOcclusionSize));
		return texture;
	}

	FeatureDebugTexture Skylighting::GetNormalizedOcclusionDebugTexture() const
	{
		FeatureDebugTexture texture{
			.unavailableText =
				"Frame-normalized occlusion depth is unavailable."
		};
		if (!_normalizedDebugPreviewEnabled.load(std::memory_order_acquire) ||
			!_enabled.load(std::memory_order_acquire) ||
			!_producerRanThisFrame.load(std::memory_order_acquire)) {
			return texture;
		}

		auto* feature = const_cast<Skylighting*>(this);
		if (!feature->EnsureResources() ||
			!feature->DispatchNormalizedDebugView() ||
			!_normalizedOcclusionSRV) {
			return texture;
		}

		texture.texture = _normalizedOcclusionSRV.get();
		texture.width = kOcclusionSize;
		texture.height = kOcclusionSize;
		const auto data = GetOcclusionData();
		texture.caption = std::format(
			"Readability aid: depth normalized against this frame's own "
			"min/max (not absolute depth); orthographic top-down view; "
			"{:.0f}-unit full-width footprint ({:.2f} units/texel)",
			data.extent,
			data.extent / static_cast<float>(kOcclusionSize));
		return texture;
	}

	bool Skylighting::Configure(
		const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!ParseSettingsTable(a_config, candidate, a_error)) {
			return false;
		}
		_settings = candidate;
		return true;
	}

	void Skylighting::PublishSettings() noexcept
	{
		_enabled.store(_settings.enabled, std::memory_order_release);
		_forceSceneTraversal.store(
			_settings.forceSceneTraversal, std::memory_order_release);
		_occlusionExtent.store(
			_settings.occlusionExtent, std::memory_order_release);
		_strength.store(_settings.strength, std::memory_order_release);
		_minDiffuseVisibility.store(
			_settings.minDiffuseVisibility, std::memory_order_release);
		_minSpecularVisibility.store(
			_settings.minSpecularVisibility, std::memory_order_release);
		_effectiveExtent.store(
			_extentGlobal ? _settings.occlusionExtent : kEngineDefaultExtent,
			std::memory_order_release);
		PublishConsumerData();
	}

	void Skylighting::SaveSettings()
	{
		toml::table settings;
		settings.insert_or_assign("enabled", _settings.enabled);
		settings.insert_or_assign(
			"force_scene_traversal", _settings.forceSceneTraversal);
		settings.insert_or_assign(
			"occlusion_extent", _settings.occlusionExtent);
		settings.insert_or_assign("strength", _settings.strength);
		settings.insert_or_assign(
			"min_diffuse_visibility", _settings.minDiffuseVisibility);
		settings.insert_or_assign(
			"min_specular_visibility", _settings.minSpecularVisibility);
		if (const auto result =
				feature_config::UpdateFeatureSettings(GetConfigKey(), settings);
			!result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	void Skylighting::Load()
	{
		ResolveOcclusionGlobals();
		PublishSettings();

		const bool registered = cs::engine::RegisterReplacement({
			.targetId = cs::engine::ShaderInjectionTarget::kBsdfLight,
			.stages = cs::engine::ShaderStageBit(
				cs::engine::ShaderStage::kPixel),
			.contributor = "Skylighting",
			.defines = {
				{
					cs::engine::shader_injection_defines::kSkylighting,
					"1"
				}
			},
			.isReady = [this] {
				return _registrationsReady.load(std::memory_order_acquire)
					&& _consumerSamplerReady.load(std::memory_order_acquire)
					&& cs::render::IsSharedDataReady();
			}
		});
		if (!registered) {
			FailLoad(
				"Skylighting requires the reconstructed BSDFLight pixel "
				"shader; registering that replacement failed");
			return;
		}

		stl::write_vfunc<0x2C, BSLightingShaderProperty_GetOcclusionPasses>(
			RE::VTABLE::BSLightingShaderProperty[0]);
		_vfuncHookInstalled.store(true, std::memory_order_release);

		stl::detour_thunk<Precipitation_RenderOcclusionMap>(
			REL::ID({ 1114882, 2208812, 2208812 }));
		_outerHookInstalled.store(true, std::memory_order_release);
		_registrationsReady.store(true, std::memory_order_release);

		L->info(
			"Installed precipitation producer and lighting-property occlusion "
			"hooks; registered the BSDFLight skylighting consumer.");
	}

	void Skylighting::ResolveOcclusionGlobals() noexcept
	{
		const auto resolve = [](REL::ID a_id, std::string_view a_name) noexcept {
			try {
				const auto address = a_id.address();
				if (address == 0) {
					L->error(
						"Failed to resolve {} Address Library ID {}.",
						a_name,
						a_id.id());
				} else {
					L->info(
						"Resolved {} Address Library ID {} at 0x{:X}.",
						a_name,
						a_id.id(),
						address);
				}
				return address;
			} catch (const std::exception& e) {
				L->error(
					"Failed to resolve {} Address Library ID {}: {}",
					a_name,
					a_id.id(),
					e.what());
			} catch (...) {
				L->error(
					"Failed to resolve {} Address Library ID {}.",
					a_name,
					a_id.id());
			}
			return std::uintptr_t{ 0 };
		};

		_matrixGlobal = reinterpret_cast<DirectX::XMFLOAT4*>(
			resolve(kPrecipOcclusionMatrix, "precipitation occlusion matrix"));
		_extentGlobal = reinterpret_cast<float*>(
			resolve(kPrecipOcclusionExtent, "precipitation occlusion extent"));
		_directionXGlobal = reinterpret_cast<float*>(
			resolve(kPrecipOcclusionDirX, "precipitation occlusion direction X"));
		_directionYGlobal = reinterpret_cast<float*>(
			resolve(kPrecipOcclusionDirY, "precipitation occlusion direction Y"));
		_directionZGlobal = reinterpret_cast<float*>(
			resolve(kPrecipOcclusionDirZ, "precipitation occlusion direction Z"));
		_preCulledTempDisabledGlobal = reinterpret_cast<std::uint8_t*>(
			resolve(
				kPreCulledTempDisabled,
				"pre-culled objects temporary-disable byte"));

		const bool matrixResolved = _matrixGlobal != nullptr;
		const bool extentResolved = _extentGlobal != nullptr;
		const bool directionResolved =
			_directionXGlobal && _directionYGlobal && _directionZGlobal;
		_matrixGlobalResolved.store(matrixResolved, std::memory_order_release);
		_extentGlobalResolved.store(extentResolved, std::memory_order_release);
		_directionGlobalsResolved.store(
			directionResolved, std::memory_order_release);
		_sceneTraversalOverrideResolved.store(
			_preCulledTempDisabledGlobal != nullptr,
			std::memory_order_release);

		if (!matrixResolved || !extentResolved || !directionResolved) {
			_failureState.store(
				FailureState::kGlobalsUnavailable,
				std::memory_order_release);
			L->error(
				"One or more precipitation occlusion globals are unavailable; "
				"the producer will retain engine defaults for unavailable overrides.");
		}
	}

	void Skylighting::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		_device.store(a_device, std::memory_order_release);
		if (!a_device) {
			_resourceFailureReason.store(
				ResourceFailureReason::kDeviceUnavailable,
				std::memory_order_relaxed);
			_resourceState.store(
				ResourceState::kFailed, std::memory_order_release);
			_failureState.store(
				FailureState::kResourcesUnavailable,
				std::memory_order_release);
			L->error("Resource initialization failed: D3D11 device unavailable.");
			return;
		}

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		const auto samplerResult = a_device->CreateSamplerState(
			&samplerDesc, _comparisonSampler.put());
		if (FAILED(samplerResult)) {
			L->error(
				"Skylighting comparison sampler creation failed with HRESULT "
				"0x{:08X}; the consumer will fail closed.",
				static_cast<std::uint32_t>(samplerResult));
			return;
		}
		cs::render::annotation::SetName(
			_comparisonSampler.get(), "Skylighting/OcclusionComparison.Sampler");
		_consumerSamplerReady.store(true, std::memory_order_release);
		PublishConsumerData();
	}

	void Skylighting::SetValidationDetail(std::string a_detail) const
	{
		const std::lock_guard lock(_validationMutex);
		_validationDetail = std::move(a_detail);
	}

	std::string Skylighting::GetValidationDetail() const
	{
		const std::lock_guard lock(_validationMutex);
		return _validationDetail;
	}

	bool Skylighting::ValidateShaderInjections(std::string& a_error)
	{
		_injectionsOperational.store(false, std::memory_order_release);
		if (!_registrationsReady.load(std::memory_order_acquire)) {
			a_error = "the BSDFLight skylighting contribution did not register";
			SetValidationDetail(a_error);
			PublishConsumerData();
			return false;
		}
		if (!cs::render::IsSharedDataReady()) {
			a_error =
				"the shared substrate is unavailable, so b7 cannot carry "
				"skylighting controls";
			SetValidationDetail(a_error);
			PublishConsumerData();
			return false;
		}
		if (!_consumerSamplerReady.load(std::memory_order_acquire)) {
			a_error = "the occlusion comparison sampler is unavailable";
			SetValidationDetail(a_error);
			PublishConsumerData();
			return false;
		}

		const auto snapshot = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto define = snapshot.defines.find(
			cs::engine::shader_injection_defines::kSkylighting);
		const bool contributed =
			define != snapshot.defines.end() && define->second == "1";
		if (!snapshot.requested
			|| !snapshot.compileComplete
			|| !snapshot.swappable
			|| snapshot.slotCollision
			|| !contributed) {
			a_error = "'" + snapshot.name
				+ "' cannot deliver skylighting (requested="
				+ std::to_string(snapshot.requested)
				+ " compile_complete="
				+ std::to_string(snapshot.compileComplete)
				+ " swappable=" + std::to_string(snapshot.swappable)
				+ " slot_collision="
				+ std::to_string(snapshot.slotCollision)
				+ " contributed=" + std::to_string(contributed) + ")";
			SetValidationDetail(a_error);
			PublishConsumerData();
			return false;
		}

		_injectionsOperational.store(true, std::memory_order_release);
		SetValidationDetail({});
		PublishConsumerData();
		L->info(
			"Skylighting BSDFLight route is ready (b{}, t{}, s{}).",
			cs::render::kSkylightingDataSlot,
			cs::render::kSkylightingTextureSlot,
			cs::render::kSkylightingSamplerSlot);
		return true;
	}

	void Skylighting::ObserveRouteDiagnostics() const noexcept
	{
		const auto outcome = cs::engine::GetShaderInjectionOutcomeSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const bool mismatch =
			outcome.matches != 0 && outcome.substitutions < outcome.matches;
		const bool previous = _routeSubstitutionMismatch.exchange(
			mismatch, std::memory_order_acq_rel);
		if (mismatch && !previous) {
			L->warn(
				"Skylighting route substitution mismatch: substitutions={}/{}; "
				"reporting only, rendering remains unchanged.",
				outcome.substitutions,
				outcome.matches);
		} else if (!mismatch && previous) {
			L->info(
				"Skylighting route substitutions now agree: "
				"substitutions={}/{}.",
				outcome.substitutions,
				outcome.matches);
		}
	}

	void Skylighting::PublishConsumerData() noexcept
	{
		const auto occlusion = GetOcclusionData();
		const bool operational =
			_injectionsOperational.load(std::memory_order_acquire);
		if (operational)
			ObserveRouteDiagnostics();
		const bool exterior =
			_inInteriorResolved.load(std::memory_order_acquire)
			&& !_inInterior.load(std::memory_order_acquire);
		const bool active =
			operational
			&& _enabled.load(std::memory_order_acquire)
			&& _resourcesReady.load(std::memory_order_acquire)
			&& _producerRanThisFrame.load(std::memory_order_acquire)
			&& _consumerSamplerReady.load(std::memory_order_acquire)
			&& occlusion.valid
			&& exterior;

		std::uint32_t mode = active ? kConsumerEnabledFlag : 0;
		if (active
			&& _visibilityDebugEnabled.load(std::memory_order_acquire)) {
			mode |= kVisibilityDebugFlag;
		}
		cs::render::SkylightingSharedData data{
			.OcclusionViewProj = occlusion.transform,
			.OcclusionDirection = occlusion.direction,
			.OcclusionExtent = occlusion.extent,
			.Strength = _strength.load(std::memory_order_acquire),
			.MinDiffuseVisibility =
				_minDiffuseVisibility.load(std::memory_order_acquire),
			.MinSpecularVisibility =
				_minSpecularVisibility.load(std::memory_order_acquire),
			.Mode = mode
		};
		cs::render::PublishSkylightingSharedData(
			data, _occlusionSRV.get(), _comparisonSampler.get());
	}

	bool Skylighting::EnsureResources() noexcept
	{
		const auto resourceState =
			_resourceState.load(std::memory_order_acquire);
		if (resourceState == ResourceState::kReady) {
			if (_normalizedDebugPreviewEnabled.load(
					std::memory_order_acquire)) {
				EnsureNormalizedDebugResources(
					_device.load(std::memory_order_acquire));
			}
			return true;
		}
		if (resourceState == ResourceState::kFailed) {
			return false;
		}

		auto* device = _device.load(std::memory_order_acquire);
		if (!device) {
			return false;
		}

		_resourceAttemptCount.fetch_add(1, std::memory_order_relaxed);
		try {
			auto* rendererData = RE::BSGraphics::GetRendererData();
			if (!rendererData) {
				_precipitationOcclusionPlatformID.store(
					-1, std::memory_order_release);
				_nativeTextureAvailable.store(false, std::memory_order_relaxed);
				_nativeSRVAvailable.store(false, std::memory_order_relaxed);
				_nativeDSVAvailable.store(false, std::memory_order_relaxed);
				_resourceWaitReason.store(
					ResourceWaitReason::kRendererData,
					std::memory_order_relaxed);
				_resourceState.store(
					ResourceState::kWaitingForNativeTarget,
					std::memory_order_release);
				return false;
			}
			const auto reportDepthStencilTargets = [&]() noexcept {
				if (_depthStencilTargetsReported.test_and_set(
						std::memory_order_acq_rel)) {
					return;
				}

				std::int32_t firstMatchingSlot = -1;
				for (std::uint32_t index = 0;
					index < static_cast<std::uint32_t>(
								cs::engine::DepthStencilTarget::kCount);
					++index) {
					const auto& target =
						rendererData->depthStencilTargets[index];
					auto* texture =
						reinterpret_cast<ID3D11Texture2D*>(target.texture);
					const bool srvAvailable = target.srViewDepth != nullptr;
					const bool dsvAvailable = target.dsView[0] != nullptr;
					if (!texture) {
						L->info(
							"Depth-stencil slot {}: texture=false "
							"srViewDepth={} dsView[0]={}.",
							index,
							srvAvailable,
							dsvAvailable);
						continue;
					}

					D3D11_TEXTURE2D_DESC desc{};
					texture->GetDesc(&desc);
					if (firstMatchingSlot < 0 &&
						desc.Width == kOcclusionSize &&
						desc.Height == kOcclusionSize &&
						GetTypelessDepthFormat(desc.Format) !=
							DXGI_FORMAT_UNKNOWN) {
						firstMatchingSlot =
							static_cast<std::int32_t>(index);
					}
					L->info(
						"Depth-stencil slot {}: texture=true "
						"srViewDepth={} dsView[0]={} width={} height={} "
						"format={} sample_count={} bind_flags=0x{:08X}.",
						index,
						srvAvailable,
						dsvAvailable,
						desc.Width,
						desc.Height,
						static_cast<std::uint32_t>(desc.Format),
						desc.SampleDesc.Count,
						desc.BindFlags);
				}
				_first512DepthStencilSlot.store(
					firstMatchingSlot, std::memory_order_release);
			};
			std::int32_t platformID = -1;
			auto* nativeTarget = ResolvePrecipitationOcclusionTarget(
				rendererData, platformID);
			_precipitationOcclusionPlatformID.store(
				platformID, std::memory_order_release);
			if (!nativeTarget) {
				_nativeTextureAvailable.store(false, std::memory_order_relaxed);
				_nativeSRVAvailable.store(false, std::memory_order_relaxed);
				_nativeDSVAvailable.store(false, std::memory_order_relaxed);
				_resourceWaitReason.store(
					ResourceWaitReason::kNativeTextureAndDepthStencilView,
					std::memory_order_relaxed);
				_resourceState.store(
					ResourceState::kWaitingForNativeTarget,
					std::memory_order_release);
				reportDepthStencilTargets();
				return false;
			}
			auto* nativeTexture =
				reinterpret_cast<ID3D11Texture2D*>(nativeTarget->texture);
			auto* nativeSRV =
				reinterpret_cast<ID3D11ShaderResourceView*>(
					nativeTarget->srViewDepth);
			auto* nativeDSV =
				reinterpret_cast<ID3D11DepthStencilView*>(
					nativeTarget->dsView[0]);

			_nativeTextureAvailable.store(
				nativeTexture != nullptr, std::memory_order_relaxed);
			_nativeSRVAvailable.store(
				nativeSRV != nullptr, std::memory_order_relaxed);
			_nativeDSVAvailable.store(
				nativeDSV != nullptr, std::memory_order_relaxed);

			if (!nativeTexture || !nativeDSV) {
				const auto waitReason =
					!nativeTexture && !nativeDSV ?
					ResourceWaitReason::kNativeTextureAndDepthStencilView :
					!nativeTexture ?
						ResourceWaitReason::kNativeTexture :
						ResourceWaitReason::kNativeDepthStencilView;
				_resourceWaitReason.store(
					waitReason, std::memory_order_relaxed);
				_resourceState.store(
					ResourceState::kWaitingForNativeTarget,
					std::memory_order_release);
				reportDepthStencilTargets();
				return false;
			}
			_resourceWaitReason.store(
				ResourceWaitReason::kNone, std::memory_order_relaxed);

			D3D11_TEXTURE2D_DESC textureDesc{};
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
			nativeTexture->GetDesc(&textureDesc);
			nativeDSV->GetDesc(&dsvDesc);
			if (textureDesc.Width != kOcclusionSize ||
				textureDesc.Height != kOcclusionSize) {
				_resourceFailureReason.store(
					ResourceFailureReason::kUnexpectedExtent,
					std::memory_order_relaxed);
				_resourceState.store(
					ResourceState::kFailed, std::memory_order_release);
				_failureState.store(
					FailureState::kResourcesUnavailable,
					std::memory_order_release);
				reportDepthStencilTargets();
				L->error(
					"Resource initialization failed: native precipitation "
					"texture is {}x{}, expected {}x{}.",
					textureDesc.Width,
					textureDesc.Height,
					kOcclusionSize,
					kOcclusionSize);
				return false;
			}

			if (nativeSRV) {
				nativeSRV->GetDesc(&srvDesc);
			} else {
				const auto typelessFormat =
					GetTypelessDepthFormat(textureDesc.Format);
				if (typelessFormat == DXGI_FORMAT_UNKNOWN ||
					!BuildDepthSRVDesc(textureDesc, srvDesc)) {
					_resourceFailureReason.store(
						ResourceFailureReason::
							kUnsupportedShaderResourceFormat,
						std::memory_order_relaxed);
					_resourceState.store(
						ResourceState::kFailed, std::memory_order_release);
					_failureState.store(
						FailureState::kResourcesUnavailable,
						std::memory_order_release);
					reportDepthStencilTargets();
					L->error(
						"Resource initialization failed: native "
						"srViewDepth is null and texture format {} cannot "
						"be mapped to a depth SRV.",
						static_cast<std::uint32_t>(textureDesc.Format));
					return false;
				}
				textureDesc.Format = typelessFormat;
				textureDesc.BindFlags |=
					D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
				_synthesizedSRVDescriptor.store(
					true, std::memory_order_relaxed);
				L->info(
					"Native precipitation srViewDepth is null; synthesized "
					"an SRV descriptor with format {}.",
					static_cast<std::uint32_t>(srvDesc.Format));
			}

			winrt::com_ptr<ID3D11Texture2D> occlusionTexture;
			winrt::com_ptr<ID3D11ShaderResourceView> occlusionSRV;
			winrt::com_ptr<ID3D11DepthStencilView> occlusionDSV;

			const auto textureResult = device->CreateTexture2D(
				&textureDesc, nullptr, occlusionTexture.put());
			if (FAILED(textureResult)) {
				_resourceFailureReason.store(
					ResourceFailureReason::kTextureCreationFailed,
					std::memory_order_relaxed);
				_resourceState.store(
					ResourceState::kFailed, std::memory_order_release);
				_failureState.store(
					FailureState::kResourcesUnavailable,
					std::memory_order_release);
				reportDepthStencilTargets();
				L->error(
					"Resource initialization failed: CreateTexture2D "
					"returned HRESULT 0x{:08X}.",
					static_cast<std::uint32_t>(textureResult));
				return false;
			}

			const auto srvResult = device->CreateShaderResourceView(
				occlusionTexture.get(), &srvDesc, occlusionSRV.put());
			if (FAILED(srvResult)) {
				_resourceFailureReason.store(
					ResourceFailureReason::
						kShaderResourceViewCreationFailed,
					std::memory_order_relaxed);
				_resourceState.store(
					ResourceState::kFailed, std::memory_order_release);
				_failureState.store(
					FailureState::kResourcesUnavailable,
					std::memory_order_release);
				reportDepthStencilTargets();
				L->error(
					"Resource initialization failed: "
					"CreateShaderResourceView returned HRESULT 0x{:08X}.",
					static_cast<std::uint32_t>(srvResult));
				return false;
			}

			const auto dsvResult = device->CreateDepthStencilView(
				occlusionTexture.get(), &dsvDesc, occlusionDSV.put());
			if (FAILED(dsvResult)) {
				_resourceFailureReason.store(
					ResourceFailureReason::kDepthStencilViewCreationFailed,
					std::memory_order_relaxed);
				_resourceState.store(
					ResourceState::kFailed, std::memory_order_release);
				_failureState.store(
					FailureState::kResourcesUnavailable,
					std::memory_order_release);
				reportDepthStencilTargets();
				L->error(
					"Resource initialization failed: "
					"CreateDepthStencilView returned HRESULT 0x{:08X}.",
					static_cast<std::uint32_t>(dsvResult));
				return false;
			}

			cs::render::annotation::SetName(
				occlusionTexture.get(), "Skylighting/OcclusionDepth.Texture");
			cs::render::annotation::SetName(
				occlusionSRV.get(), "Skylighting/OcclusionDepth.SRV");
			cs::render::annotation::SetName(
				occlusionDSV.get(), "Skylighting/OcclusionDepth.DSV");

			_occlusionTexture = std::move(occlusionTexture);
			_occlusionSRV = std::move(occlusionSRV);
			_occlusionDSV = std::move(occlusionDSV);
			_resourceWaitReason.store(
				ResourceWaitReason::kNone, std::memory_order_relaxed);
			_resourceFailureReason.store(
				ResourceFailureReason::kNone, std::memory_order_relaxed);
			_resourcesReady.store(true, std::memory_order_release);
			_resourceState.store(
				ResourceState::kReady, std::memory_order_release);
			if (_matrixGlobalResolved.load(std::memory_order_acquire) &&
				_extentGlobalResolved.load(std::memory_order_acquire) &&
				_directionGlobalsResolved.load(std::memory_order_acquire)) {
				_failureState.store(
					FailureState::kNone, std::memory_order_release);
			}
			L->info(
				"Occlusion depth resource ready ({}x{}).",
				textureDesc.Width,
				textureDesc.Height);
			if (_normalizedDebugPreviewEnabled.load(
					std::memory_order_acquire)) {
				EnsureNormalizedDebugResources(device);
			}
			return true;
		} catch (const std::exception& e) {
			_resourcesReady.store(false, std::memory_order_release);
			_resourceFailureReason.store(
				ResourceFailureReason::kUnexpectedException,
				std::memory_order_relaxed);
			_resourceState.store(
				ResourceState::kFailed, std::memory_order_release);
			_failureState.store(
				FailureState::kResourcesUnavailable,
				std::memory_order_release);
			L->error("Resource initialization failed: {}", e.what());
		} catch (...) {
			_resourcesReady.store(false, std::memory_order_release);
			_resourceFailureReason.store(
				ResourceFailureReason::kUnexpectedException,
				std::memory_order_relaxed);
			_resourceState.store(
				ResourceState::kFailed, std::memory_order_release);
			_failureState.store(
				FailureState::kResourcesUnavailable,
				std::memory_order_release);
			L->error("Resource initialization failed.");
		}
		return false;
	}

	bool Skylighting::EnsureNormalizedDebugResources(
		ID3D11Device* a_device) noexcept
	{
		if (_normalizedResourcesAllocated.load(std::memory_order_acquire)) {
			return true;
		}
		if (!a_device ||
			_normalizedResourcesAttempted.exchange(
				true, std::memory_order_acq_rel)) {
			return false;
		}

		try {
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = kOcclusionSize;
			textureDesc.Height = kOcclusionSize;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = DXGI_FORMAT_R8_UNORM;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags =
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			winrt::com_ptr<ID3D11Texture2D> normalizedTexture;
			winrt::com_ptr<ID3D11ShaderResourceView> normalizedSRV;
			winrt::com_ptr<ID3D11UnorderedAccessView> normalizedUAV;
			DX::ThrowIfFailed(a_device->CreateTexture2D(
				&textureDesc, nullptr, normalizedTexture.put()));
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(
				normalizedTexture.get(), nullptr, normalizedSRV.put()));
			DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(
				normalizedTexture.get(), nullptr, normalizedUAV.put()));

			D3D11_BUFFER_DESC rangeDesc{};
			rangeDesc.ByteWidth = sizeof(std::uint32_t) * 2;
			rangeDesc.Usage = D3D11_USAGE_DEFAULT;
			rangeDesc.BindFlags =
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			rangeDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			rangeDesc.StructureByteStride = sizeof(std::uint32_t) * 2;

			winrt::com_ptr<ID3D11Buffer> rangeBuffer;
			winrt::com_ptr<ID3D11ShaderResourceView> rangeSRV;
			winrt::com_ptr<ID3D11UnorderedAccessView> rangeUAV;
			DX::ThrowIfFailed(a_device->CreateBuffer(
				&rangeDesc, nullptr, rangeBuffer.put()));

			D3D11_SHADER_RESOURCE_VIEW_DESC rangeSRVDesc{};
			rangeSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
			rangeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			rangeSRVDesc.Buffer.NumElements = 1;
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(
				rangeBuffer.get(), &rangeSRVDesc, rangeSRV.put()));

			D3D11_UNORDERED_ACCESS_VIEW_DESC rangeUAVDesc{};
			rangeUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
			rangeUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			rangeUAVDesc.Buffer.NumElements = 1;
			DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(
				rangeBuffer.get(), &rangeUAVDesc, rangeUAV.put()));

			winrt::com_ptr<ID3D11ComputeShader> reduceCS;
			winrt::com_ptr<ID3D11ComputeShader> writeCS;
			reduceCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(
					kNormalizedDebugShaderPath,
					{ { "REDUCE_DEPTH_RANGE", "1" } },
					"cs_5_0")));
			writeCS.attach(reinterpret_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(
					kNormalizedDebugShaderPath,
					{ { "NORMALIZE_DEPTH", "1" } },
					"cs_5_0")));
			if (!reduceCS || !writeCS) {
				throw std::runtime_error(
					"normalized debug compute shader compilation failed");
			}

			cs::render::annotation::SetName(
				normalizedTexture.get(),
				"Skylighting/OcclusionDepthNormalized.Texture");
			cs::render::annotation::SetName(
				normalizedSRV.get(),
				"Skylighting/OcclusionDepthNormalized.SRV");
			cs::render::annotation::SetName(
				normalizedUAV.get(),
				"Skylighting/OcclusionDepthNormalized.UAV");
			cs::render::annotation::SetName(
				rangeBuffer.get(),
				"Skylighting/OcclusionDepthNormalizedRange.Buffer");
			cs::render::annotation::SetName(
				rangeSRV.get(),
				"Skylighting/OcclusionDepthNormalizedRange.SRV");
			cs::render::annotation::SetName(
				rangeUAV.get(),
				"Skylighting/OcclusionDepthNormalizedRange.UAV");
			cs::render::annotation::SetName(
				reduceCS.get(),
				"Skylighting/OcclusionDepthNormalizedReduce.CS");
			cs::render::annotation::SetName(
				writeCS.get(),
				"Skylighting/OcclusionDepthNormalizedWrite.CS");

			_normalizedOcclusionTexture = std::move(normalizedTexture);
			_normalizedOcclusionSRV = std::move(normalizedSRV);
			_normalizedOcclusionUAV = std::move(normalizedUAV);
			_normalizedRangeBuffer = std::move(rangeBuffer);
			_normalizedRangeSRV = std::move(rangeSRV);
			_normalizedRangeUAV = std::move(rangeUAV);
			_normalizedReduceCS = std::move(reduceCS);
			_normalizedWriteCS = std::move(writeCS);
			_normalizedResourcesAllocated.store(
				true, std::memory_order_release);
			L->info(
				"Frame-normalized occlusion debug resources ready ({}x{}).",
				kOcclusionSize,
				kOcclusionSize);
			return true;
		} catch (const std::exception& e) {
			L->error(
				"Frame-normalized occlusion debug resources failed: {}",
				e.what());
		} catch (...) {
			L->error(
				"Frame-normalized occlusion debug resources failed.");
		}
		return false;
	}

	bool Skylighting::DispatchNormalizedDebugView() noexcept
	{
		if (!_normalizedDebugPreviewEnabled.load(std::memory_order_acquire) ||
			!_normalizedResourcesAllocated.load(std::memory_order_acquire) ||
			!_occlusionSRV ||
			!_normalizedOcclusionUAV ||
			!_normalizedRangeBuffer ||
			!_normalizedRangeSRV ||
			!_normalizedRangeUAV ||
			!_normalizedReduceCS ||
			!_normalizedWriteCS) {
			return false;
		}

		auto* context = cs::engine::GetImmediateContext();
		if (!context) {
			return false;
		}

		const std::array<std::uint32_t, 2> initialRange{
			std::numeric_limits<std::uint32_t>::max(),
			0
		};
		context->UpdateSubresource(
			_normalizedRangeBuffer.get(),
			0,
			nullptr,
			initialRange.data(),
			0,
			0);

		cs::engine::OMScope omScope(context);
		cs::ComputeScope computeScope(context, 2, 0, 1, 0);
		cs::render::annotation::ScopedEvent event(
			"Skylighting/OcclusionDepthNormalized");

		ID3D11ShaderResourceView* depthSRV = _occlusionSRV.get();
		ID3D11UnorderedAccessView* rangeUAV = _normalizedRangeUAV.get();
		context->CSSetShaderResources(0, 1, &depthSRV);
		context->CSSetUnorderedAccessViews(0, 1, &rangeUAV, nullptr);
		context->CSSetShader(_normalizedReduceCS.get(), nullptr, 0);
		context->Dispatch(
			kOcclusionSize / kDebugThreadGroupSize,
			kOcclusionSize / kDebugThreadGroupSize,
			1);

		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		std::array<ID3D11ShaderResourceView*, 2> normalizeSRVs{
			_occlusionSRV.get(),
			_normalizedRangeSRV.get()
		};
		ID3D11UnorderedAccessView* normalizedUAV =
			_normalizedOcclusionUAV.get();
		context->CSSetShaderResources(
			0,
			static_cast<UINT>(normalizeSRVs.size()),
			normalizeSRVs.data());
		context->CSSetUnorderedAccessViews(
			0, 1, &normalizedUAV, nullptr);
		context->CSSetShader(_normalizedWriteCS.get(), nullptr, 0);
		context->Dispatch(
			kOcclusionSize / kDebugThreadGroupSize,
			kOcclusionSize / kDebugThreadGroupSize,
			1);

		_normalizedViewDispatchedLastFrame.store(
			true, std::memory_order_release);
		return true;
	}

	void Skylighting::RenderProducer() noexcept
	{
		_sceneTraversalOverrideAppliedLastRender.store(
			false, std::memory_order_release);
		if (!_resourcesReady.load(std::memory_order_acquire) ||
			!_occlusionTexture ||
			!_occlusionSRV ||
			!_occlusionDSV) {
			_failureState.store(
				FailureState::kResourcesUnavailable,
				std::memory_order_release);
			return;
		}

		auto* sky = RE::Sky::GetSingleton();
		if (!sky) {
			_failureState.store(
				FailureState::kSkyUnavailable, std::memory_order_release);
			return;
		}
		auto* precipitation = sky->precip;
		if (!precipitation) {
			_failureState.store(
				FailureState::kPrecipitationUnavailable,
				std::memory_order_release);
			return;
		}
		auto* rendererData = RE::BSGraphics::GetRendererData();
		std::int32_t platformID = -1;
		auto* nativeTarget = ResolvePrecipitationOcclusionTarget(
			rendererData, platformID);
		_precipitationOcclusionPlatformID.store(
			platformID, std::memory_order_release);
		if (!nativeTarget) {
			_failureState.store(
				FailureState::kRendererUnavailable,
				std::memory_order_release);
			return;
		}

		try {
			DepthTargetOverride targetOverride(
				*nativeTarget,
				_occlusionTexture.get(),
				_occlusionSRV.get(),
				_occlusionDSV.get());

			_workingVfuncCallsDuringRender.store(0, std::memory_order_relaxed);
			_workingVfuncLastRenderModeDuringRender.store(
				-1, std::memory_order_relaxed);
			_workingGeometryCount.store(0, std::memory_order_relaxed);
			_workingPassCount.store(0, std::memory_order_relaxed);
			OcclusionRenderScope renderScope(_inOcclusionRender);

			using Producer = void (*)(RE::Precipitation*, void*);
			static REL::Relocation<Producer> producer{
				REL::ID({ 856638, 2208823, 2208823 })
			};
			if (producer.address() == 0) {
				throw std::runtime_error(
					"Precipitation occlusion producer is unavailable");
			}

			auto* accumulator =
				precipitation->occlusionData.accumulator.get();
			const bool accumulatorPresent = accumulator != nullptr;
			const std::int64_t accumulatorRenderMode = accumulator ?
				static_cast<std::int64_t>(accumulator->renderMode) :
				-1;
			_accumulatorPresent.store(
				accumulatorPresent, std::memory_order_release);
			_accumulatorRenderMode.store(
				accumulatorRenderMode, std::memory_order_release);

			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto* cell = player ? player->GetParentCell() : nullptr;
			const bool inInteriorResolved = cell != nullptr;
			const bool inInterior = cell && !cell->IsExterior();
			const auto* worldspace =
				cell && cell->IsExterior() ? cell->worldSpace : nullptr;
			const std::int64_t currentCellFormID = cell ?
				static_cast<std::int64_t>(cell->GetFormID()) :
				-1;
			const std::int64_t currentWorldspaceFormID = worldspace ?
				static_cast<std::int64_t>(worldspace->GetFormID()) :
				-1;
			_inInteriorResolved.store(
				inInteriorResolved, std::memory_order_release);
			_inInterior.store(inInterior, std::memory_order_release);
			_currentCellFormID.store(
				currentCellFormID, std::memory_order_release);
			_currentWorldspaceFormID.store(
				currentWorldspaceFormID, std::memory_order_release);

			const float requestedExtent =
				_occlusionExtent.load(std::memory_order_acquire);
			const float effectiveExtent =
				_extentGlobal ? requestedExtent : kEngineDefaultExtent;
			_effectiveExtent.store(
				effectiveExtent, std::memory_order_release);

			{
				FloatOverride extentOverride(_extentGlobal, requestedExtent);
				// Keep the zenith stable until probe accumulation can use jitter.
				DirectionOverride directionOverride(
					_directionXGlobal,
					_directionYGlobal,
					_directionZGlobal);
				const bool forceSceneTraversal =
					_forceSceneTraversal.load(std::memory_order_acquire);
				_sceneTraversalOverrideAppliedLastRender.store(
					forceSceneTraversal &&
						_preCulledTempDisabledGlobal != nullptr,
					std::memory_order_release);
				ByteOverride sceneTraversalOverride(
					_preCulledTempDisabledGlobal,
					std::uint8_t{ 1 },
					forceSceneTraversal);
				producer(precipitation, nullptr);
			}

			auto* occlusionCamera =
				precipitation->occlusionData.camera.get();
			const bool occlusionCameraPresent = occlusionCamera != nullptr;
			float frustumNear = kUnavailableDiagnosticFloat;
			float frustumFar = kUnavailableDiagnosticFloat;
			float frustumLeft = kUnavailableDiagnosticFloat;
			float frustumRight = kUnavailableDiagnosticFloat;
			float frustumTop = kUnavailableDiagnosticFloat;
			float frustumBottom = kUnavailableDiagnosticFloat;
			std::int64_t frustumOrthographic = -1;
			if (occlusionCamera) {
				const auto& frustum = occlusionCamera->viewFrustum;
				const auto& [left, right, top, bottom, nearZ, farZ, ortho] =
					frustum;
				frustumNear = nearZ;
				frustumFar = farZ;
				frustumLeft = left;
				frustumRight = right;
				frustumTop = top;
				frustumBottom = bottom;
				frustumOrthographic = ortho ? 1 : 0;
			}
			_occlusionCameraPresent.store(
				occlusionCameraPresent, std::memory_order_release);
			_occlusionFrustumNear.store(
				frustumNear, std::memory_order_release);
			_occlusionFrustumFar.store(
				frustumFar, std::memory_order_release);
			_occlusionFrustumLeft.store(
				frustumLeft, std::memory_order_release);
			_occlusionFrustumRight.store(
				frustumRight, std::memory_order_release);
			_occlusionFrustumTop.store(
				frustumTop, std::memory_order_release);
			_occlusionFrustumBottom.store(
				frustumBottom, std::memory_order_release);
			_occlusionFrustumOrthographic.store(
				frustumOrthographic, std::memory_order_release);

			DirectX::XMFLOAT4X4 transform{};
			if (_matrixGlobal) {
				std::memcpy(
					std::addressof(transform),
					_matrixGlobal,
					sizeof(transform));
			}

			DirectX::XMFLOAT4 direction{};
			if (occlusionCamera) {
				const auto& cameraDirection =
					occlusionCamera->GetLocalRotate()[0];
				direction = {
					-cameraDirection.x,
					-cameraDirection.y,
					-cameraDirection.z,
					0.0f
				};
			}
			const bool valid =
				IsFiniteMatrix(transform) && IsFiniteDirection(direction);
			{
				std::scoped_lock lock(_occlusionDataMutex);
				_occlusionData = {
					.transform = transform,
					.direction = direction,
					.extent = effectiveExtent,
					.valid = valid
				};
			}

			const auto geometryCount =
				_workingGeometryCount.load(std::memory_order_relaxed);
			const auto vfuncCallsDuringRender =
				_workingVfuncCallsDuringRender.load(
					std::memory_order_relaxed);
			const auto vfuncLastRenderModeDuringRender =
				_workingVfuncLastRenderModeDuringRender.load(
					std::memory_order_relaxed);
			_lastVfuncCallsDuringRender.store(
				vfuncCallsDuringRender, std::memory_order_release);
			_vfuncLastRenderModeDuringRender.store(
				vfuncLastRenderModeDuringRender,
				std::memory_order_release);
			_lastGeometryCount.store(geometryCount, std::memory_order_release);
			_lastPassCount.store(
				_workingPassCount.load(std::memory_order_relaxed),
				std::memory_order_release);
			if (geometryCount == 0 &&
				!_emptyGeometryReported.test_and_set(
					std::memory_order_acq_rel)) {
				const auto resolve = [](REL::ID a_id) noexcept {
					try {
						return a_id.address();
					} catch (...) {
						return std::uintptr_t{ 0 };
					}
				};

				std::uintptr_t shadowSceneNodeGlobal = 0;
				try {
					shadowSceneNodeGlobal = kShadowSceneNode.address();
				} catch (...) {
				}

				const bool shadowSceneNodeResolved =
					shadowSceneNodeGlobal != 0;
				const auto shadowSceneNode = shadowSceneNodeResolved ?
					*reinterpret_cast<const std::uintptr_t*>(
						shadowSceneNodeGlobal) :
					0;
				const auto preCulledObjects = shadowSceneNode ?
					*reinterpret_cast<const std::uintptr_t*>(
						shadowSceneNode + 0x128) :
					0;
				const auto* sceneRoot = preCulledObjects ?
					*reinterpret_cast<RE::NiAVObject* const*>(
						preCulledObjects + 0x18) :
					nullptr;
				const auto* sceneRootNode = sceneRoot ?
					sceneRoot->IsNode() :
					nullptr;
				const std::int64_t sceneRootChildCount = sceneRootNode ?
					static_cast<std::int64_t>(sceneRootNode->children.size()) :
					-1;
				const bool cullingProcessPresent =
					precipitation->occlusionData.cullingProcess != nullptr;
				const auto qEnabledAddress = resolve(kPreCulledQEnabled);
				const auto wantEnabledAddress =
					resolve(kPreCulledWantEnabled);
				const auto displaySettingAddress =
					resolve(kPreCulledDisplaySetting);
				const auto tempDisabledAddress =
					resolve(kPreCulledTempDisabled);
				const bool qEnabledResolved = qEnabledAddress != 0;
				const bool wantEnabledResolved = wantEnabledAddress != 0;
				const bool displaySettingResolved =
					displaySettingAddress != 0;
				const bool tempDisabledResolved = tempDisabledAddress != 0;
				bool qEnabled = false;
				if (qEnabledResolved) {
					const REL::Relocation<bool()> query{ qEnabledAddress };
					qEnabled = query();
				}
				const bool wantEnabled = wantEnabledResolved &&
					*reinterpret_cast<const std::uint8_t*>(
						wantEnabledAddress) != 0;
				const bool displaySetting = displaySettingResolved &&
					*reinterpret_cast<const std::uint8_t*>(
						displaySettingAddress + 0x08) != 0;
				const bool tempDisabled = tempDisabledResolved &&
					*reinterpret_cast<const std::uint8_t*>(
						tempDisabledAddress) != 0;
				const bool sceneTraversalOverrideApplied =
					_sceneTraversalOverrideAppliedLastRender.load(
						std::memory_order_acquire);
				const char* geometryGatherBranch =
					sceneTraversalOverrideApplied ?
					"scene_traversal" :
					!qEnabledResolved ?
						"unresolved" :
						qEnabled ? "pre_culled_cache" : "scene_traversal";

				_shadowSceneNodeGlobalResolved.store(
					shadowSceneNodeResolved, std::memory_order_release);
				_shadowSceneNodePresent.store(
					shadowSceneNode != 0, std::memory_order_release);
				_preCulledObjectsPresent.store(
					preCulledObjects != 0, std::memory_order_release);
				_sceneRootPresent.store(
					sceneRoot != nullptr, std::memory_order_release);
				_sceneRootChildCount.store(
					sceneRootChildCount, std::memory_order_release);
				_cullingProcessPresent.store(
					cullingProcessPresent, std::memory_order_release);
				_accumulatorPresent.store(
					accumulatorPresent, std::memory_order_release);
				_preCulledQEnabled.store(
					qEnabled, std::memory_order_release);
				_preCulledQEnabledResolved.store(
					qEnabledResolved, std::memory_order_release);
				_preCulledWantEnabled.store(
					wantEnabled, std::memory_order_release);
				_preCulledWantEnabledResolved.store(
					wantEnabledResolved, std::memory_order_release);
				_preCulledDisplaySetting.store(
					displaySetting, std::memory_order_release);
				_preCulledDisplaySettingResolved.store(
					displaySettingResolved, std::memory_order_release);
				_preCulledTempDisabled.store(
					tempDisabled, std::memory_order_release);
				_preCulledTempDisabledResolved.store(
					tempDisabledResolved, std::memory_order_release);
				L->error(
					"Occlusion zero-geometry vfunc diagnostics: "
					"vfunc_calls_total={} vfunc_calls_during_render={} "
					"vfunc_calls_mode14={} geometry_count_last_render={} "
					"vfunc_last_render_mode_during_render={}.",
					_vfuncCallsTotal.load(std::memory_order_relaxed),
					vfuncCallsDuringRender,
					geometryCount,
					geometryCount,
					vfuncLastRenderModeDuringRender);
				L->error(
					"Occlusion zero-geometry producer diagnostics: "
					"accumulator_present={} accumulator_render_mode={} "
					"occlusion_camera_present={} "
					"occlusion_frustum_near={} occlusion_frustum_far={} "
					"occlusion_frustum_left={} occlusion_frustum_right={} "
					"occlusion_frustum_top={} occlusion_frustum_bottom={} "
					"occlusion_frustum_orthographic={} "
					"in_interior_resolved={} in_interior={} "
					"current_worldspace_form_id={} current_cell_form_id={}.",
					accumulatorPresent,
					accumulatorRenderMode,
					occlusionCameraPresent,
					frustumNear,
					frustumFar,
					frustumLeft,
					frustumRight,
					frustumTop,
					frustumBottom,
					frustumOrthographic,
					inInteriorResolved,
					inInterior,
					currentWorldspaceFormID,
					currentCellFormID);
				L->error(
					"Occlusion producer gathered zero geometry: "
					"shadow_scene_node_global_resolved={} "
					"shadow_scene_node_present={} "
					"pre_culled_objects_present={} scene_root_present={} "
					"scene_root_child_count={} "
					"culling_process_present={} accumulator_present={} "
					"geometry_gather_branch={} "
					"pre_culled_q_enabled_resolved={} "
					"pre_culled_q_enabled={} "
					"pre_culled_want_enabled_resolved={} "
					"pre_culled_want_enabled={} "
					"pre_culled_display_setting_resolved={} "
					"pre_culled_display_setting={} "
					"pre_culled_temp_disabled_resolved={} "
					"pre_culled_temp_disabled={} "
					"scene_traversal_override_resolved={} "
					"scene_traversal_override_applied_last_render={}.",
					shadowSceneNodeResolved,
					shadowSceneNode != 0,
					preCulledObjects != 0,
					sceneRoot != nullptr,
					sceneRootChildCount,
					cullingProcessPresent,
					accumulatorPresent,
					geometryGatherBranch,
					qEnabledResolved,
					qEnabled,
					wantEnabledResolved,
					wantEnabled,
					displaySettingResolved,
					displaySetting,
					tempDisabledResolved,
					tempDisabled,
					_sceneTraversalOverrideResolved.load(
						std::memory_order_acquire),
					sceneTraversalOverrideApplied);
			}
			_projectionValid.store(valid, std::memory_order_release);
			_producerRanThisFrame.store(true, std::memory_order_release);
			_renderCount.fetch_add(1, std::memory_order_relaxed);
			const bool globalsAvailable =
				_matrixGlobal && _extentGlobal &&
				_directionXGlobal && _directionYGlobal && _directionZGlobal;
			_failureState.store(
				!globalsAvailable ? FailureState::kGlobalsUnavailable :
					valid ? FailureState::kNone :
						FailureState::kProjectionInvalid,
				std::memory_order_release);
			PublishConsumerData();
		} catch (...) {
			_failureState.store(
				FailureState::kProducerException,
				std::memory_order_release);
			L->error("Occlusion producer threw; engine state was restored.");
			PublishConsumerData();
		}
	}

	void Skylighting::DrawSettings()
	{
		bool changed = ImGui::Checkbox("Enabled", &_settings.enabled);
		changed |= ImGui::Checkbox(
			"Force scene traversal", &_settings.forceSceneTraversal);
		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s",
				"The engine normally replays a precipitation-only cache that "
				"is empty outside of rain. Force a real scene traversal for "
				"the occlusion producer.");
		}
		changed |= ImGui::SliderFloat(
			"Occlusion extent",
			&_settings.occlusionExtent,
			kMinOcclusionExtent,
			kMaxOcclusionExtent,
			"%.0f",
			ImGuiSliderFlags_Logarithmic);
		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s",
				"Full ortho width. The map is fixed at 512x512, so more range "
				"costs resolution one for one: 4096 is 8 units/texel; 10000 is "
				"about 19.5 units/texel.");
		}
		changed |= ImGui::SliderFloat(
			"Strength",
			&_settings.strength,
			kMinConsumerSetting,
			kMaxConsumerSetting,
			"%.2f");
		if (auto tooltip = ui::HoverTooltipWrapper()) {
			ImGui::Text(
				"%s",
				"Blends direct occlusion visibility into ambient lighting. "
				"1.0 is upstream-equivalent; lower values attenuate the effect.");
		}
		changed |= ImGui::SliderFloat(
			"Diffuse minimum visibility",
			&_settings.minDiffuseVisibility,
			kMinConsumerSetting,
			kMaxConsumerSetting,
			"%.2f");
		changed |= ImGui::SliderFloat(
			"Specular minimum visibility",
			&_settings.minSpecularVisibility,
			kMinConsumerSetting,
			kMaxConsumerSetting,
			"%.2f");
		ImGui::TextDisabled(
			"Interiors deliberately take the identity path, matching upstream.");
		if (changed) {
			PublishSettings();
			SaveSettings();
		}
		Menu::Get().DrawDebugViewSelector(*this);
	}

	void Skylighting::RestoreDefaultSettings()
	{
		_settings = {};
		PublishSettings();
		SaveSettings();
	}

	Skylighting::OcclusionData Skylighting::GetOcclusionData() const
	{
		std::scoped_lock lock(_occlusionDataMutex);
		return _occlusionData;
	}

	const char* Skylighting::FailureStateName(FailureState a_state) noexcept
	{
		switch (a_state) {
		case FailureState::kResourcesUnavailable:
			return "resources_unavailable";
		case FailureState::kSkyUnavailable:
			return "sky_unavailable";
		case FailureState::kPrecipitationUnavailable:
			return "precipitation_unavailable";
		case FailureState::kRendererUnavailable:
			return "renderer_unavailable";
		case FailureState::kGlobalsUnavailable:
			return "globals_unavailable";
		case FailureState::kProjectionInvalid:
			return "projection_invalid";
		case FailureState::kProducerException:
			return "producer_exception";
		default:
			return "none";
		}
	}

	const char* Skylighting::ResourceStateName(ResourceState a_state) noexcept
	{
		switch (a_state) {
		case ResourceState::kNotAttempted:
			return "not_attempted";
		case ResourceState::kWaitingForNativeTarget:
			return "waiting_native_target";
		case ResourceState::kFailed:
			return "failed";
		case ResourceState::kReady:
			return "ready";
		default:
			return "unknown";
		}
	}

	const char* Skylighting::ResourceWaitReasonName(
		ResourceWaitReason a_reason) noexcept
	{
		switch (a_reason) {
		case ResourceWaitReason::kRendererData:
			return "renderer_data_null";
		case ResourceWaitReason::kNativeTexture:
			return "native_texture_null";
		case ResourceWaitReason::kNativeDepthStencilView:
			return "native_ds_view_0_null";
		case ResourceWaitReason::kNativeTextureAndDepthStencilView:
			return "native_texture_and_ds_view_0_null";
		default:
			return "none";
		}
	}

	const char* Skylighting::ResourceFailureReasonName(
		ResourceFailureReason a_reason) noexcept
	{
		switch (a_reason) {
		case ResourceFailureReason::kDeviceUnavailable:
			return "device_unavailable";
		case ResourceFailureReason::kUnexpectedExtent:
			return "unexpected_native_extent";
		case ResourceFailureReason::kUnsupportedShaderResourceFormat:
			return "unsupported_shader_resource_format";
		case ResourceFailureReason::kTextureCreationFailed:
			return "create_texture_2d_failed";
		case ResourceFailureReason::kShaderResourceViewCreationFailed:
			return "create_shader_resource_view_failed";
		case ResourceFailureReason::kDepthStencilViewCreationFailed:
			return "create_depth_stencil_view_failed";
		case ResourceFailureReason::kUnexpectedException:
			return "unexpected_exception";
		default:
			return "none";
		}
	}

	void Skylighting::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		const bool qEnabledResolved =
			_preCulledQEnabledResolved.load(std::memory_order_acquire);
		const bool qEnabled =
			_preCulledQEnabled.load(std::memory_order_acquire);
		const bool sceneTraversalOverrideApplied =
			_sceneTraversalOverrideAppliedLastRender.load(
				std::memory_order_acquire);
		const auto injection = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto sharedStatus =
			cs::render::GetSkylightingSharedDataStatus();
		const auto validationDetail = GetValidationDetail();
		const auto occlusion = GetOcclusionData();
		const auto footprintFrame =
			sharedStatus.footprintFrame == UINT32_MAX ?
				std::int64_t{ -1 } :
				static_cast<std::int64_t>(sharedStatus.footprintFrame);
		const auto footprintTotal =
			sharedStatus.footprintInside + sharedStatus.footprintOutside;
		const double footprintOutsideFraction =
			footprintTotal == 0 ?
				0.0 :
				static_cast<double>(sharedStatus.footprintOutside)
					/ static_cast<double>(footprintTotal);
		a_sink
			.Field("enabled", _enabled.load(std::memory_order_acquire))
			.Field(
				"force_scene_traversal",
				_forceSceneTraversal.load(std::memory_order_acquire))
			.Field(
				"strength",
				static_cast<double>(_strength.load(std::memory_order_acquire)))
			.Field(
				"min_diffuse_visibility",
				static_cast<double>(
					_minDiffuseVisibility.load(std::memory_order_acquire)))
			.Field(
				"min_specular_visibility",
				static_cast<double>(
					_minSpecularVisibility.load(std::memory_order_acquire)))
			.Field(
				"visibility_debug",
				_visibilityDebugEnabled.load(std::memory_order_acquire))
			.Field(
				"registrations_ready",
				_registrationsReady.load(std::memory_order_acquire))
			.Field(
				"injection_operational",
				_injectionsOperational.load(std::memory_order_acquire))
			.Field("injection_requested", injection.requested)
			.Field(
				"injection_compile_attempted",
				injection.compileAttempted)
			.Field("injection_compile_ok", injection.compileOk)
			.Field(
				"injection_compile_complete",
				injection.compileComplete)
			.Field("injection_swappable", injection.swappable)
			.Field("injection_slot_collision", injection.slotCollision)
			.Field(
				"injection_matches",
				static_cast<std::int64_t>(injection.matches))
			.Field(
				"injection_substitutions",
				static_cast<std::int64_t>(injection.substitutions))
			.Field(
				"injection_passthrough_not_ready",
				static_cast<std::int64_t>(
					injection.passthroughNotReady))
			.Field(
				"route_substitution_mismatch",
				injection.matches != 0
					&& injection.substitutions < injection.matches)
			.Field(
				"consumer_sampler_ready",
				_consumerSamplerReady.load(std::memory_order_acquire))
			.Field("shared_data_ready", cs::render::IsSharedDataReady())
			.Field(
				"consumer_shared_data_published",
				sharedStatus.dataPublished)
			.Field("consumer_srv_published", sharedStatus.srvPublished)
			.Field(
				"consumer_sampler_published",
				sharedStatus.samplerPublished)
			.Field("consumer_srv_bound_last_call", sharedStatus.boundLastCall)
			.Field(
				"consumer_camera_published_last_call",
				sharedStatus.cameraPublishedLastCall)
			.Field(
				"consumer_publish_calls",
				static_cast<std::int64_t>(sharedStatus.publishCalls))
			.Field(
				"consumer_buffer_writes",
				static_cast<std::int64_t>(sharedStatus.bufferWrites))
			.Field(
				"consumer_bind_calls",
				static_cast<std::int64_t>(sharedStatus.bindCalls))
			.Field(
				"consumer_successful_binds",
				static_cast<std::int64_t>(sharedStatus.successfulBinds))
			.Field(
				"consumer_rejected_no_buffer",
				static_cast<std::int64_t>(sharedStatus.rejectedNoBuffer))
			.Field(
				"consumer_rejected_no_data",
				static_cast<std::int64_t>(sharedStatus.rejectedNoData))
			.Field(
				"consumer_rejected_no_srv",
				static_cast<std::int64_t>(sharedStatus.rejectedNoSrv))
			.Field(
				"consumer_rejected_no_sampler",
				static_cast<std::int64_t>(
					sharedStatus.rejectedNoSampler))
			.Field(
				"consumer_rejected_camera_missing",
				static_cast<std::int64_t>(
					sharedStatus.rejectedCameraMissing))
			.Field(
				"consumer_rejected_camera_stale",
				static_cast<std::int64_t>(
					sharedStatus.rejectedCameraStale))
			.Field(
				"consumer_footprint_counter_ready",
				sharedStatus.footprintCounterReady)
			.Field("consumer_footprint_frame", footprintFrame)
			.Field(
				"consumer_footprint_inside_last_frame",
				static_cast<std::int64_t>(sharedStatus.footprintInside))
			.Field(
				"consumer_footprint_outside_last_frame",
				static_cast<std::int64_t>(sharedStatus.footprintOutside))
			.Field(
				"consumer_footprint_outside_fraction",
				footprintOutsideFraction)
			.Field(
				"consumer_footprint_wrong_space_signature",
				sharedStatus.footprintWrongSpaceSignature)
			.Field(
				"consumer_footprint_readbacks_dropped",
				static_cast<std::int64_t>(
					sharedStatus.footprintReadbacksDropped))
			.Field(
				"consumer_camera_origin_compared",
				sharedStatus.cameraOriginCompared)
			.Field(
				"consumer_camera_origin_delta_magnitude",
				static_cast<double>(
					sharedStatus.cameraOriginDeltaMagnitude))
			.Field(
				"consumer_validation_detail",
				validationDetail.empty() ?
					"operational" :
					validationDetail)
			.Field(
				"resources_ready",
				_resourcesReady.load(std::memory_order_acquire))
			.Field(
				"resource_state",
				ResourceStateName(
					_resourceState.load(std::memory_order_acquire)))
			.Field(
				"resource_attempt_count",
				static_cast<std::int64_t>(
					_resourceAttemptCount.load(std::memory_order_relaxed)))
			.Field(
				"resource_wait_reason",
				ResourceWaitReasonName(
					_resourceWaitReason.load(std::memory_order_acquire)))
			.Field(
				"resource_failure_reason",
				ResourceFailureReasonName(
					_resourceFailureReason.load(std::memory_order_acquire)))
			.Field(
				"native_texture_available",
				_nativeTextureAvailable.load(std::memory_order_relaxed))
			.Field(
				"native_srv_available",
				_nativeSRVAvailable.load(std::memory_order_relaxed))
			.Field(
				"native_dsv_available",
				_nativeDSVAvailable.load(std::memory_order_relaxed))
			.Field(
				"first_512x512_depth_stencil_slot",
				static_cast<std::int64_t>(
					_first512DepthStencilSlot.load(std::memory_order_acquire)))
			.Field(
				"precipitation_occlusion_platform_id",
				static_cast<std::int64_t>(
					_precipitationOcclusionPlatformID.load(
						std::memory_order_acquire)))
			.Field(
				"srv_descriptor_synthesized",
				_synthesizedSRVDescriptor.load(std::memory_order_relaxed))
			.Field(
				"outer_hook_installed",
				_outerHookInstalled.load(std::memory_order_acquire))
			.Field(
				"vfunc_hook_installed",
				_vfuncHookInstalled.load(std::memory_order_acquire))
			.Field(
				"producer_ran_this_frame",
				_producerRanThisFrame.load(std::memory_order_acquire))
			.Field(
				"normalized_debug_resources_allocated",
				_normalizedResourcesAllocated.load(std::memory_order_acquire))
			.Field(
				"normalized_view_dispatched_last_frame",
				_normalizedViewDispatchedLastFrame.load(
					std::memory_order_acquire))
			.Field(
				"render_count",
				static_cast<std::int64_t>(
					_renderCount.load(std::memory_order_relaxed)))
			.Field(
				"vfunc_calls_total",
				static_cast<std::int64_t>(
					_vfuncCallsTotal.load(std::memory_order_relaxed)))
			.Field(
				"vfunc_calls_during_render",
				static_cast<std::int64_t>(
					_lastVfuncCallsDuringRender.load(
						std::memory_order_acquire)))
			.Field(
				"vfunc_calls_mode14",
				static_cast<std::int64_t>(
					_lastGeometryCount.load(std::memory_order_acquire)))
			.Field(
				"vfunc_last_render_mode_during_render",
				_vfuncLastRenderModeDuringRender.load(
					std::memory_order_acquire))
			.Field(
				"geometry_count_last_render",
				static_cast<std::int64_t>(
					_lastGeometryCount.load(std::memory_order_acquire)))
			.Field(
				"pass_count_last_render",
				static_cast<std::int64_t>(
					_lastPassCount.load(std::memory_order_acquire)))
			.Field(
				"projection_valid",
				_projectionValid.load(std::memory_order_acquire))
			.Field(
				"shadow_scene_node_global_resolved",
				_shadowSceneNodeGlobalResolved.load(std::memory_order_acquire))
			.Field(
				"shadow_scene_node_present",
				_shadowSceneNodePresent.load(std::memory_order_acquire))
			.Field(
				"pre_culled_objects_present",
				_preCulledObjectsPresent.load(std::memory_order_acquire))
			.Field(
				"scene_root_present",
				_sceneRootPresent.load(std::memory_order_acquire))
			.Field(
				"scene_root_child_count",
				_sceneRootChildCount.load(std::memory_order_acquire))
			.Field(
				"culling_process_present",
				_cullingProcessPresent.load(std::memory_order_acquire))
			.Field(
				"accumulator_present",
				_accumulatorPresent.load(std::memory_order_acquire))
			.Field(
				"accumulator_render_mode",
				_accumulatorRenderMode.load(std::memory_order_acquire))
			.Field(
				"occlusion_camera_present",
				_occlusionCameraPresent.load(std::memory_order_acquire))
			.Field(
				"occlusion_frustum_near",
				static_cast<double>(
					_occlusionFrustumNear.load(std::memory_order_acquire)))
			.Field(
				"occlusion_frustum_far",
				static_cast<double>(
					_occlusionFrustumFar.load(std::memory_order_acquire)))
			.Field(
				"occlusion_frustum_left",
				static_cast<double>(
					_occlusionFrustumLeft.load(std::memory_order_acquire)))
			.Field(
				"occlusion_frustum_right",
				static_cast<double>(
					_occlusionFrustumRight.load(std::memory_order_acquire)))
			.Field(
				"occlusion_frustum_top",
				static_cast<double>(
					_occlusionFrustumTop.load(std::memory_order_acquire)))
			.Field(
				"occlusion_frustum_bottom",
				static_cast<double>(
					_occlusionFrustumBottom.load(std::memory_order_acquire)))
			.Field(
				"occlusion_frustum_orthographic",
				_occlusionFrustumOrthographic.load(
					std::memory_order_acquire))
			.Field(
				"in_interior_resolved",
				_inInteriorResolved.load(std::memory_order_acquire))
			.Field(
				"in_interior",
				_inInterior.load(std::memory_order_acquire))
			.Field(
				"current_worldspace_form_id",
				_currentWorldspaceFormID.load(std::memory_order_acquire))
			.Field(
				"current_cell_form_id",
				_currentCellFormID.load(std::memory_order_acquire))
			.Field(
				"geometry_gather_branch",
				sceneTraversalOverrideApplied ?
					"scene_traversal" :
					!qEnabledResolved ?
						"unresolved" :
						qEnabled ? "pre_culled_cache" : "scene_traversal")
			.Field(
				"pre_culled_q_enabled_resolved",
				qEnabledResolved)
			.Field(
				"pre_culled_q_enabled",
				qEnabled)
			.Field(
				"pre_culled_want_enabled_resolved",
				_preCulledWantEnabledResolved.load(
					std::memory_order_acquire))
			.Field(
				"pre_culled_want_enabled",
				_preCulledWantEnabled.load(std::memory_order_acquire))
			.Field(
				"pre_culled_display_setting_resolved",
				_preCulledDisplaySettingResolved.load(
					std::memory_order_acquire))
			.Field(
				"pre_culled_display_setting",
				_preCulledDisplaySetting.load(std::memory_order_acquire))
			.Field(
				"pre_culled_temp_disabled_resolved",
				_preCulledTempDisabledResolved.load(
					std::memory_order_acquire))
			.Field(
				"pre_culled_temp_disabled",
				_preCulledTempDisabled.load(std::memory_order_acquire))
			.Field(
				"scene_traversal_override_resolved",
				_sceneTraversalOverrideResolved.load(
					std::memory_order_acquire))
			.Field(
				"scene_traversal_override_applied_last_render",
				sceneTraversalOverrideApplied)
			.Field(
				"matrix_global_resolved",
				_matrixGlobalResolved.load(std::memory_order_acquire))
			.Field(
				"extent_global_resolved",
				_extentGlobalResolved.load(std::memory_order_acquire))
			.Field(
				"direction_globals_resolved",
				_directionGlobalsResolved.load(std::memory_order_acquire))
			.Field(
				"configured_extent",
				static_cast<double>(
					_occlusionExtent.load(std::memory_order_acquire)))
			.Field(
				"effective_extent",
				static_cast<double>(
					_effectiveExtent.load(std::memory_order_acquire)))
			.Field("occlusion_direction_x", occlusion.direction.x)
			.Field("occlusion_direction_y", occlusion.direction.y)
			.Field("occlusion_direction_z", occlusion.direction.z)
			.Field("occlusion_transform_00", occlusion.transform._11)
			.Field("occlusion_transform_01", occlusion.transform._12)
			.Field("occlusion_transform_02", occlusion.transform._13)
			.Field("occlusion_transform_03", occlusion.transform._14)
			.Field("occlusion_transform_10", occlusion.transform._21)
			.Field("occlusion_transform_11", occlusion.transform._22)
			.Field("occlusion_transform_12", occlusion.transform._23)
			.Field("occlusion_transform_13", occlusion.transform._24)
			.Field("occlusion_transform_20", occlusion.transform._31)
			.Field("occlusion_transform_21", occlusion.transform._32)
			.Field("occlusion_transform_22", occlusion.transform._33)
			.Field("occlusion_transform_23", occlusion.transform._34)
			.Field("occlusion_transform_30", occlusion.transform._41)
			.Field("occlusion_transform_31", occlusion.transform._42)
			.Field("occlusion_transform_32", occlusion.transform._43)
			.Field("occlusion_transform_33", occlusion.transform._44)
			.Field(
				"extent_source",
				_extentGlobalResolved.load(std::memory_order_acquire) ?
					"live_override" :
					"engine_default")
			.Field(
				"failure",
				FailureStateName(
					_failureState.load(std::memory_order_acquire)));
	}
}
