#include "LightLimitFix.h"
#include "Authoring/LightDerivation.h"
#include "Clustered/ClusterGrid.h"
#include "Clustered/LightGather.h"
#include "Clustered/StrictLights.h"
#include "Deferred/DeferredLights.h"
#include "RE/B/BSDFTiledLighting.h"
#include "Sidecar/InverseSquareMath.h"
#include "Sidecar/LightSidecar.h"

#include <DearModdingUI/Client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <utility>

#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/Engine.h"
#include "Render/FeatureShaderBindings.h"
#include "Render/ForwardDraw.h"
#include "Render/LocalLights.h"
#include "Render/RuntimeCallback.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Render/SharedDataLayout.h"
#include "Settings/SettingsPersistence.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		using namespace light_limit_fix;
		using Flag = RE::OBJ_LIGH::Flag;
		namespace native = engine::local_lights;
		constexpr std::uint32_t kRasterSlot = 11;
		constexpr std::uint32_t kTiledSlot = 8;
		constexpr std::uint32_t kStrictSlot = 3;
		constexpr std::uint32_t kForwardSlot = 34;
		constexpr auto kDirectory = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Lights";
		// FO4: only the clustered view has a source.
		constexpr std::array<FeatureDebugView, 1> kDebugViews{ { { "clustered_lights", "Clustered Lights Count", FeatureDebugViewKind::kFullscreen } } };
		constexpr std::uint32_t kClusteredLightsMode = 2;

		struct State
		{
			std::mutex mutex;
			LightSidecar sidecar;
			std::vector<LightDefinition> definitions;
			std::unordered_map<RE::TESFormID, AuthoredLight> forms, references;
			DeferredLights deferred;
			LightGather gather;
			ClusterGrid grid;
			buffer::CachedConstantBuffer<StrictLightDataCB> strict;
			std::atomic<bool> gridBuilt{ false };
			std::atomic<std::uint32_t> debugView{ 0 };
			std::atomic<std::uint64_t> clusterDispatches{ 0 }, clusterSkips{ 0 };
			std::atomic<std::uint32_t> clusterLights{ 0 }, clusterShadowLights{ 0 }, clusterConeLights{ 0 }, clusterStrictLights{ 0 };
			std::atomic<std::uint32_t> clusterDropped{ 0 }, clusterX{ 0 }, clusterY{ 0 }, clusterZ{ 0 };
			std::atomic<bool> resources{ false }, validated{ false }, hooks{ false }, dataLoaded{ false };
			std::atomic<std::uint64_t> authored{ 0 }, created{ 0 }, removed{ 0 };
			std::atomic<std::uint64_t> removedLights{ 0 }, orphaned{ 0 };
			std::atomic<std::uint64_t> derived{ 0 }, keptNative{ 0 };
			std::atomic<std::uint64_t> appended{ 0 }, uploads{ 0 }, rasterDraws{ 0 }, luminance{ 0 }, uploadFailures{ 0 };
			std::atomic<std::uint32_t> lastReference{ 0 }, lastForm{ 0 }, lastIndex{ 0 }, lastSide{ 0 };
			std::atomic<std::uint64_t> lastNiLight{ 0 }, lastBSLight{ 0 };
		};
		State g_state;
		thread_local RE::BSLight* g_appendingLight = nullptr;
		thread_local RE::BSLight* g_createdLight = nullptr;
		thread_local LightData g_rasterData{};

		bool Enabled()
		{
			return g_state.validated.load() && g_state.dataLoaded.load() &&
			       LightLimitFix::GetSingleton()->IsActive();
		}
		bool IsInverseSquare(const LightData& a_data)
		{
			return (a_data.lightFlags & static_cast<std::uint32_t>(LightFlags::kInverseSquare)) != 0;
		}
		LightData Snapshot(RE::BSLight* a_light, bool a_renderFade)
		{
			if (!a_light || !Enabled())
				return {};
			auto* light = a_light->GetSourceLight();
			if (!light)
				return {};
			std::scoped_lock lock(g_state.mutex);
			const auto* data = g_state.sidecar.Refresh(*light, a_light->IsShadowLight());
			if (!data)
				return {};
			auto result = data->shaderData;
			if (a_renderFade)
				result.fade *= a_light->currentFade;
			return result;
		}

		// FO4: GenDynamic and AddLight jointly identify the form, NiLight and shadow wrapper.
		struct CreateLight
		{
			static RE::NiLight* thunk(RE::TESObjectLIGH* a_form, RE::TESObjectREFR* a_reference,
				RE::NiAVObject* a_root, bool a_dynamic, bool a_radius, bool a_requester,
				void* a_outLight, float a_scale, bool a_unknown)
			{
				const auto previous = std::exchange(g_createdLight, nullptr);
				auto* light = func(a_form, a_reference, a_root, a_dynamic, a_radius,
					a_requester, a_outLight, a_scale, a_unknown);
				auto* wrapper = std::exchange(g_createdLight, previous);
				if (!light || !a_form || !a_root || !Enabled())
					return light;
				const bool derive = LightLimitFix::GetSingleton()->DeriveUnauthoredLights();
				// GetFade locks the reference's ExtraDataList; keep it outside our mutex.
				const float fade = derive && a_reference ? a_reference->GetFade(a_form->fade) : a_form->fade;
				std::scoped_lock lock(g_state.mutex);
				const auto referenceID = a_reference ? a_reference->GetFormID() : 0;
				const auto* existing = g_state.sidecar.Find(*light);
				const auto radius = existing && light->GetRadius() == existing->shaderData.radius ?
				                        existing->nativeRadius :
				                        light->GetRadius();
				const auto shadow = wrapper  ? wrapper->IsShadowLight() :
				                    existing ? existing->shadowCaster :
				                               a_form->data.flags.any(Flag::kShadowSpotlight, Flag::kShadowHemisphere,
												   Flag::kShadowOmnidirectional);
				AuthoredLight authored;
				float intensityScale = 1;
				bool found = false;
				bool fromDerivation = false;
				if (const auto it = g_state.forms.find(a_form->GetFormID()); it != g_state.forms.end()) {
					authored = it->second;
					found = true;
				}
				if (const auto it = g_state.references.find(referenceID); it != g_state.references.end()) {
					authored.Apply(it->second);
					found = true;
				}
				// A TOML entry is authoritative: derivation never merges with it.
				if (!found && derive) {
					const auto& data = a_form->data;
					if (const auto derived = DeriveLight({ .radius = radius,
							.fade = fade,
							.a = data.attenConstant,
							.b = data.attenScalar,
							.c = data.attenExponent,
							.negatedColor = data.flags.any(Flag::kNegative) })) {
						authored = derived->authored;
						intensityScale = derived->intensityScale;
						found = true;
						fromDerivation = true;
						++g_state.derived;
					} else {
						++g_state.keptNative;
					}
				}
				if (!found) {
					// A reused light may carry a stale entry from an earlier conversion.
					if (existing) {
						g_state.sidecar.Remove(*light);
						light->SetRadius(radius);
					}
					return light;
				}
				const auto& data = g_state.sidecar.CaptureAuthoredLight(*light, *a_form,
					referenceID, authored, radius, shadow, intensityScale, fromDerivation);
				if (IsInverseSquare(data.shaderData))
					light->SetRadius(data.shaderData.radius);
				++g_state.created;
				return light;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct AddLight
		{
			static RE::BSLight* thunk(void* a_scene, RE::NiLight* a_light, void* a_parameters)
			{
				auto* result = func(a_scene, a_light, a_parameters);
				g_createdLight = result;
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// FO4: release sidecar ownership at both reference and scene removal seams.
		struct RemoveLight
		{
			static void thunk(RE::TESObjectREFR* a_reference, bool a_unknown)
			{
				{
					std::scoped_lock lock(g_state.mutex);
					g_state.sidecar.RemoveReference(a_reference->GetFormID());
				}
				func(a_reference, a_unknown);
				++g_state.removed;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct RemoveSceneLight
		{
			static void thunk(void* a_scene, RE::BSLight** a_light)
			{
				if (*a_light) {
					if (const auto* light = (*a_light)->GetSourceLight()) {
						std::scoped_lock lock(g_state.mutex);
						if (g_state.sidecar.Remove(*light))
							++g_state.removedLights;
					}
				}
				func(a_scene, a_light);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// FO4: refresh animated dimmer before native bounds and lists consume the radius.
		struct UpdateLight
		{
			static void thunk(RE::TESObjectLIGH* a_form, RE::NiPointer<RE::NiLight>& a_light,
				RE::TESObjectREFR* a_reference, float a_delta)
			{
				func(a_form, a_light, a_reference, a_delta);
				if (!a_light || !Enabled())
					return;
				std::scoped_lock lock(g_state.mutex);
				if (const auto* data = g_state.sidecar.Refresh(*a_light);
					data && IsInverseSquare(data->shaderData))
					a_light->SetRadius(data->shaderData.radius);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct CullLight
		{
			static std::uint32_t thunk(RE::BSLight* a_light, void* a_frustum)
			{
				const auto data = Snapshot(a_light, false);
				if (IsInverseSquare(data)) {
					auto* light = a_light->GetSourceLight();
					light->SetRadius(data.radius);
				}
				if (auto* light = a_light->GetSourceLight()) {
					std::scoped_lock lock(g_state.mutex);
					if (g_state.sidecar.UpdateCullRadius(*light))
						a_light->InvalidateSpotCone();
				}
				return func(a_light, a_frustum);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// FO4: the callback supplies provenance for dense, double-buffered t6 append order.
		struct TiledCallback
		{
			static std::uint32_t thunk(void* a_callback, RE::BSLight** a_light, bool* a_remove)
			{
				if (*a_light) {
					if (auto* light = (*a_light)->GetSourceLight()) {
						std::scoped_lock lock(g_state.mutex);
						// Native pruning requires the scene wrapper to be the sole NiLight owner.
						if (std::atomic_ref(light->refCount).load() == 2 && g_state.sidecar.Remove(*light))
							++g_state.orphaned;
					}
				}
				const auto previous = std::exchange(g_appendingLight, *a_light);
				const auto result = func(a_callback, a_light, a_remove);
				g_appendingLight = previous;
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct AppendLight
		{
			static void thunk(std::uint32_t a_type, RE::NiPoint3& a_position, float a_radius,
				RE::NiColor& a_color, RE::NiPoint3& a_attenuation,
				bool a_roughness, bool a_rim, bool a_attenuationOnly, bool a_specular)
			{
				const auto side = RE::BSDFTiledLighting::GetWriteSide();
				const auto index = side < 2 ? RE::BSDFTiledLighting::GetLightCounts()[side] : static_cast<std::uint32_t>(RE::BSDFTiledLighting::MAX_LIGHTS);
				const auto data = Snapshot(g_appendingLight, true);
				if (side < 2 && index < RE::BSDFTiledLighting::MAX_LIGHTS) {
					g_state.deferred.Record(side, index, data);
					std::scoped_lock lock(g_state.mutex);
					if (IsInverseSquare(data)) {
						++g_state.appended;
						const auto* light = g_appendingLight->GetSourceLight();
						const auto* runtime = g_state.sidecar.Find(*light);
						g_state.lastReference = runtime ? runtime->referenceID : 0;
						g_state.lastForm = runtime ? runtime->formID : 0;
						g_state.lastNiLight = reinterpret_cast<std::uintptr_t>(light);
						g_state.lastBSLight = reinterpret_cast<std::uintptr_t>(g_appendingLight);
						g_state.lastIndex = index;
						g_state.lastSide = side;
					}
				}
				func(a_type, a_position, a_radius, a_color, a_attenuation,
					a_roughness, a_rim, a_attenuationOnly, a_specular);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct UploadLights
		{
			static void thunk(void* a_renderer, void* a_buffer, void* a_data,
				std::uint32_t a_stride, std::uint32_t a_count)
			{
				if (Enabled() && g_state.resources.load() && a_stride == sizeof(RE::BSDFTiledLighting::TiledLightRecord)) {
					for (std::uint32_t side = 0; side < 2; ++side) {
						if (a_data != RE::BSDFTiledLighting::GetLightRecords()[side])
							continue;
						const std::uint32_t count = a_count == 0 ? DeferredLights::kMaxLights : a_count;
						if (count > DeferredLights::kMaxLights)
							continue;
						if (render::RunRenderCallback(LightLimitFix::GetSingleton(), "LLF metadata upload",
								[&] { g_state.deferred.UploadTiled(side, count); }))
							++g_state.uploads;
						else
							++g_state.uploadFailures;
					}
				}
				func(a_renderer, a_buffer, a_data, a_stride, a_count);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void OnForwardDraw(const render::forward_draw::Draw& a_draw)
		{
			if (!Enabled() || !g_state.resources.load() || !a_draw.pass)
				return;
			StrictLightDataCB data;
			data.roomIndex = g_state.gather.Strict().RoomIndex(*a_draw.pass);
			g_state.strict.Update(data);
		}

		// FO4: raster draws use pass-local b11 instead of LLF's clustered light buffer.
		struct SetupGeometry
		{
			static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass)
			{
				func(a_shader, a_pass);
				g_rasterData = Snapshot(a_pass->numLights && a_pass->sceneLights ?
											a_pass->sceneLights[0] :
											nullptr,
					true);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// FO4: point lights skip SetupGeometry; this writes their per-draw constants.
		struct SetupPointLightGeometry
		{
			static bool thunk(RE::BSShader* a_shader, RE::BSLight* a_light, std::uint32_t a_technique, bool a_setState)
			{
				const auto result = func(a_shader, a_light, a_technique, a_setState);
				g_rasterData = Snapshot(a_light, true);
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// FO4: gameplay queries preserve native shape masks and exclusions, without render fade.
		struct Luminance
		{
			static float thunk(RE::BSLight* a_light, RE::NiPoint3* a_position, RE::NiLight* a_ignore)
			{
				const auto data = Snapshot(a_light, false);
				if (!IsInverseSquare(data))
					return func(a_light, a_position, a_ignore);
				auto* light = a_light->GetSourceLight();
				if (light == a_ignore || light->GetAppCulled() || a_light->excludeFromPointLuminance)
					return 0;
				const auto delta = light->world.translate - *a_position;
				const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
				const float size = std::sqrt(data.sizeBias * 2 / kScaledUnitsSq);
				const float value = (data.color.x + data.color.y + data.color.z) *
				                    data.fade * GetAttenuation(distance, data.radius, size) * (1.0f / 3.0f) *
				                    native::ShapeAttenuation(*a_light, *a_position);
				a_light->luminance = value;
				++g_state.luminance;
				return value;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		template <class T>
		void InstallHook(REL::VariantID a_id)
		{
			stl::detour_thunk<T>(a_id);
			if (!T::func.address())
				throw std::runtime_error("Unable to install LLF native hook");
		}
	}

	LightLimitFix* LightLimitFix::GetSingleton()
	{
		static LightLimitFix instance;
		return &instance;
	}
	bool LightLimitFix::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(light_limit_fix::kSchema, a_config, candidate, a_error))
			return false;
		_settings = candidate;
		_bootSettings = candidate;
		return true;
	}
	bool LightLimitFix::SaveSettings()
	{
		return settings::SaveDelta(light_limit_fix::kSchema, GetConfigKey(), _settings, *Log());
	}
	std::vector<std::string_view> LightLimitFix::GetRestartSettings() const
	{
		return settings::RestartRequired(light_limit_fix::kSchema, _bootSettings, _settings);
	}
	void LightLimitFix::RestoreDefaultSettings()
	{
		_settings = {};
		SaveSettings();
	}
	void LightLimitFix::Load()
	{
		std::string error;
		if (!LoadLightDefinitions(kDirectory, g_state.definitions, error)) {
			FailLoad(error);
			return;
		}
		render::forward_draw::Subscribe(this, "LLF strict lights", &OnForwardDraw);
		if (!engine::RegisterFeatureShaderBindings("LightLimitFix", *this, [](engine::ShaderReplacementRegistration& registration) {
				switch (registration.targetId) {
				case engine::ShaderInjectionTarget::kDfTiledLighting:
					registration.bind = [](ID3D11DeviceContext* context) {
						auto* view = Enabled() ? g_state.deferred.TiledView() : nullptr;
						engine::BindInjectionShaderResources(context, kTiledSlot, 1, &view);
					};
					break;
				case engine::ShaderInjectionTarget::kEffect:
				case engine::ShaderInjectionTarget::kBsWater:
					registration.bind = [](ID3D11DeviceContext* context) {
						const bool ready = Enabled() && g_state.gridBuilt.load();
						ID3D11ShaderResourceView* views[]{
							ready ? g_state.grid.ConesView() : nullptr, ready ? g_state.grid.LightsView() : nullptr,
							ready ? g_state.grid.IndexListView() : nullptr, ready ? g_state.grid.GridView() : nullptr
						};
						engine::BindInjectionShaderResources(context, kForwardSlot, std::size(views), views);
						auto* strict = ready ? g_state.strict.CB() : nullptr;
						engine::BindInjectionConstantBuffers(context, kStrictSlot, 1, &strict);
					};
					break;
				case engine::ShaderInjectionTarget::kBsdfLight:
					registration.bind = [](ID3D11DeviceContext* context) {
						const LightData data = Enabled() ? g_rasterData : LightData{};
						ID3D11Buffer* buffer = nullptr;
						try {
							buffer = g_state.deferred.RasterBuffer(data);
						} catch (...) {
							engine::BindInjectionConstantBuffers(context, kRasterSlot, 1, &buffer);
							throw;
						}
						engine::BindInjectionConstantBuffers(context, kRasterSlot, 1, &buffer);
						if (IsInverseSquare(data))
							++g_state.rasterDraws;
					};
					break;
				default:
					break;
				}
			})) {
			FailLoad("Unable to register the LLF consumers");
			return;
		}
	}
	void LightLimitFix::OnDataLoaded()
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		std::scoped_lock lock(g_state.mutex);
		for (const auto& entry : g_state.definitions) {
			auto* form = handler->LookupForm(entry.identity.formID, entry.identity.plugin);
			if (!form || (entry.reference ? !form->As<RE::TESObjectREFR>() : !form->As<RE::TESObjectLIGH>())) {
				Log()->warn("Ignoring LLF identity {}:{:06X}: missing or wrong form type",
					entry.identity.plugin, entry.identity.formID);
				continue;
			}
			auto& target = entry.reference ? g_state.references : g_state.forms;
			target[form->GetFormID()].Apply(entry.data);
			++g_state.authored;
		}
		if (auto* setting = RE::GameSettingCollection::GetSingleton()->GetSetting("iMagicLightMaxCount")) {
			setting->SetInt(std::numeric_limits<std::int32_t>::max());
			Log()->info("[LLF] Unlocked magic light limit");
		}
		g_state.dataLoaded = true;
	}
	void LightLimitFix::OnPostPostLoad()
	{
		InstallHook<CreateLight>(RE::ID::TESObjectLIGH::GenDynamic);
		InstallHook<AddLight>(RE::ID::ShadowSceneNode::AddLight);
		InstallHook<RemoveLight>(RE::ID::TESObjectREFR::RemoveLight);
		InstallHook<RemoveSceneLight>(RE::ID::ShadowSceneNode::RemoveLight);
		InstallHook<UpdateLight>(RE::ID::TESObjectLIGH::Update);
		InstallHook<CullLight>(RE::ID::BSLight::TestFrustumCull);
		InstallHook<TiledCallback>(RE::ID::BSDFTiledLighting::UpdateLightListTiledPackingCallback);
		InstallHook<AppendLight>(RE::ID::BSDFTiledLighting::AddLight);
		InstallHook<UploadLights>(RE::ID::BSGraphics::Renderer::UpdateStructuredBuffer);
		InstallHook<SetupGeometry>(RE::ID::BSDFLightShader::SetupGeometry);
		InstallHook<SetupPointLightGeometry>(RE::ID::BSDFLightShader::SetupPointLightGeometry);
		InstallHook<Luminance>(RE::ID::BSLight::GetLuminanceAtPoint);
		g_state.hooks = true;
	}
	void LightLimitFix::OnRuntimeQuarantined(std::string_view, std::string_view) noexcept
	{
		g_state.validated = false;
		std::scoped_lock lock(g_state.mutex);
		g_state.gridBuilt = false;
		g_state.sidecar.RestoreNativeRadii();
		g_state.deferred.Clear();
	}
	std::span<const FeatureDebugView> LightLimitFix::GetDebugViews() const noexcept
	{
		return kDebugViews;
	}
	void LightLimitFix::SetDebugView(std::string_view a_view) noexcept
	{
		const auto it = std::ranges::find(kDebugViews, a_view, &FeatureDebugView::id);
		g_state.debugView.store(it == kDebugViews.end() ? 0u : static_cast<std::uint32_t>(it - kDebugViews.begin()) + 1, std::memory_order_release);
	}
	FullscreenDebugData LightLimitFix::GetFullscreenDebugData() const noexcept
	{
		return { .owner = FullscreenDebugOwner::LightLimitFix, .mode = g_state.debugView.load(std::memory_order_acquire) };
	}
	void LightLimitFix::WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB& a_fo4)
	{
		const auto view = g_state.debugView.load(std::memory_order_acquire);
		a_feature.lightLimitFixSettings.EnableLightsVisualisation = view != 0 ? 1u : 0u;
		a_feature.lightLimitFixSettings.LightsVisualisationMode = view != 0 ? kClusteredLightsMode : 0u;
		a_fo4.EnabledLightLimitFix = Enabled() && g_state.gridBuilt.load() ? 1u : 0u;
		if (g_state.resources.load()) {
			const auto size = ClusterGrid::ActiveDimensions();
			std::ranges::copy(size, std::begin(a_feature.lightLimitFixSettings.ClusterSize));
		}
	}
	void LightLimitFix::OnD3D11Ready(IDXGIAdapter*, ID3D11Device*)
	{
		g_state.deferred.Create();
		g_state.grid.Create();
		g_state.strict.Create("LLF StrictLightData");
		g_state.resources = true;
	}
	void LightLimitFix::Prepass()
	{
		g_state.gridBuilt = false;
		if (!Enabled() || !g_state.resources.load())
			return;
		const auto* graphics = engine::GetGraphicsState();
		auto* scene = engine::GetWorldShadowSceneNode();
		const auto camera = graphics ? engine::GetCapturedWorldCameraRecord(graphics->frameCount) : std::nullopt;
		if (!scene || !camera)
			return;
		g_state.gather.Gather(*scene, camera->PosAdjust, &Snapshot);
		if (!g_state.grid.Build(g_state.gather, *camera)) {
			++g_state.clusterSkips;
			return;
		}
		g_state.gridBuilt = true;
		const auto& stats = g_state.gather.Stats();
		const auto size = ClusterGrid::ActiveDimensions();
		g_state.clusterLights = stats.lights;
		g_state.clusterShadowLights = stats.shadowLights;
		g_state.clusterConeLights = stats.coneLights;
		g_state.clusterStrictLights = stats.strictLights;
		g_state.clusterDropped = stats.dropped;
		g_state.clusterX = size[0];
		g_state.clusterY = size[1];
		g_state.clusterZ = size[2];
		++g_state.clusterDispatches;
	}
	bool LightLimitFix::ValidateShaderInjections(std::string& a_error)
	{
		std::vector<engine::ShaderFamilyDescriptor> variants;
		for (std::uint32_t descriptor = 1; descriptor <= 18; ++descriptor)
			variants.push_back({ .target = engine::ShaderInjectionTarget::kDfTiledLighting,
				.stage = engine::ShaderStage::kCompute,
				.descriptor = descriptor,
				.nativeName = "DFTiledLighting" });
		g_state.validated = engine::ValidateShaderInjectionRoutes(GetName(), a_error) &&
		                    engine::PrepareShaderInjectionVariants(variants, a_error);
		return g_state.validated;
	}
	void LightLimitFix::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		if (const ui::Section section{ "llf-general", "General" }; section) {
			edit.Discrete(dmui::ui::Checkbox("Derive Unauthored Lights", &_settings.deriveUnauthoredLights));
			dmui::ui::TextWrapped(
				"Off: only lights authored in %s\\*.toml are inverse-square. On: lights "
				"without an entry convert from their native falloff; an entry is "
				"authoritative and never merged. Changes require a restart.",
				kDirectory);
		}
	}
	void LightLimitFix::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink.Field("injection_operational", g_state.validated.load() && g_state.resources.load() &&
												  g_state.hooks.load() && g_state.dataLoaded.load())
			.Field("hooks_installed", g_state.hooks.load())
			.Field("authored_definitions", g_state.authored.load())
			.Field("created_lights", g_state.created.load())
			.Field("derived_lights", g_state.derived.load())
			.Field("kept_native_lights", g_state.keptNative.load())
			.Field("removed_references", g_state.removed.load())
			.Field("removed_scene_lights", g_state.removedLights.load())
			.Field("orphaned_lights", g_state.orphaned.load())
			.Field("tiled_appends", g_state.appended.load())
			.Field("tiled_uploads", g_state.uploads.load())
			.Field("raster_draws", g_state.rasterDraws.load())
			.Field("luminance_queries", g_state.luminance.load())
			.Field("upload_failures", g_state.uploadFailures.load())
			.Field("cluster_lights", g_state.clusterLights.load())
			.Field("cluster_shadow_lights", g_state.clusterShadowLights.load())
			.Field("cluster_cone_lights", g_state.clusterConeLights.load())
			.Field("cluster_strict_lights", g_state.clusterStrictLights.load())
			.Field("cluster_dropped_lights", g_state.clusterDropped.load())
			.Field("cluster_dim_x", g_state.clusterX.load())
			.Field("cluster_dim_y", g_state.clusterY.load())
			.Field("cluster_dim_z", g_state.clusterZ.load())
			.Field("cluster_dispatches", g_state.clusterDispatches.load())
			.Field("cluster_skipped_frames", g_state.clusterSkips.load())
			.Field("last_join_reference", g_state.lastReference.load())
			.Field("last_join_form", g_state.lastForm.load())
			.Field("last_join_ni_light", g_state.lastNiLight.load())
			.Field("last_join_bs_light", g_state.lastBSLight.load())
			.Field("last_join_t6_index", g_state.lastIndex.load())
			.Field("last_join_buffer_side", g_state.lastSide.load());
	}
	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(LightLimitFix::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
