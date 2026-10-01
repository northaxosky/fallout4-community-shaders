#include "InverseSquareLighting.h"
#include "InverseSquareLightingData.h"
#include "InverseSquareLightingMath.h"

#include <DearModdingUI/Client.h>
#include <winrt/base.h>

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <utility>

#include "Render/FeatureShaderContributions.h"
#include "Render/LocalLights.h"
#include "Render/ShaderFamilyDescriptor.h"
#include "Telemetry/Telemetry.h"

namespace cs::features
{
	namespace
	{
		using namespace inverse_square_lighting;
		namespace native = engine::local_lights;
		constexpr std::uint32_t kRasterSlot = 11;
		constexpr std::uint32_t kTiledSlot = 8;
		constexpr auto kDirectory = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Lights";

		struct State
		{
			std::mutex mutex;
			LightSidecar sidecar;
			std::vector<LightDefinition> definitions;
			std::unordered_map<RE::TESFormID, AuthoredLight> forms, references;
			std::array<std::array<PerLightData, native::kCapacity>, 2> snapshots{};
			std::array<winrt::com_ptr<ID3D11Buffer>, 2> buffers;
			std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 2> views;
			winrt::com_ptr<ID3D11Buffer> raster;
			std::optional<PerLightData> uploadedRaster;
			winrt::com_ptr<ID3D11DeviceContext> context;
			std::uint32_t uploadedSide = 0;
			std::atomic<bool> resources{ false }, validated{ false }, hooks{ false }, dataLoaded{ false };
			std::atomic<std::uint64_t> authored{ 0 }, created{ 0 }, removed{ 0 };
			std::atomic<std::uint64_t> removedLights{ 0 }, orphaned{ 0 };
			std::atomic<std::uint64_t> appended{ 0 }, uploads{ 0 }, rasterDraws{ 0 }, luminance{ 0 }, uploadFailures{ 0 };
			std::atomic<std::uint32_t> lastReference{ 0 }, lastForm{ 0 }, lastIndex{ 0 }, lastSide{ 0 };
			std::atomic<std::uint64_t> lastNiLight{ 0 }, lastBSLight{ 0 };
		};
		State g_state;
		thread_local RE::BSLight* g_appendingLight = nullptr;
		thread_local RE::BSLight* g_createdLight = nullptr;
		thread_local PerLightData g_rasterData{};

