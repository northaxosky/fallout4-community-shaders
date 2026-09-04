#include "Skylighting.h"

#include <d3d11.h>
#include <imgui.h>

#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/Annotation.h"
#include "Render/ComputeScope.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/RenderHooks.h"
#include "Render/ShaderInjection.h"
#include "Render/ShaderInjectionDefines.h"
#include "Render/ShaderVariantRuntimeResolver.h"
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
			if (!feature->_inOcclusionRender.load(std::memory_order_acquire)) {
				return func(
					a_property, a_geometry, a_renderMode, a_accumulator);
			}

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

		const auto registerTarget = [this](
			cs::engine::ShaderInjectionTarget a_target,
			cs::engine::ShaderStage a_stage) {
			std::vector<cs::engine::ShaderSlotClaim> slotClaims{
				{
					a_stage,
					cs::engine::ShaderResourceType::kConstantBuffer,
					cs::render::kSkylightingDataSlot
				}
			};
			if (a_stage == cs::engine::ShaderStage::kCompute) {
				slotClaims.push_back({
					a_stage,
					cs::engine::ShaderResourceType::kShaderResource,
					cs::render::kSkylightingComputeTextureSlot
				});
				slotClaims.push_back({
					a_stage,
					cs::engine::ShaderResourceType::kSampler,
					cs::render::kSkylightingComputeSamplerSlot
				});
			} else {
				slotClaims.push_back({
					a_stage,
					cs::engine::ShaderResourceType::kShaderResource,
					cs::render::kSkylightingTextureSlot
				});
				slotClaims.push_back({
					a_stage,
					cs::engine::ShaderResourceType::kSampler,
					cs::render::kSkylightingSamplerSlot
				});
			}
			return cs::engine::RegisterReplacement({
				.targetId = a_target,
				.stages = cs::engine::ShaderStageBit(a_stage),
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
				},
				.slotClaims = std::move(slotClaims)
			});
		};
		const bool registered =
			registerTarget(
				cs::engine::ShaderInjectionTarget::kBsdfLight,
				cs::engine::ShaderStage::kPixel)
			&& registerTarget(
				cs::engine::ShaderInjectionTarget::kDfTiledLighting,
				cs::engine::ShaderStage::kCompute);
		if (!registered) {
			FailLoad(
				"Skylighting requires reconstructed BSDFLight and "
				"DFTiledLighting shaders; registering those replacements failed");
			return;
		}

		cs::engine::RegisterPreDeferredLightsImpl(
			[this] { ObserveLightingPath(); });
		stl::write_vfunc<0x2C, BSLightingShaderProperty_GetOcclusionPasses>(
			RE::VTABLE::BSLightingShaderProperty[0]);
		_vfuncHookInstalled.store(true, std::memory_order_release);

		stl::detour_thunk<Precipitation_RenderOcclusionMap>(
			REL::ID({ 1114882, 2208812, 2208812 }));
		_outerHookInstalled.store(true, std::memory_order_release);
		_registrationsReady.store(true, std::memory_order_release);

		L->info(
			"Installed precipitation producer and lighting-property occlusion "
			"hooks; registered BSDFLight and DFTiledLighting skylighting "
			"consumers.");
	}

	void Skylighting::ObserveLightingPath() noexcept
	{
		const auto tiled = cs::engine::QueryTiledLightingEnabled();
		if (!tiled) {
			_unknownPathCount.fetch_add(1, std::memory_order_relaxed);
		} else if (*tiled) {
			_tiledPathCount.fetch_add(1, std::memory_order_relaxed);
		} else {
			_volumePathCount.fetch_add(1, std::memory_order_relaxed);
		}
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
			a_error = "the skylighting shader contributions did not all register";
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

		constexpr std::array targets{
			cs::engine::ShaderInjectionTarget::kBsdfLight,
			cs::engine::ShaderInjectionTarget::kDfTiledLighting
		};
		for (const auto target : targets) {
			const auto snapshot =
				cs::engine::GetShaderInjectionTargetSnapshot(target);
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
		}

		_injectionsOperational.store(true, std::memory_order_release);
		SetValidationDetail({});
		PublishConsumerData();
		L->info(
			"Skylighting routes are ready (PS b{}, t{}, s{}; CS b{}, t{}, "
			"s{}).",
			cs::render::kSkylightingDataSlot,
			cs::render::kSkylightingTextureSlot,
			cs::render::kSkylightingSamplerSlot,
			cs::render::kSkylightingDataSlot,
			cs::render::kSkylightingComputeTextureSlot,
			cs::render::kSkylightingComputeSamplerSlot);
		return true;
	}

	void Skylighting::ObserveRouteDiagnostics() const noexcept
	{
		const auto volume = cs::engine::GetShaderInjectionOutcomeSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto tiled = cs::engine::GetShaderInjectionOutcomeSnapshot(
			cs::engine::ShaderInjectionTarget::kDfTiledLighting);
		const bool mismatch =
			(volume.matches != 0
				&& volume.substitutions < volume.matches)
			|| (tiled.matches != 0
				&& tiled.substitutions < tiled.matches);
		const bool previous = _routeSubstitutionMismatch.exchange(
			mismatch, std::memory_order_acq_rel);
		if (mismatch && !previous) {
			L->warn(
				"Skylighting route substitution mismatch: BSDF "
				"substitutions={}/{}, tiled substitutions={}/{}; reporting "
				"only, rendering remains unchanged.",
				volume.substitutions,
				volume.matches,
				tiled.substitutions,
				tiled.matches);
		} else if (!mismatch && previous) {
			L->info(
				"Skylighting route substitutions now agree: BSDF "
				"substitutions={}/{}, tiled substitutions={}/{}.",
				volume.substitutions,
				volume.matches,
				tiled.substitutions,
				tiled.matches);
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

			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto* cell = player ? player->GetParentCell() : nullptr;
			const bool inInteriorResolved = cell != nullptr;
			const bool inInterior = cell && !cell->IsExterior();
			_inInteriorResolved.store(
				inInteriorResolved, std::memory_order_release);
			_inInterior.store(inInterior, std::memory_order_release);

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
				ByteOverride sceneTraversalOverride(
					_preCulledTempDisabledGlobal,
					std::uint8_t{ 1 },
					forceSceneTraversal);
				producer(precipitation, nullptr);
			}

			auto* occlusionCamera =
				precipitation->occlusionData.camera.get();

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
			const auto passCount =
				_workingPassCount.load(std::memory_order_relaxed);
			_lastGeometryCount.store(geometryCount, std::memory_order_release);
			_lastPassCount.store(passCount, std::memory_order_release);
			if (geometryCount == 0 &&
				!_emptyGeometryReported.test_and_set(
					std::memory_order_acq_rel)) {
				L->error(
					"Occlusion producer completed with zero gathered geometry.");
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
		const auto injection = cs::engine::GetShaderInjectionTargetSnapshot(
			cs::engine::ShaderInjectionTarget::kBsdfLight);
		const auto tiledInjection =
			cs::engine::GetShaderInjectionTargetSnapshot(
				cs::engine::ShaderInjectionTarget::kDfTiledLighting);
		const auto hasSkylightingDefine = [](const auto& a_snapshot) {
			const auto define = a_snapshot.defines.find(
				cs::engine::shader_injection_defines::kSkylighting);
			return define != a_snapshot.defines.end() && define->second == "1";
		};
		const auto sharedStatus =
			cs::render::GetSkylightingSharedDataStatus();
		const auto validationDetail = GetValidationDetail();
		const auto occlusion = GetOcclusionData();
		a_sink
			.Field("enabled", _enabled.load(std::memory_order_acquire))
			.Field(
				"force_scene_traversal",
				_forceSceneTraversal.load(std::memory_order_acquire))
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
			.Field(
				"bsdf_define_present",
				hasSkylightingDefine(injection))
			.Field(
				"tiled_define_present",
				hasSkylightingDefine(tiledInjection))
			.Field(
				"bsdf_path_count",
				static_cast<std::int64_t>(
					_volumePathCount.load(std::memory_order_relaxed)))
			.Field(
				"tiled_path_count",
				static_cast<std::int64_t>(
					_tiledPathCount.load(std::memory_order_relaxed)))
			.Field(
				"unknown_path_count",
				static_cast<std::int64_t>(
					_unknownPathCount.load(std::memory_order_relaxed)))
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
				"injection_dispatches",
				static_cast<std::int64_t>(injection.dispatches))
			.Field(
				"tiled_injection_requested",
				tiledInjection.requested)
			.Field(
				"tiled_injection_compile_complete",
				tiledInjection.compileComplete)
			.Field(
				"tiled_injection_swappable",
				tiledInjection.swappable)
			.Field(
				"tiled_injection_slot_collision",
				tiledInjection.slotCollision)
			.Field(
				"tiled_injection_matches",
				static_cast<std::int64_t>(tiledInjection.matches))
			.Field(
				"tiled_injection_substitutions",
				static_cast<std::int64_t>(tiledInjection.substitutions))
			.Field(
				"injection_passthrough_not_ready",
				static_cast<std::int64_t>(
					injection.passthroughNotReady))
			.Field(
				"route_substitution_mismatch",
				(injection.matches != 0
					&& injection.substitutions < injection.matches)
					|| (tiledInjection.matches != 0
						&& tiledInjection.substitutions
							< tiledInjection.matches))
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
				"consumer_pixel_bind_calls",
				static_cast<std::int64_t>(sharedStatus.pixelBindCalls))
			.Field(
				"consumer_pixel_successful_binds",
				static_cast<std::int64_t>(
					sharedStatus.pixelSuccessfulBinds))
			.Field(
				"consumer_compute_bind_calls",
				static_cast<std::int64_t>(sharedStatus.computeBindCalls))
			.Field(
				"consumer_compute_successful_binds",
				static_cast<std::int64_t>(
					sharedStatus.computeSuccessfulBinds))
			.Field(
				"consumer_compute_previous_frame_camera_binds",
				static_cast<std::int64_t>(
					sharedStatus.computePreviousFrameCameraBinds))
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
				"consumer_rejected_camera_validation",
				static_cast<std::int64_t>(
					sharedStatus.rejectedCameraValidation))
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
				"scene_traversal_override_resolved",
				_sceneTraversalOverrideResolved.load(
					std::memory_order_acquire))
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
