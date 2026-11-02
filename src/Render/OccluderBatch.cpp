#include "Render/OccluderBatch.h"
#include "Render/OccluderPasses.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Render/ScopedContextState.h"
#include "Utils/CSBuffer.h"
#include "Utils/CSUtil.h"

#include <d3d11.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <winrt/base.h>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting.capture");

		using Flag = RE::BSUtilityShader::Flags;
		using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;

		constexpr std::uint32_t kHeapCount = 8;
		// Records per class and cull group; overflow stays on the engine.
		constexpr std::uint32_t kArenaCapacity = 8192;
		constexpr std::uint32_t kGroupCount = static_cast<std::uint32_t>(BatchClass::kCount) * 2;
		// Triangles per instance; each chunk draws 64 triangles of one record.
		constexpr std::uint32_t kChunkIndices = 192;
		constexpr std::uint32_t kInstanceStride = 80;
		constexpr std::uint32_t kPoolRecords = 1u << 18;
		// Captures a record buffer may go unseen before its copy is freed.
		constexpr std::uint64_t kPoolIdleCaptures = 600;
		constexpr std::uint8_t kPlainType = 3;
		constexpr std::uint8_t kMeshLODType = 5;
		constexpr std::uint32_t kShaderResourceCount = 11;

		// Techniques the batch reproduces; anything else keeps the engine's pass.
		constexpr std::uint32_t kBatchableTechnique =
			std::to_underlying(Flag::kVertexColors) | std::to_underlying(Flag::kTexture) |
			std::to_underlying(Flag::kNormals) | std::to_underlying(Flag::kBinormalTangent) |
			std::to_underlying(Flag::kRenderShadowMap) | std::to_underlying(Flag::kRenderShadowMapClamped) |
			std::to_underlying(Flag::kCombined) | std::to_underlying(Flag::kInstanced);
		constexpr std::uint32_t kMergeTechnique = std::to_underlying(Flag::kCombined) | std::to_underlying(Flag::kInstanced);

		// Mirrors OccluderBatchVS.hlsl Record.
		struct Record
		{
			float world[4][4];
			std::uint32_t vertexOffset;
			std::uint32_t indexOffset;
			std::uint32_t slotOffset;
			// Heap indices (vertex, index << 4, slot << 8) and the half-position bit 12.
			std::uint32_t heaps;
			std::uint32_t stride;
			std::uint32_t indexCount;
			std::uint32_t trianglesPerSlot;
			std::uint32_t instanceBase;
		};
		static_assert(sizeof(Record) == 96);

		constexpr std::uint32_t kHalfPosition = 1u << 12;

		BatchStats g_stats;
		// Validation can batch one class at a time to bisect a mismatch.
		std::atomic<std::uint32_t> g_classMask{ ~0u };
		std::atomic<bool> g_validating{ false };

		[[nodiscard]] bool RuntimeSupported() noexcept
		{
			// OG: the per-frame constant buffer and pendingCopy offsets are unverified.
			return !REX::FModule::IsRuntimeOG();
		}

		[[nodiscard]] std::uint32_t Load(const std::uint32_t& a_value) noexcept
		{
			return std::atomic_ref<std::uint32_t>{ const_cast<std::uint32_t&>(a_value) }.load(std::memory_order_acquire);
		}

		// Heap blocks fill asynchronously; draw only settled buffers.
		[[nodiscard]] bool Ready(const RE::BSGraphics::Buffer* a_buffer) noexcept
		{
			return a_buffer && a_buffer->buffer && a_buffer->heapAllocated && !a_buffer->pendingCopy &&
			       Load(a_buffer->pendingRequests.load_unchecked()) == 0;
		}

		// One whole-heap raw SRV per engine heap buffer; at most eight exist.
		class HeapTable
		{
		public:
			// Heap index, or -1 when the buffer has no usable view.
			[[nodiscard]] int Slot(RE::BSGraphics::Buffer& a_buffer)
			{
				auto* buffer = reinterpret_cast<ID3D11Buffer*>(a_buffer.buffer);
				if (const auto found = Find(buffer, _count.load(std::memory_order_acquire)); found >= 0)
					return found;

				const std::scoped_lock lock{ _mutex };
				const auto count = _count.load(std::memory_order_relaxed);
				if (const auto found = Find(buffer, count); found >= 0)
					return found;
				auto* device = GetDevice();
				if (!device || count >= kHeapCount)
					return -1;

				D3D11_BUFFER_DESC desc{};
				buffer->GetDesc(&desc);
				if (!(desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS))
					return -1;
				D3D11_SHADER_RESOURCE_VIEW_DESC view{};
				view.Format = DXGI_FORMAT_R32_TYPELESS;
				view.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
				view.BufferEx.NumElements = desc.ByteWidth / 4;
				view.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
				winrt::com_ptr<ID3D11ShaderResourceView> srv;
				if (FAILED(device->CreateShaderResourceView(buffer, &view, srv.put())))
					return -1;
				cs::render::annotation::SetName(srv.get(), "Skylighting/OccluderBatch.Heap.SRV");
				_entries[count].srv = std::move(srv);
				_entries[count].buffer.store(buffer, std::memory_order_relaxed);
				_count.store(count + 1, std::memory_order_release);
				L->info("occluder batch: heap {} is {} MiB", count, desc.ByteWidth >> 20);
				return static_cast<int>(count);
			}

			// Null views past the last heap leave their shader slots unbound.
			void Bind(std::array<ID3D11ShaderResourceView*, kShaderResourceCount>& a_views) const noexcept
			{
				const auto count = _count.load(std::memory_order_acquire);
				for (std::uint32_t i = 0; i < kHeapCount; ++i)
					a_views[i] = i < count ? _entries[i].srv.get() : nullptr;
			}

		private:
			[[nodiscard]] int Find(const ID3D11Buffer* a_buffer, std::uint32_t a_count) const noexcept
			{
				for (std::uint32_t i = 0; i < a_count; ++i) {
					if (_entries[i].buffer.load(std::memory_order_relaxed) == a_buffer)
						return static_cast<int>(i);
				}
				return -1;
			}

			struct Entry
			{
				std::atomic<const ID3D11Buffer*> buffer{ nullptr };
				winrt::com_ptr<ID3D11ShaderResourceView> srv;
			};

			std::array<Entry, kHeapCount> _entries;
			std::atomic<std::uint32_t> _count{ 0 };
			std::mutex _mutex;
		};

		// A pool mirrors each merge shape's record buffer for one draw.
		class InstancePool
		{
		public:
			enum class Result : std::uint8_t
			{
				kOk,
				kInvalid,
				kFull
			};

			void Create()
			{
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = kPoolRecords * kInstanceStride;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
				desc.StructureByteStride = kInstanceStride;
				_pool = std::make_unique<cs::buffer::StructuredBuffer>(desc, kPoolRecords);
				_pool->CreateSRV();
				_pool->SetName("Skylighting/OccluderBatch.Instances");
				_free = { { 0, kPoolRecords } };
			}

			[[nodiscard]] bool Ready() const noexcept { return _pool != nullptr; }
			[[nodiscard]] ID3D11ShaderResourceView* View() const noexcept { return _pool->SRV(); }

			// Worker threads. The held reference keeps the address from being reused.
			[[nodiscard]] Result Acquire(ID3D11Buffer* a_source, std::uint32_t& a_first)
			{
				const auto epoch = _epoch.load(std::memory_order_relaxed);
				{
					const std::shared_lock lock{ _mutex };
					if (const auto found = _entries.find(a_source); found != _entries.end()) {
						found->second->lastSeen.store(epoch, std::memory_order_relaxed);
						a_first = found->second->first;
						return Result::kOk;
					}
				}

				D3D11_BUFFER_DESC desc{};
				a_source->GetDesc(&desc);
				if (desc.StructureByteStride != kInstanceStride || desc.ByteWidth == 0 ||
					desc.ByteWidth % kInstanceStride != 0 || !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
					return Result::kInvalid;
				const std::uint32_t count = desc.ByteWidth / kInstanceStride;

				const std::unique_lock lock{ _mutex };
				if (const auto found = _entries.find(a_source); found != _entries.end()) {
					a_first = found->second->first;
					return Result::kOk;
				}
				const auto first = Allocate(count);
				if (!first)
					return Result::kFull;
				auto entry = std::make_unique<Entry>();
				entry->source.copy_from(a_source);
				entry->first = *first;
				entry->count = count;
				entry->lastSeen.store(epoch, std::memory_order_relaxed);
				_pending.push_back(entry.get());
				_entries.emplace(a_source, std::move(entry));
				_used += count;
				a_first = *first;
				return Result::kOk;
			}

			// Render thread, before the recorders run.
			void BeginCapture()
			{
				const auto epoch = _epoch.fetch_add(1, std::memory_order_relaxed) + 1;
				if (epoch % 64 != 0)
					return;
				const std::unique_lock lock{ _mutex };
				for (auto it = _entries.begin(); it != _entries.end();) {
					const auto& entry = *it->second;
					if (!entry.uploaded || epoch - entry.lastSeen.load(std::memory_order_relaxed) <= kPoolIdleCaptures) {
						++it;
						continue;
					}
					Free(entry.first, entry.count);
					_used -= entry.count;
					it = _entries.erase(it);
				}
			}

			// Render thread, after the recorders: copies each new shape's records in once.
			void Flush(ID3D11DeviceContext* a_context)
			{
				const std::unique_lock lock{ _mutex };
				for (auto* entry : _pending) {
					const D3D11_BOX box{ 0, 0, 0, entry->count * kInstanceStride, 1, 1 };
					a_context->CopySubresourceRegion(
						_pool->Resource(), 0, entry->first * kInstanceStride, 0, 0, entry->source.get(), 0, &box);
					entry->uploaded = true;
				}
				_pending.clear();
				g_stats.poolRecords.store(_used, std::memory_order_relaxed);
			}

		private:
			struct Entry
			{
				winrt::com_ptr<ID3D11Buffer> source;
				std::uint32_t first = 0;
				std::uint32_t count = 0;
				bool uploaded = false;
				std::atomic<std::uint64_t> lastSeen{ 0 };
			};

			[[nodiscard]] std::optional<std::uint32_t> Allocate(std::uint32_t a_count)
			{
				for (auto it = _free.begin(); it != _free.end(); ++it) {
					if (it->second < a_count)
						continue;
					const auto first = it->first;
					const auto left = it->second - a_count;
					_free.erase(it);
					if (left)
						_free.emplace(first + a_count, left);
					return first;
				}
				return std::nullopt;
			}

			void Free(std::uint32_t a_first, std::uint32_t a_count)
			{
				auto next = _free.lower_bound(a_first);
				if (next != _free.begin()) {
					auto previous = std::prev(next);
					if (previous->first + previous->second == a_first) {
						a_first = previous->first;
						a_count += previous->second;
						_free.erase(previous);
					}
				}
				if (next != _free.end() && a_first + a_count == next->first) {
					a_count += next->second;
					_free.erase(next);
				}
				_free.emplace(a_first, a_count);
			}

			std::unique_ptr<cs::buffer::StructuredBuffer> _pool;
			std::shared_mutex _mutex;
			std::unordered_map<ID3D11Buffer*, std::unique_ptr<Entry>> _entries;
			std::vector<Entry*> _pending;
			std::map<std::uint32_t, std::uint32_t> _free;
			std::uint32_t _used = 0;
			std::atomic<std::uint64_t> _epoch{ 0 };
		};

		// Records of one class and cull group, appended by the recorder threads.
		struct Arena
		{
			std::unique_ptr<Record[]> records = std::make_unique<Record[]>(kArenaCapacity);
			std::atomic<std::uint32_t> count{ 0 };
		};

		[[nodiscard]] constexpr std::size_t GroupIndex(BatchClass a_class, bool a_twoSided) noexcept
		{
			return static_cast<std::size_t>(a_class) * 2 + (a_twoSided ? 1 : 0);
		}

		// Draw-side state: shaders, raster variants, upload buffers.
		struct Resources
		{
			winrt::com_ptr<ID3DDeviceContextState> state;
			std::array<winrt::com_ptr<ID3D11VertexShader>, static_cast<std::size_t>(BatchClass::kCount)> shaders;
			winrt::com_ptr<ID3D11RasterizerState> cullBack;
			winrt::com_ptr<ID3D11RasterizerState> cullNone;
			winrt::com_ptr<ID3D11DepthStencilState> depth;
			std::unique_ptr<cs::buffer::ConstantBuffer> drawConstants;
			std::unique_ptr<cs::buffer::StructuredBuffer> records;
			std::unique_ptr<cs::buffer::StructuredBuffer> chunks;
			std::uint32_t chunkCapacity = 0;
		};

		std::array<Arena, kGroupCount> g_arenas;
		HeapTable g_heaps;
		InstancePool g_pool;
		std::unique_ptr<Resources> g_resources;
		bool g_resourcesFailed = false;

		struct Chunk
		{
			std::uint32_t record;
			std::uint32_t firstIndex;
		};
		std::vector<Record> g_recordScratch;
		std::vector<Chunk> g_chunkScratch;

		void Count(BatchFallback a_reason) noexcept
		{
			g_stats.fallback[static_cast<std::size_t>(a_reason)].fetch_add(1, std::memory_order_relaxed);
		}

		// SetupGeometry's cb2 rows: scaled rotation transposed, then translation.
		void FillWorld(Record& a_record, const RE::NiTransform& a_world) noexcept
		{
			const auto& rotate = a_world.rotate;
			const float scale = a_world.scale;
			const float translate[3]{ a_world.translate.x, a_world.translate.y, a_world.translate.z };
			for (std::size_t column = 0; column < 3; ++column) {
				a_record.world[0][column] = rotate.entry[column].x * scale;
				a_record.world[1][column] = rotate.entry[column].y * scale;
				a_record.world[2][column] = rotate.entry[column].z * scale;
				a_record.world[3][column] = rotate.entry[column].w * scale;
			}
			a_record.world[0][3] = translate[0];
			a_record.world[1][3] = translate[1];
			a_record.world[2][3] = translate[2];
			a_record.world[3][3] = 1.0f;
		}

		[[nodiscard]] bool IsMeshLOD(RE::BSGeometry& a_geometry) noexcept
		{
			static const REL::Relocation<const RE::NiRTTI*> rtti{ RE::BSMeshLODTriShape::Ni_RTTI };
			return a_geometry.GetRTTI() == rtti.get();
		}

		// Returns the reason an occluder cannot batch; empty when it was recorded.
		[[nodiscard]] std::optional<BatchFallback> TryRecord(
			const RE::BSLightingShaderProperty& a_property,
			RE::BSGeometry& a_geometry,
			std::uint32_t a_technique,
			BatchClass& a_class)
		{
			if ((a_technique & ~kBatchableTechnique) != 0 || a_property.flags.any(ShaderFlag::kVATSTarget))
				return BatchFallback::kTechnique;
			// The engine builds no draw for animated properties; neither do we.
			const auto* alpha = a_geometry.GetAlphaProperty();
			if (a_property.controllers || (alpha && alpha->controllers))
				return BatchFallback::kAnimated;

			const bool merge = (a_technique & kMergeTechnique) == kMergeTechnique;
			if (!(g_classMask.load(std::memory_order_relaxed) & 1u << static_cast<unsigned>(merge ? BatchClass::kMergeInstanced : BatchClass::kPlain)))
				return BatchFallback::kTechnique;
			auto* shape = static_cast<RE::BSTriShape*>(&a_geometry);
			auto* renderer = static_cast<RE::BSGraphics::TriShape*>(a_geometry.rendererData);
			if (!renderer)
				return BatchFallback::kRendererData;
			if (!Ready(renderer->vertexBuffer) || !Ready(renderer->indexBuffer))
				return BatchFallback::kBufferNotReady;

			Record record{};
			std::uint32_t indexCount = 0;
			RE::BSMergeInstancedTriShape* merged = nullptr;
			if (merge) {
				merged = a_geometry.IsBSMergeInstancedTriShape();
				if (!merged)
					return BatchFallback::kTechnique;
				// The engine reads the LOD bands only for type 5, else numTriangles.
				indexCount = (a_geometry.type == kMeshLODType ? a_geometry.GetRenderableTris(kDefaultLodMode) : shape->numTriangles) * 3;
			} else if (a_geometry.type == kPlainType) {
				indexCount = shape->numTriangles * 3;
			} else if (a_geometry.type == kMeshLODType && IsMeshLOD(a_geometry)) {
				indexCount = a_geometry.GetRenderableTris(kDefaultLodMode) * 3;
			} else {
				return BatchFallback::kTechnique;
			}
			if (indexCount == 0)
				return BatchFallback::kTechnique;

			const int vertexHeap = g_heaps.Slot(*renderer->vertexBuffer);
			const int indexHeap = g_heaps.Slot(*renderer->indexBuffer);
			if (vertexHeap < 0 || indexHeap < 0)
				return BatchFallback::kHeapView;
			record.heaps = static_cast<std::uint32_t>(vertexHeap) | static_cast<std::uint32_t>(indexHeap) << 4;
			record.vertexOffset = renderer->vertexBuffer->dataOffset;
			record.indexOffset = renderer->indexBuffer->dataOffset;
			record.indexCount = indexCount;

			if (merge) {
				auto* slots = merged->shapeIdLookupTable;
				auto* instances = merged->shapeDataBuffer;
				if (!Ready(slots) || !instances || !instances->buffer || Load(instances->pendingRequests.load_unchecked()) != 0)
					return BatchFallback::kStructuredNotReady;
				const int slotHeap = g_heaps.Slot(*slots);
				if (slotHeap < 0)
					return BatchFallback::kHeapView;
				const auto result = g_pool.Acquire(reinterpret_cast<ID3D11Buffer*>(instances->buffer), record.instanceBase);
				if (result != InstancePool::Result::kOk)
					return result == InstancePool::Result::kFull ? BatchFallback::kPoolFull : BatchFallback::kStructuredNotReady;
				record.heaps |= static_cast<std::uint32_t>(slotHeap) << 8;
				record.slotOffset = slots->dataOffset;
				record.stride = merged->vertexStride;
				record.trianglesPerSlot = merged->trianglesPerSlot;
			} else {
				auto& desc = renderer->vertexDesc;
				record.stride = desc.GetSize();
				// Exactly one of the vertex and full-precision bits means half floats.
				using Vertex = RE::BSGraphics::Vertex;
				if (desc.HasFlag(Vertex::VF_VERTEX) != desc.HasFlag(Vertex::VF_FULLPREC))
					record.heaps |= kHalfPosition;
			}

			FillWorld(record, a_geometry.world);
			a_class = merge ? BatchClass::kMergeInstanced : BatchClass::kPlain;
			auto& arena = g_arenas[GroupIndex(a_class, a_property.flags.any(ShaderFlag::kTwoSided))];
			const auto slot = arena.count.fetch_add(1, std::memory_order_relaxed);
			if (slot >= kArenaCapacity)
				return BatchFallback::kArenaFull;
			arena.records[slot] = record;
			return std::nullopt;
		}

		[[nodiscard]] std::unique_ptr<Resources> CreateResources()
		{
			auto* device = GetDevice();
			if (!device)
				return nullptr;
			auto resources = std::make_unique<Resources>();
			resources->state = cs::render::CreateContextState(device);

			for (std::size_t i = 0; i < resources->shaders.size(); ++i) {
				std::vector<std::pair<const char*, const char*>> defines;
				if (i == static_cast<std::size_t>(BatchClass::kMergeInstanced))
					defines.emplace_back("MERGE_INSTANCED", "");
				resources->shaders[i] = cs::util::CompileVertexShader(L"Data\\Shaders\\FO4\\OccluderBatchVS.hlsl", defines);
				if (!resources->shaders[i])
					return nullptr;
			}

			// The stock occlusion map's bias, clip and winding.
			D3D11_RASTERIZER_DESC raster{};
			raster.FillMode = D3D11_FILL_SOLID;
			raster.CullMode = D3D11_CULL_BACK;
			raster.FrontCounterClockwise = TRUE;
			raster.DepthBias = 12;
			raster.SlopeScaledDepthBias = 6.0f;
			raster.DepthBiasClamp = -100.0f;
			raster.DepthClipEnable = TRUE;
			DX::ThrowIfFailed(device->CreateRasterizerState(&raster, resources->cullBack.put()));
			raster.CullMode = D3D11_CULL_NONE;
			DX::ThrowIfFailed(device->CreateRasterizerState(&raster, resources->cullNone.put()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = TRUE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			DX::ThrowIfFailed(device->CreateDepthStencilState(&depth, resources->depth.put()));

			resources->drawConstants = std::make_unique<cs::buffer::ConstantBuffer>(cs::buffer::ConstantBufferDesc(16, false));
			resources->drawConstants->SetName("Skylighting/OccluderBatch.Draw");

			D3D11_BUFFER_DESC desc{};
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(Record);
			desc.ByteWidth = sizeof(Record) * kArenaCapacity * kGroupCount;
			resources->records = std::make_unique<cs::buffer::StructuredBuffer>(desc, kArenaCapacity * kGroupCount);
			resources->records->CreateSRV();
			resources->records->SetName("Skylighting/OccluderBatch.Records");

			g_pool.Create();
			return resources;
		}

		void EnsureChunkCapacity(Resources& a_resources, std::uint32_t a_count)
		{
			if (a_resources.chunks && a_resources.chunkCapacity >= a_count)
				return;
			std::uint32_t capacity = std::max<std::uint32_t>(a_resources.chunkCapacity, 4096);
			while (capacity < a_count)
				capacity *= 2;
			D3D11_BUFFER_DESC desc{};
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(Chunk);
			desc.ByteWidth = capacity * sizeof(Chunk);
			a_resources.chunks = std::make_unique<cs::buffer::StructuredBuffer>(desc, capacity);
			a_resources.chunks->CreateSRV();
			a_resources.chunks->SetName("Skylighting/OccluderBatch.Chunks");
			a_resources.chunkCapacity = capacity;
		}
	}

	BatchStats& GetBatchStats() noexcept
	{
		return g_stats;
	}

	BatchDecision RecordOccluder(
		OccluderBatchMode a_mode,
		const RE::BSLightingShaderProperty& a_property,
		RE::BSGeometry& a_geometry,
		std::uint32_t a_technique)
	{
		if (a_mode == OccluderBatchMode::kOff)
			return BatchDecision::kStock;
		auto batchClass = BatchClass::kPlain;
		const auto reason = TryRecord(a_property, a_geometry, a_technique, batchClass);
		const bool counting = !g_validating.load(std::memory_order_relaxed);
		if (reason) {
			if (counting)
				Count(*reason);
			return BatchDecision::kStock;
		}
		if (counting)
			g_stats.batched[static_cast<std::size_t>(batchClass)].fetch_add(1, std::memory_order_relaxed);
		return BatchDecision::kRecorded;
	}

	void BeginValidationCapture(std::uint32_t a_mask) noexcept
	{
		g_classMask.store(a_mask, std::memory_order_relaxed);
		g_validating.store(true, std::memory_order_relaxed);
	}

	void EndValidationCapture() noexcept
	{
		g_classMask.store(~0u, std::memory_order_relaxed);
		g_validating.store(false, std::memory_order_relaxed);
	}

	bool PrepareOccluderBatch()
	{
		if (!RuntimeSupported()) {
			static bool logged = false;
			if (!std::exchange(logged, true))
				L->info("occluder batch unavailable on OG");
			return false;
		}
		if (g_resourcesFailed)
			return false;
		if (!g_resources) {
			try {
				g_resources = CreateResources();
			} catch (const std::exception& error) {
				L->error("occluder batch resources failed: {}", error.what());
			}
			if (!g_resources) {
				g_resourcesFailed = true;
				L->error("occluder batch disabled: its shaders or buffers could not be created");
				return false;
			}
			L->info("occluder batch ready: arenas {}x{}, instance pool {} records", kGroupCount, kArenaCapacity, kPoolRecords);
		}
		for (auto& arena : g_arenas)
			arena.count.store(0, std::memory_order_relaxed);
		g_pool.BeginCapture();
		return true;
	}

	BatchRecorded GetBatchRecorded() noexcept
	{
		const auto count = [](std::size_t a_group) {
			return std::min(g_arenas[a_group].count.load(std::memory_order_relaxed), kArenaCapacity);
		};
		return {
			.plain = count(GroupIndex(BatchClass::kPlain, false)) + count(GroupIndex(BatchClass::kPlain, true)),
			.mergeInstanced = count(GroupIndex(BatchClass::kMergeInstanced, false)) + count(GroupIndex(BatchClass::kMergeInstanced, true))
		};
	}

	void DrawOccluderBatch(ID3D11DepthStencilView* a_target, std::uint32_t a_extent)
	{
		auto* context = GetImmediateContext();
		auto* engineContext = GetActiveContext();
		if (!g_resources || !context || !engineContext || !engineContext->perFrameConstantBuffer)
			return;
		auto& resources = *g_resources;

		// Records pack back to back; each group owns a contiguous chunk range.
		std::array<std::uint32_t, kGroupCount> chunkBase{};
		std::array<std::uint32_t, kGroupCount> chunkCount{};
		g_recordScratch.clear();
		g_chunkScratch.clear();
		for (std::uint32_t group = 0; group < kGroupCount; ++group) {
			const auto count = std::min(g_arenas[group].count.load(std::memory_order_relaxed), kArenaCapacity);
			const auto* records = g_arenas[group].records.get();
			chunkBase[group] = static_cast<std::uint32_t>(g_chunkScratch.size());
			for (std::uint32_t i = 0; i < count; ++i) {
				const auto index = static_cast<std::uint32_t>(g_recordScratch.size());
				g_recordScratch.push_back(records[i]);
				for (std::uint32_t first = 0; first < records[i].indexCount; first += kChunkIndices)
					g_chunkScratch.push_back({ index, first });
			}
			chunkCount[group] = static_cast<std::uint32_t>(g_chunkScratch.size()) - chunkBase[group];
		}
		if (g_chunkScratch.empty())
			return;

		const ScopedCaptureTimer timer{ GetCaptureTimings().batch };
		const cs::render::annotation::ScopedEvent event{ "Skylighting::OccluderBatch" };
		g_pool.Flush(context);
		EnsureChunkCapacity(resources, static_cast<std::uint32_t>(g_chunkScratch.size()));
		resources.records->Update(g_recordScratch.data(), g_recordScratch.size() * sizeof(Record));
		resources.chunks->Update(g_chunkScratch.data(), g_chunkScratch.size() * sizeof(Chunk));

		const cs::render::ScopedContextState state{ context, resources.state.get() };
		if (!state.IsActive())
			return;
		std::array<ID3D11ShaderResourceView*, kShaderResourceCount> views{};
		g_heaps.Bind(views);
		views[8] = g_pool.View();
		views[9] = resources.chunks->SRV();
		views[10] = resources.records->SRV();
		context->VSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
		// The per-frame buffer still holds the occlusion camera.
		ID3D11Buffer* constants[]{ resources.drawConstants->CB(), reinterpret_cast<ID3D11Buffer*>(engineContext->perFrameConstantBuffer) };
		context->VSSetConstantBuffers(0, 1, &constants[0]);
		context->VSSetConstantBuffers(12, 1, &constants[1]);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->PSSetShader(nullptr, nullptr, 0);
		const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(a_extent), static_cast<float>(a_extent), 0, 1 };
		context->RSSetViewports(1, &viewport);
		context->OMSetDepthStencilState(resources.depth.get(), 0);
		context->OMSetRenderTargets(0, nullptr, a_target);

		for (std::uint32_t group = 0; group < kGroupCount; ++group) {
			if (!chunkCount[group])
				continue;
			const std::uint32_t drawConstants[4]{ chunkBase[group], 0, 0, 0 };
			resources.drawConstants->Update(drawConstants, sizeof(drawConstants));
			context->VSSetShader(resources.shaders[group / 2].get(), nullptr, 0);
			context->RSSetState((group & 1 ? resources.cullNone : resources.cullBack).get());
			context->DrawInstanced(kChunkIndices, chunkCount[group], 0, 0);
			g_stats.draws.fetch_add(1, std::memory_order_relaxed);
		}
	}
}