		bool Enabled()
		{
			return g_state.validated.load() && g_state.dataLoaded.load() &&
			       InverseSquareLighting::GetSingleton()->IsActive();
		}
		bool IsInverseSquare(const PerLightData& a_data)
		{
			return (a_data.lightFlags & static_cast<std::uint32_t>(LightFlags::kInverseSquare)) != 0;
		}
		PerLightData Snapshot(RE::BSLight* a_light, bool a_renderFade)
		{
			if (!a_light || !Enabled())
				return {};
			auto* light = native::Light(*a_light);
			if (!light)
				return {};
			std::scoped_lock lock(g_state.mutex);
			const auto* data = g_state.sidecar.Refresh(*light, native::IsShadowLight(*a_light));
			if (!data)
				return {};
			auto result = data->shaderData;
			if (a_renderFade)
				result.fade *= native::CurrentFade(*a_light);
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
				std::scoped_lock lock(g_state.mutex);
				AuthoredLight authored;
				bool found = false;
				if (const auto it = g_state.forms.find(a_form->GetFormID()); it != g_state.forms.end()) {
					authored = it->second;
					found = true;
				}
				const auto referenceID = a_reference ? a_reference->GetFormID() : 0;
				if (const auto it = g_state.references.find(referenceID); it != g_state.references.end()) {
					authored.Apply(it->second);
					found = true;
				}
				if (found) {
					const auto* existing = g_state.sidecar.Find(*light);
					const auto radius = existing && native::Radius(*light) == existing->shaderData.radius ?
					                        existing->nativeRadius :
					                        native::Radius(*light);
					const auto shadow = wrapper  ? native::IsShadowLight(*wrapper) :
					                    existing ? existing->shadowCaster :
					                               (a_form->data.flags & 0x1C00U) != 0;
					const auto& data = g_state.sidecar.CaptureAuthoredLight(*light, *a_form,
						referenceID, authored, radius, shadow);
					if (IsInverseSquare(data.shaderData))
						native::SetRadius(*light, data.shaderData.radius);
					++g_state.created;
				}
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
					if (const auto* light = native::Light(**a_light)) {
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
					native::SetRadius(*a_light, data->shaderData.radius);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		struct CullLight
		{
			static std::uint32_t thunk(RE::BSLight* a_light, void* a_frustum)
			{
				const auto data = Snapshot(a_light, false);
				if (IsInverseSquare(data)) {
					auto* light = native::Light(*a_light);
					native::SetRadius(*light, data.radius);
				}
				if (auto* light = native::Light(*a_light)) {
					std::scoped_lock lock(g_state.mutex);
					if (g_state.sidecar.UpdateCullRadius(*light))
						native::InvalidateSpotCone(*a_light);
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
					if (auto* light = native::Light(**a_light)) {
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
				const auto side = native::AppendSide();
				const auto index = side < 2 ? native::AppendCount(side) : native::kCapacity;
				const auto data = Snapshot(g_appendingLight, true);
				if (side < 2 && index < native::kCapacity) {
					std::scoped_lock lock(g_state.mutex);
					g_state.snapshots[side][index] = data;
					if (IsInverseSquare(data)) {
						++g_state.appended;
						const auto* light = native::Light(*g_appendingLight);
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

		bool WriteBuffer(ID3D11DeviceContext* a_context, ID3D11Buffer* a_buffer,
			const void* a_data, std::size_t a_size)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(a_context->Map(a_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				++g_state.uploadFailures;
				FeatureManager::Get().QuarantineRuntimeCallback(*InverseSquareLighting::GetSingleton(),
					"ISL metadata upload", "D3D11 Map failed");
				return false;
			}
			std::memcpy(mapped.pData, a_data, a_size);
			a_context->Unmap(a_buffer, 0);
			return true;
		}
		struct UploadLights
		{
			static void thunk(void* a_renderer, void* a_buffer, void* a_data,
				std::uint32_t a_stride, std::uint32_t a_count)
			{
				if (Enabled() && g_state.resources.load() && a_stride == sizeof(native::TiledRecord)) {
					for (std::uint32_t side = 0; side < 2; ++side) {
						if (a_data != native::Records(side))
							continue;
						std::array<PerLightData, native::kCapacity> snapshot;
						{
							std::scoped_lock lock(g_state.mutex);
							snapshot = g_state.snapshots[side];
						}
						const auto count = a_count == 0 ? native::kCapacity : a_count;
						if (count <= native::kCapacity &&
							WriteBuffer(g_state.context.get(), g_state.buffers[side].get(),
								snapshot.data(), count * sizeof(PerLightData))) {
							g_state.uploadedSide = side;
							++g_state.uploads;
						}
					}
				}
				func(a_renderer, a_buffer, a_data, a_stride, a_count);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

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

		// FO4: gameplay queries preserve native shape masks and exclusions, without render fade.
		struct Luminance
		{
			static float thunk(RE::BSLight* a_light, RE::NiPoint3* a_position, RE::NiLight* a_ignore)
			{
				const auto data = Snapshot(a_light, false);
				if (!IsInverseSquare(data))
					return func(a_light, a_position, a_ignore);
				auto* light = native::Light(*a_light);
				if (light == a_ignore || light->GetAppCulled() || native::GameplayExcluded(*a_light))
					return 0;
				const auto delta = light->world.translate - *a_position;
				const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
				const float size = std::sqrt(data.sizeBias * 2 / kScaledUnitsSq);
				const float value = (data.color.r + data.color.g + data.color.b) *
				                    data.fade * GetAttenuation(distance, data.radius, size) * (1.0f / 3.0f) *
				                    native::ShapeAttenuation(*a_light, *a_position);
				native::SetLuminance(*a_light, value);
				++g_state.luminance;
				return value;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
		template <class T>
		void InstallHook(REL::ID a_id)
		{
			stl::detour_thunk<T>(a_id);
			if (!T::func.address())
				throw std::runtime_error("Unable to install ISL native hook");
		}
	}

	InverseSquareLighting* InverseSquareLighting::GetSingleton()
	{
		static InverseSquareLighting instance;
		return &instance;
	}
	void InverseSquareLighting::Load()
	{
		std::string error;
		if (!LoadLightDefinitions(kDirectory, g_state.definitions, error)) {
			FailLoad(error);
			return;
		}
		if (!engine::RegisterFeatureShaderContributions("InverseSquareLighting", [](engine::ShaderReplacementRegistration& registration) {
				const bool compute = registration.targetId == engine::ShaderInjectionTarget::kDfTiledLighting;
				const auto stage = compute ? engine::ShaderStage::kCompute : engine::ShaderStage::kPixel;
				registration.isReady = [] { return g_state.resources.load(); };
				registration.bind = [compute](ID3D11DeviceContext* context) {
						if (compute) {
							auto* view = Enabled() ? g_state.views[g_state.uploadedSide].get() : nullptr;
							context->CSSetShaderResources(kTiledSlot, 1, &view);
						} else {
							const PerLightData data = Enabled() ? g_rasterData : PerLightData{};
							if ((g_state.uploadedRaster && std::memcmp(&*g_state.uploadedRaster, &data, sizeof(data)) == 0) ||
								WriteBuffer(context, g_state.raster.get(), &data, sizeof(data))) {
								g_state.uploadedRaster = data;
								auto* buffer = g_state.raster.get();
								engine::BindInjectionConstantBuffers(context, kRasterSlot, 1, &buffer);
								if (IsInverseSquare(data))
									++g_state.rasterDraws;
							} else {
								ID3D11Buffer* empty = nullptr;
								engine::BindInjectionConstantBuffers(context, kRasterSlot, 1, &empty);
							}
						} };
				registration.slotClaims = { { stage, compute ? engine::ShaderResourceType::kShaderResource : engine::ShaderResourceType::kConstantBuffer, compute ? kTiledSlot : kRasterSlot } };
			})) {
			FailLoad("Unable to register the ISL consumers");
			return;
		}
	}
	void InverseSquareLighting::OnDataLoaded()
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		std::scoped_lock lock(g_state.mutex);
		for (const auto& entry : g_state.definitions) {
			auto* form = handler->LookupForm(entry.identity.formID, entry.identity.plugin);
			if (!form || (entry.reference ? !form->As<RE::TESObjectREFR>() : !form->As<RE::TESObjectLIGH>())) {
				Log()->warn("Ignoring ISL identity {}:{:06X}: missing or wrong form type",
					entry.identity.plugin, entry.identity.formID);
				continue;
			}
			auto& target = entry.reference ? g_state.references : g_state.forms;
			target[form->GetFormID()].Apply(entry.data);
			++g_state.authored;
		}
		g_state.dataLoaded = true;
	}
	void InverseSquareLighting::OnPostPostLoad()
	{
		InstallHook<CreateLight>(REL::ID({ 30546, 2198256, 2198256 }));
		InstallHook<AddLight>(REL::ID({ 1109421, 2317457, 2317457 }));
		InstallHook<RemoveLight>(REL::ID({ 162205, 2200909, 2200909 }));
		InstallHook<RemoveSceneLight>(REL::ID({ 1410391, 2317464, 2317464 }));
		InstallHook<UpdateLight>(REL::ID({ 1022957, 2198261, 2198261 }));
		InstallHook<CullLight>(REL::ID({ 1440624, 2318414, 2318414 }));
		InstallHook<TiledCallback>(REL::ID({ 999390, 2317525, 2317525 }));
		InstallHook<AppendLight>(REL::ID({ 1250844, 2318542, 2318542 }));
		InstallHook<UploadLights>(REL::ID({ 402301, 2276904, 2276904 }));
		InstallHook<SetupGeometry>(REL::ID({ 976849, 2319150, 2319150 }));
		InstallHook<Luminance>(REL::ID({ 170662, 2318428, 2318428 }));
		g_state.hooks = true;
	}
	void InverseSquareLighting::OnRuntimeQuarantined() noexcept
	{
		g_state.validated = false;
		std::scoped_lock lock(g_state.mutex);
		g_state.sidecar.RestoreNativeRadii();
		g_state.snapshots = {};
	}
	void InverseSquareLighting::OnD3D11Ready(IDXGIAdapter*, ID3D11Device* a_device)
	{
		a_device->GetImmediateContext(g_state.context.put());
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(PerLightData) * native::kCapacity;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(PerLightData);
		for (std::uint32_t side = 0; side < 2; ++side) {
			D3D11_SUBRESOURCE_DATA initial{ g_state.snapshots[side].data(), 0, 0 };
			if (FAILED(a_device->CreateBuffer(&desc, &initial, g_state.buffers[side].put())) ||
				FAILED(a_device->CreateShaderResourceView(g_state.buffers[side].get(),
					nullptr, g_state.views[side].put())))
				throw std::runtime_error("Unable to create ISL tiled metadata resources");
		}
		desc.ByteWidth = sizeof(PerLightData);
		g_state.uploadedRaster.reset();
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;
		if (FAILED(a_device->CreateBuffer(&desc, nullptr, g_state.raster.put())))
			throw std::runtime_error("Unable to create ISL raster metadata buffer");
		g_state.resources = true;
	}
	bool InverseSquareLighting::ValidateShaderInjections(std::string& a_error)
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
	void InverseSquareLighting::DrawSettings()
	{
		dmui::ui::TextWrapped("Lights are opt-in through %s\\*.toml. Authoring changes require a restart.", kDirectory);
	}
	void InverseSquareLighting::CollectTelemetry(cs::telemetry::Sink& a_sink) const
	{
		a_sink.Field("injection_operational", g_state.validated.load() && g_state.resources.load() &&
												  g_state.hooks.load() && g_state.dataLoaded.load())
			.Field("hooks_installed", g_state.hooks.load())
			.Field("authored_definitions", g_state.authored.load())
			.Field("created_lights", g_state.created.load())
			.Field("removed_references", g_state.removed.load())
			.Field("removed_scene_lights", g_state.removedLights.load())
			.Field("orphaned_lights", g_state.orphaned.load())
			.Field("tiled_appends", g_state.appended.load())
			.Field("tiled_uploads", g_state.uploads.load())
			.Field("raster_draws", g_state.rasterDraws.load())
			.Field("luminance_queries", g_state.luminance.load())
			.Field("upload_failures", g_state.uploadFailures.load())
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
			AutoRegister() { FeatureManager::Get().Register(InverseSquareLighting::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
