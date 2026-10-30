#include "UnifiedWater.h"
#include "Render/SharedDataLayout.h"

#include "Log.h"
#include "RE/B/BSModelDB.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/N/NiUpdateData.h"
#include "RE/T/TES.h"
#include "RE/U/UI.h"
#include "Render/FeatureShaderBindings.h"
#include "Telemetry/Telemetry.h"

#include <DearModdingUI/Client.h>
#include <DearModdingUI/UI.h>

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.unifiedwater");

		std::vector<RE::NiPointer<RE::NiAVObject>> DetachChildren(RE::NiNode* a_parent)
		{
			// FO4: NiTArray size is the active count, not the last occupied slot.
			std::vector<RE::NiPointer<RE::NiAVObject>> children;
			for (const auto& child : a_parent->children)
				if (child)
					children.push_back(child);
			for (const auto& child : children)
				a_parent->DetachChild(child.get());
			return children;
		}
	}

	UnifiedWater* UnifiedWater::GetSingleton()
	{
		static UnifiedWater instance;
		return &instance;
	}

	void UnifiedWater::WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB& a_fo4)
	{
		a_fo4.EnabledUnifiedWater = _ready.load(std::memory_order_acquire) &&
		                                    _injections.load(std::memory_order_acquire) ?
		                                1u :
		                                0u;
	}

	void UnifiedWater::Load()
	{
		_appliedEnabled.store(IsEnabled());
		if (!engine::RegisterFeatureShaderBindings("UnifiedWater", *this)) {
			FailLoad("Unified Water shader contribution registration failed.");
			return;
		}
		_registrations.store(true, std::memory_order_release);
	}

	void UnifiedWater::OnPostPostLoad()
	{
		// FO4: terrain and worldspace cleanup have distinct native signatures.
		stl::detour_thunk<Attach>(RE::ID::BGSTerrainChunk::Attach);
		stl::detour_thunk<Detach>(RE::ID::BGSTerrainChunk::Detach);
		stl::detour_thunk<AttachWater>(RE::ID::BGSTerrainChunk::AttachWater);
		stl::detour_thunk<Visibility>(RE::ID::BGSTerrainNode::UpdateWaterVisibility);
		stl::detour_thunk<SetWorldSpace>(RE::ID::TES::SetWorldSpace);
		stl::detour_thunk<ReleaseWorld>(RE::ID::TESWorldSpace::ReleaseLowResMaxHeightData);
		stl::detour_thunk<UpdateWaterSystem>(RE::ID::TESWaterSystem::UpdateWaterSystem);
		if (!engine::water::InstallWaterMaterialIdentity())
			throw std::runtime_error("Unified Water AddWater material call site did not match.");
		if (!engine::water::InstallTileWaterOverride())
			throw std::runtime_error("Unified Water AddLODWater call site did not match.");
		if (!Attach::func.address() || !Detach::func.address() || !AttachWater::func.address() || !Visibility::func.address() ||
			!SetWorldSpace::func.address() || !ReleaseWorld::func.address() ||
			!UpdateWaterSystem::func.address()) {
			throw std::runtime_error("Unified Water native hook installation failed.");
		}
		_hooks.store(true, std::memory_order_release);
	}

	bool UnifiedWater::ValidateShaderInjections(std::string& a_error)
	{
		if (!_registrations.load(std::memory_order_acquire)) {
			a_error = "shader contribution did not register";
			return false;
		}
		if (!engine::ValidateShaderInjectionRoutes("UnifiedWater", a_error))
			return false;
		_injections.store(true, std::memory_order_release);
		return true;
	}

	void UnifiedWater::OnDataLoaded()
	{
		if (!_hooks.load(std::memory_order_acquire))
			return;
		RE::BSModelDB::DBTraits::ArgsType args{};
		args.loadTextures = true;
		RE::NiPointer<RE::NiNode> nif;
		const auto error = RE::BSModelDB::Demand("meshes\\water\\FO4UnifiedWater.nif", &nif, args);
		if (error != RE::BSResource::ErrorCode::kNone || !nif || nif->children.empty() || !nif->children[0] ||
			!nif->children[0]->IsNode() || nif->children[0]->IsNode()->children.empty()) {
			throw std::runtime_error("Failed to load Unified Water mesh; terrain gates remain vanilla.");
		}
		const auto shape = nif->children[0]->IsNode()->children[0]->IsTriShape();
		if (!shape || !shape->properties[1]) {
			throw std::runtime_error("Unified Water mesh has no valid water shape/property; terrain gates remain vanilla.");
		}
		_mesh = shape;
		_cache.LoadOrGenerateCaches(unified_water::WaterCache::LoadOrderChanged());
		if (!_cache.IsReady() || !engine::water::DisableVanillaWaterLOD()) {
			throw std::runtime_error("Unified Water cache or native branch validation failed; terrain gates remain vanilla.");
		}
		_ready.store(true, std::memory_order_release);
		L->info("Unified Water ready: terrain-only registration gates patched; placed reference water unchanged");
	}

	void UnifiedWater::RestoreOriginal(RE::BSMultiBoundNode* a_parent, ManagedWater& a_water)
	{
		DetachChildren(a_parent);
		for (const auto& child : a_water.original)
			if (child)
				a_parent->AttachChild(child.get(), true);
	}

	void UnifiedWater::AttachVanilla(Chunk* a_chunk, bool a_alreadyAttached)
	{
		if (!a_alreadyAttached)
			Attach::func(a_chunk);
		RegisterChunkWater(a_chunk);
		_vanillaFallbacks.fetch_add(1);
	}

	void UnifiedWater::RegisterChunkWater(Chunk* a_chunk)
	{
		// FO4: AttachWater(false) detaches terrain water; defer registration.
		if (!_lodWaterAttached.load(std::memory_order_acquire)) {
			a_chunk->waterAttached = false;
			return;
		}
		const auto it = _managed.find(a_chunk->water.get());
		if (it != _managed.end())
			engine::water::RegisterTileWater(a_chunk, it->second.water);
		else
			engine::water::RegisterVanillaWater(a_chunk);
		a_chunk->waterAttached = true;
	}

	void UnifiedWater::Attach::thunk(Chunk* a_chunk)
	{
		auto& self = *GetSingleton();
		const std::scoped_lock lock(self._mutex);
		self.ApplyEnabledState();
		if (!self._ready.load(std::memory_order_acquire) || !a_chunk || !a_chunk->loaded || a_chunk->attached ||
			!a_chunk->chunk || !a_chunk->water || !a_chunk->node || !a_chunk->node->manager) {
			func(a_chunk);
			return;
		}
		// FO4: Prepare first so Attach cannot overwrite tile properties.
		engine::water::Prepare(a_chunk);
		self._attached.insert(a_chunk);
		if (!self._appliedEnabled.load(std::memory_order_acquire)) {
			self.AttachVanilla(a_chunk);
			return;
		}
		const auto node = a_chunk->node;
		// Keeps the backing runtime cache alive while tiles are built from it
		const auto result = self._cache.GetInstructions(node->manager->world, node->GetLODLevel(), node->baseCellX, node->baseCellY);
		const auto parent = a_chunk->water.get();
		// Empty instruction sets mean this block should stay vanilla
		if (!result.instructions || result.instructions->empty()) {
			self.AttachVanilla(a_chunk);
			return;
		}
		ManagedWater managed;
		managed.original = DetachChildren(parent);
		// FO4: tiles reuse the Prepare-built terrain water property.
		RE::NiProperty* prepared = nullptr;
		for (const auto& child : managed.original)
			if (const auto geometry = child ? child->IsGeometry() : nullptr; geometry && geometry->properties[1])
				prepared = geometry->properties[1].get();
		if (!prepared) {
			self.RestoreOriginal(parent, managed);
			self.AttachVanilla(a_chunk);
			return;
		}
		std::vector<std::pair<RE::BSTriShape*, const unified_water::Instruction*>> built;
		for (const auto& instruction : *result.instructions) {
			if (!instruction.form.ptr)
				continue;
			RE::NiCloningProcess clone{};
			clone.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			clone.scale = { 1.0f, 1.0f, 1.0f };
			const auto object = self._mesh->CreateClone(clone);
			const auto shape = object ? object->IsTriShape() : nullptr;
			if (!shape)
				break;
			const auto posX = (instruction.x - node->baseCellX) * 4096.0f + instruction.size * 2048.0f;
			const auto posY = (instruction.y - node->baseCellY) * 4096.0f + instruction.size * 2048.0f;
			RE::NiCloningProcess propertyClone{};
			propertyClone.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			const auto property = static_cast<RE::NiProperty*>(prepared->CreateClone(propertyClone));
			if (!property)
				break;
			shape->properties[1] = RE::NiPointer<RE::NiProperty>{ property };
			shape->local.scale = static_cast<float>(instruction.size);
			shape->local.translate = { posX, posY, instruction.waterHeight };
			parent->AttachChild(shape, true);
			managed.tiles.push_back({ shape, instruction.x, instruction.y });
			built.emplace_back(shape, &instruction);
		}
		if (built.size() != result.instructions->size()) {
			self.RestoreOriginal(parent, managed);
			self.AttachVanilla(a_chunk);
			return;
		}
		// FO4: the attach gate bypasses unchecked tile segment getters.
		func(a_chunk);
		std::vector<engine::water::TileWater> tiles;
		tiles.reserve(built.size());
		for (const auto& [shape, instruction] : built)
			tiles.push_back({ shape, static_cast<RE::TESWaterForm*>(instruction->form.ptr), instruction->waterHeight });
		managed.water = std::move(tiles);
		self._managed.insert_or_assign(parent, std::move(managed));
		// FO4: terrain water is drawn only once AddLODWater registers it.
		self.RegisterChunkWater(a_chunk);
		RE::NiUpdateData update{};
		parent->Update(update);
		self._tilesBuilt.fetch_add(built.size());
		self._chunksAttached.fetch_add(1);
	}

	void UnifiedWater::Detach::thunk(Chunk* a_chunk)
	{
		if (!a_chunk) {
			return;
		}
		auto& self = *GetSingleton();
		const std::scoped_lock lock(self._mutex);
		self._attached.erase(a_chunk);
		RE::NiPointer<RE::BSMultiBoundNode> water = a_chunk->water;
		ManagedWater managed;
		bool owned = false;
		{
			const auto it = self._managed.find(water.get());
			if (it != self._managed.end()) {
				managed = std::move(it->second);
				self._managed.erase(it);
				owned = true;
			}
		}
		if (!owned && !self._applying) {
			func(a_chunk);
			return;
		}
		// Hidden water is already unregistered.
		if (self._lodWaterAttached.load(std::memory_order_acquire))
			engine::water::UnregisterVanillaWater(a_chunk);
		// Hide UW-managed water from vanilla detach so it does not delete it
		a_chunk->water = nullptr;
		func(a_chunk);
		if (owned)
			self.RestoreOriginal(water.get(), managed);
		// Park water under the detached chunk so block->water stays valid
		if (a_chunk->chunk) {
			a_chunk->chunk->AttachChild(water.get(), true);
			a_chunk->water = water;
		}
		a_chunk->waterAttached = false;
		self._chunksDetached.fetch_add(1);
	}

	void UnifiedWater::AttachWater::thunk(bool a_attach)
	{
		func(a_attach);
		auto& self = *GetSingleton();
		if (!self._ready.load(std::memory_order_acquire))
			return;
		const std::scoped_lock lock(self._mutex);
		// The engine re-issues Hide every terrain update.
		if (self._lodWaterAttached.exchange(a_attach, std::memory_order_acq_rel) == a_attach)
			return;
		for (const auto chunk : self._attached) {
			if (!chunk || !chunk->water)
				continue;
			if (a_attach) {
				self.RegisterChunkWater(chunk);
			} else {
				engine::water::UnregisterVanillaWater(chunk);
				chunk->waterAttached = false;
			}
		}
		if (a_attach)
			self.RefreshTileVisibility();
	}

	void UnifiedWater::Visibility::thunk(Node* a_node, RE::NiNode* a_parent)
	{
		auto& self = *GetSingleton();
		if (!self._ready.load(std::memory_order_acquire) || !self._appliedEnabled.load(std::memory_order_acquire)) {
			func(a_node, a_parent);
			return;
		}
		const std::scoped_lock lock(self._mutex);
		const auto it = self._managed.find(a_parent);
		// FO4: mixed parents cannot use unchecked native segment traversal.
		engine::water::UpdateVisibility(a_node, a_parent, it != self._managed.end() ? std::span<const engine::water::TileCell>{ it->second.tiles } : std::span<const engine::water::TileCell>{}, self.TilesHidden());
		self._visibilityUpdates.fetch_add(1);
	}

	void UnifiedWater::SetWorldSpace::thunk(RE::TES* a_tes, RE::TESWorldSpace* a_world, bool a_exterior)
	{
		func(a_tes, a_world, a_exterior);
		auto& self = *GetSingleton();
		self._exterior.store(a_world && a_exterior, std::memory_order_release);
		self._refreshTiles.store(true, std::memory_order_release);
	}

	void UnifiedWater::ReleaseWorld::thunk(RE::TESWorldSpace* a_world)
	{
		func(a_world);
		auto& self = *GetSingleton();
		self._exterior.store(false, std::memory_order_release);
		self._refreshTiles.store(true, std::memory_order_release);
	}

	void UnifiedWater::ApplyEnabledState()
	{
		const std::scoped_lock lock(_mutex);
		const auto enabled = IsEnabled();
		if (!_ready.load(std::memory_order_acquire) || _applying ||
			enabled == _appliedEnabled.load(std::memory_order_acquire))
			return;
		// FO4: rebuild at native terrain timing, never on the settings thread.
		const std::vector<Chunk*> chunks(_attached.begin(), _attached.end());
		struct ApplyingScope
		{
			bool& applying;
			~ApplyingScope() { applying = false; }
		} scope{ _applying };
		_applying = true;
		_appliedEnabled.store(enabled, std::memory_order_release);
		for (const auto chunk : chunks) {
			Detach::thunk(chunk);
			Attach::thunk(chunk);
		}
	}

	void UnifiedWater::UpdateWaterSystem::thunk(engine::water::WaterSystem* a_system, RE::BSCullingProcess* a_process)
	{
		// FO4: runs every frame, so live toggles apply with a static camera.
		auto& self = *GetSingleton();
		self.ApplyEnabledState();
		if (self._refreshTiles.exchange(false, std::memory_order_acq_rel))
			self.RefreshTileVisibility();
		func(a_system, a_process);
	}

	bool UnifiedWater::TilesHidden() const
	{
		// FO4: the Pip-Boy map has no Skyrim exterior-LOD MapMenu rendering path.
		return !_exterior.load(std::memory_order_acquire);
	}

	void UnifiedWater::RefreshTileVisibility()
	{
		const std::scoped_lock lock(_mutex);
		const auto hide = TilesHidden();
		for (const auto chunk : _attached) {
			if (!chunk || !chunk->node || !chunk->water)
				continue;
			const auto it = _managed.find(chunk->water.get());
			if (it != _managed.end())
				engine::water::UpdateVisibility(chunk->node, chunk->water.get(), it->second.tiles, hide);
		}
	}

	void UnifiedWater::DrawSettings()
	{
		if (dmui::ui::Button("Regenerate Caches"))
			_cache.RegenerateCaches();
		dmui::ui::Text("%s", "Activation and shader ownership changes require a restart.");
	}

	bool UnifiedWater::IsOverlayActive() const
	{
		const auto progress = _cache.GetProgress();
		return progress.active || progress.failed != 0;
	}

	void UnifiedWater::DrawOverlay()
	{
		const auto progress = _cache.GetProgress();
		if (progress.active) {
			dmui::ui::Text("%s", "Generating Water Cache:");
			const auto text = std::format("{}/{} ({:.1f}%)", progress.completed, progress.total,
				progress.total ? 100.0f * progress.completed / progress.total : 0.0f);
			dmui::ui::ProgressBar(progress.total ? static_cast<float>(progress.completed) / progress.total : 0.0f, {}, text.c_str());
		} else if (progress.failed) {
			dmui::ui::Text("ERROR: Water cache generation failed for %u WorldSpaces. Check FO4CommunityShaders.log", progress.failed);
		}
	}

	void UnifiedWater::CollectTelemetry(telemetry::Sink& a_sink) const
	{
		const auto progress = _cache.GetProgress();
		a_sink.Field("operational", _ready.load() && _injections.load())
			.Field("applied_enabled", _appliedEnabled.load())
			.Field("hooks_ready", _hooks.load())
			.Field("cache_ready", _cache.IsReady())
			.Field("cache_total", progress.total)
			.Field("cache_completed", progress.completed)
			.Field("cache_failed", progress.failed)
			.Field("tiles_built", _tilesBuilt.load())
			.Field("chunks_attached", _chunksAttached.load())
			.Field("chunks_detached", _chunksDetached.load())
			.Field("vanilla_fallbacks", _vanillaFallbacks.load())
			.Field("visibility_updates", _visibilityUpdates.load())
			.Field("tile_water_overrides", engine::water::TileWaterOverrides())
			.Field("exterior", _exterior.load())
			.Field("tiles_hidden", TilesHidden())
			.Field("lod_water_attached", _lodWaterAttached.load());
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(UnifiedWater::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}
