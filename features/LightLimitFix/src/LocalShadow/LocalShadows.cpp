#include "LocalShadows.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/RendererContext.h"
#include "Utils/CSUtil.h"

#include <RE/B/BSShaderManager.h>
#include <RE/N/NiCamera.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cs::features::light_limit_fix
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.lightlimitfix");

		constexpr auto kCopyPath = L"Data\\Shaders\\FO4\\LightLimitFix\\LocalShadowCopyCS.hlsl";
		constexpr std::uint32_t kEvictFrames = 120;
		constexpr std::uint32_t kMinCacheSize = 128;
		constexpr std::uint32_t kCopyGroupSize = 8;
		constexpr float kFadeInFrames = 10.0f;
		constexpr float kDepthBias = 0.00025f;
		constexpr float kDefaultSpotFalloff = 2.0f;
		constexpr float kFilterRadiusTexels = 4.0f;
		// FO4: without the wrapper flag the native shadow fade distance is unbounded.
		constexpr float kUnboundedFadeDistanceSquared = 1e8f;

		struct CopyCB
		{
			std::uint32_t frontSlice, backSlice, targetSlice, omni;
			std::uint32_t targetSize, mapSize, sourceSize, pad0;
		};
		static_assert(sizeof(CopyCB) == 32);

		RE::BSGraphics::DepthStencilTargetID ShadowTarget() noexcept
		{
			return static_cast<RE::BSGraphics::DepthStencilTargetID>(
				RE::BSGraphics::RenderTargetManager::QShadowMapArrayDepthStencil());
		}

		bool IsFinite(const DirectX::XMFLOAT4X4& a_matrix) noexcept
		{
			for (const auto& row : a_matrix.m) {
				for (const float value : row) {
					if (!std::isfinite(value))
						return false;
				}
			}
			return true;
		}
	}

	void LocalShadows::Create()
	{
		_copyShader = util::RequireComputeShader("LightLimitFix/LocalShadowCopy.CS", kCopyPath);
		_copyCB = std::make_unique<buffer::ConstantBuffer>(buffer::ConstantBufferDesc<CopyCB>());
		_copyCB->SetName("LLF Local Shadow Copy CB");
	}

	ID3D11ShaderResourceView* LocalShadows::DataView() const noexcept
	{
		return _ready.load(std::memory_order_acquire) ? _data->SRV() : nullptr;
	}

	ID3D11ShaderResourceView* LocalShadows::CacheView() const noexcept
	{
		return _ready.load(std::memory_order_acquire) ? _cache->srv.get() : nullptr;
	}

	void LocalShadows::ReleaseResources()
	{
		_ready.store(false, std::memory_order_release);
		_cache.reset();
		_data.reset();
		_slots.clear();
		_upload.clear();
		_layout = {};
		_requested = {};
		stats.slots = 0;
		stats.cached = 0;
		stats.resolution = 0;
	}

	void LocalShadows::Clear()
	{
		std::scoped_lock lock(_mutex);
		ReleaseResources();
	}

	void LocalShadows::Ensure(const LocalShadowConfig& a_config)
	{
		auto* manager = engine::GetRenderTargetManager();
		auto* device = engine::GetDevice();
		if (!manager || !device)
			return;
		const auto& properties = manager->GetDepthStencilTargetProperties(
			RE::BSGraphics::RenderTargetManager::QShadowMapArrayDepthStencil());
		// Omni lights use slices 0 and 1 of the engine array.
		if (properties.width == 0 || properties.arraySize < 2) {
			ReleaseResources();
			return;
		}

		const std::uint32_t engineSize = properties.width;
		const std::uint32_t cacheSize = std::clamp(a_config.resolution, std::min(kMinCacheSize, engineSize), engineSize);
		DXGI_FORMAT format = DXGI_FORMAT_R32_FLOAT;
		UINT support = 0;
		if (properties.use16BitsDepth && SUCCEEDED(device->CheckFormatSupport(DXGI_FORMAT_R16_UNORM, &support)) &&
			(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))
			format = DXGI_FORMAT_R16_UNORM;
		const Layout wanted{
			std::clamp(a_config.slots, kMinLocalShadowSlots, kMaxLocalShadowSlots), cacheSize, engineSize, format
		};
		if (wanted == _requested)
			return;
		ReleaseResources();
		_requested = wanted;

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = cacheSize;
		desc.Height = cacheSize;
		desc.MipLevels = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		std::unique_ptr<buffer::Texture2D> cache;
		std::uint32_t slots = wanted.slots;
		for (;;) {
			desc.ArraySize = slots;
			try {
				cache = std::make_unique<buffer::Texture2D>(desc);
				break;
			} catch (const std::exception&) {
				if (slots <= kMinLocalShadowSlots)
					break;
				slots = std::max(slots / 2, kMinLocalShadowSlots);
			}
		}
		if (!cache) {
			L->warn("Local shadow cache: could not allocate {} slots at {}x{}", wanted.slots, cacheSize, cacheSize);
			return;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srv.Texture2DArray.MipLevels = 1;
		srv.Texture2DArray.ArraySize = slots;
		cache->CreateSRV(srv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uav.Texture2DArray.ArraySize = slots;
		cache->CreateUAV(uav);
		cache->SetName("LLF Local Shadow Cache", "LLF Local Shadow Cache SRV", "LLF Local Shadow Cache UAV");

		auto data = std::make_unique<buffer::StructuredBuffer>(
			buffer::StructuredBufferDesc<LocalShadowData>(slots, false, true), slots);
		data->CreateSRV();
		data->SetName("LLF Local Shadows");

		_cache = std::move(cache);
		_data = std::move(data);
		_layout = { slots, cacheSize, engineSize, format };
		_slots.assign(slots, {});
		_upload.assign(slots, {});
		_texelSize.store(1.0f / static_cast<float>(cacheSize), std::memory_order_release);
		stats.slots = slots;
		stats.resolution = cacheSize;
		_ready.store(true, std::memory_order_release);
		L->info("Local shadow cache: {} slots (requested {}) at {}x{}, engine shadow maps {}x{}",
			slots, wanted.slots, cacheSize, cacheSize, engineSize, engineSize);
	}

	void LocalShadows::Begin(const LocalShadowConfig& a_config, const DirectX::XMFLOAT4& a_eye)
	{
		std::scoped_lock lock(_mutex);
		_frame = engine::GetFrameCount();
		_eye = a_eye;
		if (!a_config.enabled) {
			ReleaseResources();
			return;
		}
		Ensure(a_config);
		if (!_ready.load(std::memory_order_acquire))
			return;
		const float scale = std::isfinite(a_config.filterScale) ?
		                        std::clamp(a_config.filterScale, kMinLocalShadowFilterScale, kMaxLocalShadowFilterScale) :
		                        kMinLocalShadowFilterScale;
		_samples.store(a_config.samples, std::memory_order_release);
		_filterRadius.store(kFilterRadiusTexels / static_cast<float>(_layout.engineSize) * scale, std::memory_order_release);
		for (auto& slot : _slots) {
			if (slot.owner && _frame - slot.lastCopy > kEvictFrames)
				slot = {};
		}
		stats.copies = 0;
	}

	std::uint32_t LocalShadows::Find(const RE::BSLight* a_owner) const
	{
		const auto found = std::ranges::find(_slots, a_owner, &Slot::owner);
		return static_cast<std::uint32_t>(found - _slots.begin());
	}

	float LocalShadows::FadeIn(const Slot& a_slot) const noexcept
	{
		return std::min(static_cast<float>(_frame - a_slot.readyFrame + 1) / kFadeInFrames, 1.0f);
	}

	void LocalShadows::Resolve(RE::BSShadowLight& a_light, LightData& a_data)
	{
		std::scoped_lock lock(_mutex);
		if (!_ready.load(std::memory_order_acquire))
			return;
		const auto index = Find(&a_light);
		if (index >= _slots.size())
			return;
		const auto& slot = _slots[index];
		a_data.lightFlags |= static_cast<std::uint32_t>(LightFlags::kLocalShadow);
		a_data.localShadowIndex = index;
		// Spots fade in whole; other types fade in their shadow.
		if (slot.type == LocalShadowType::kSpot)
			a_data.fade *= FadeIn(slot);
	}

	void LocalShadows::Release(const RE::BSLight& a_light)
	{
		std::scoped_lock lock(_mutex);
		for (auto& slot : _slots) {
			if (slot.owner == &a_light)
				slot = {};
		}
	}

	std::optional<LocalShadows::CopyJob> LocalShadows::Capture(
		Slot& a_slot, RE::BSShadowLight& a_light, engine::ShadowLightKind a_kind)
	{
		using namespace DirectX;
		const auto* entries = a_light.shadowMapData;
		const auto count = a_light.shadowMapCount;
		const auto* source = a_light.GetSourceLight();
		if (!entries || count == 0 || count > 2 || !source)
			return std::nullopt;
		const auto target = RE::BSGraphics::RenderTargetManager::QShadowMapArrayDepthStencil();
		const auto& entry = entries[0];
		const bool omni = a_kind == engine::ShadowLightKind::kParabolic && count == 2;
		if (entry.depthStencilTarget != target || (omni && entries[1].depthStencilTarget != target))
			return std::nullopt;

		// FO4: the map is the bottom-left square of its slice.
		const auto mapWidth = entry.viewport[1] - entry.viewport[0];
		const auto mapHeight = entry.viewport[2] - entry.viewport[3];
		const float radius = source->GetRadius();
		if (mapWidth <= 0 || mapWidth != mapHeight || static_cast<std::uint32_t>(mapWidth) > _layout.engineSize ||
			!std::isfinite(radius) || radius <= 0.0f)
			return std::nullopt;
		const auto mapSize = static_cast<std::uint32_t>(mapWidth);

		XMFLOAT4X4 projection;
		static_assert(sizeof(entry.worldToShadow) == sizeof(projection));
		std::memcpy(&projection, entry.worldToShadow, sizeof(projection));
		if (!IsFinite(projection))
			return std::nullopt;

		auto type = LocalShadowType::kSpot;
		float falloff = kDefaultSpotFalloff;
		if (a_kind == engine::ShadowLightKind::kParabolic) {
			type = omni ? LocalShadowType::kOmni : LocalShadowType::kHemisphere;
		} else {
			// FO4: the matrix bakes in the map's N/R viewport; span the cache slice.
			const float scale = static_cast<float>(_layout.engineSize) / static_cast<float>(mapSize);
			const float offset = scale - 1.0f;
			const XMMATRIX slice{
				scale, 0.0f, 0.0f, 0.0f,
				0.0f, scale, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				offset, -offset, 0.0f, 1.0f
			};
			XMStoreFloat4x4(&projection, XMMatrixMultiply(XMLoadFloat4x4(&projection), slice));
			// FO4: the native edge fade uses the same BSLight field.
			if (std::isfinite(a_light.cone.exponent) && a_light.cone.exponent > 0.0f)
				falloff = a_light.cone.exponent;
		}

		const float bias = std::isfinite(a_light.shadowDepthBias) && a_light.shadowDepthBias > 0.0f ? a_light.shadowDepthBias : 1.0f;
		// FO4: bias grows with source texels per cache texel.
		const float texelScale = std::max(static_cast<float>(mapSize) / static_cast<float>(_layout.cacheSize), 1.0f);
		// FO4: DFLight fades the shadow term against this squared distance.
		const float fadeDistance = RE::BSShaderManager::GetCachedLocalShadowDistanceSquared();
		const bool fades = a_light.field177 && std::isfinite(fadeDistance) && fadeDistance > 0.0f;

		a_slot.type = type;
		a_slot.projection = projection;
		a_slot.params = { static_cast<float>(type), radius, bias * kDepthBias * texelScale, 1.0f };
		a_slot.params2 = { falloff, 0.0f, fades ? fadeDistance : kUnboundedFadeDistanceSquared, 0.0f };
		return CopyJob{ entry.arraySlice, omni ? entries[1].arraySlice : 0u, mapSize, omni };
	}

	bool LocalShadows::Copy(const CopyJob& a_job, std::uint32_t a_slot)
	{
		auto* context = engine::GetImmediateContext();
		auto* source = engine::GetDepthStencilDepthSRV(ShadowTarget());
		if (!context || !source || !_copyShader || !_copyCB)
			return false;
		const CopyCB constants{ a_job.frontSlice, a_job.backSlice, a_slot, a_job.omni ? 1u : 0u,
			_layout.cacheSize, a_job.mapSize, _layout.engineSize, 0 };
		_copyCB->Update(constants);

		// The engine still has this map bound as the depth target.
		engine::ComputeOMScope scope(context, 1, 0, 1, 1);
		render::annotation::ScopedEvent event("LightLimitFix::LocalShadowCopy");
		auto* uav = _cache->uav.get();
		auto* constantBuffer = _copyCB->CB();
		context->CSSetShaderResources(0, 1, &source);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetConstantBuffers(0, 1, &constantBuffer);
		context->CSSetShader(_copyShader.get(), nullptr, 0);
		const auto groups = (_layout.cacheSize + kCopyGroupSize - 1) / kCopyGroupSize;
		context->Dispatch(groups, groups, 1);
		return true;
	}

	void LocalShadows::OnRendered(RE::BSShadowLight& a_light, engine::ShadowLightKind a_kind)
	{
		std::scoped_lock lock(_mutex);
		if (!_ready.load(std::memory_order_acquire))
			return;
		// FO4: focus wrappers render orthographic frustums and are not local lights.
		const auto* entries = a_light.shadowMapData;
		if (a_kind == engine::ShadowLightKind::kFrustum && entries && a_light.shadowMapCount &&
			entries[0].camera && entries[0].camera->viewFrustum.ortho)
			return;
		// A slot is taken only once the light's map provably exists.
		auto index = Find(&a_light);
		const bool known = index < _slots.size();
		if (!known)
			index = Find(nullptr);
		if (index >= _slots.size()) {
			++stats.rejected;
			return;
		}
		Slot slot = known ? _slots[index] : Slot{};
		const auto job = Capture(slot, a_light, a_kind);
		if (!job || !Copy(*job, index)) {
			++stats.rejected;
			return;
		}
		if (!known) {
			slot.owner = &a_light;
			slot.readyFrame = _frame;
		}
		slot.lastCopy = _frame;
		_slots[index] = slot;
		++stats.copies;
		++stats.copyTotal;
	}

	void LocalShadows::Upload()
	{
		std::scoped_lock lock(_mutex);
		if (!_ready.load(std::memory_order_acquire))
			return;
		std::ranges::fill(_upload, LocalShadowData{});
		std::uint32_t cached = 0;
		const double eyeX = _eye.x, eyeY = _eye.y, eyeZ = _eye.z;
		for (std::size_t i = 0; i < _slots.size(); ++i) {
			const auto& slot = _slots[i];
			if (!slot.owner)
				continue;
			auto& out = _upload[i];
			out.shadowProj = slot.projection;
			// Rebase to the eye in double; the stored matrix is absolute world.
			for (int column = 0; column < 4; ++column) {
				out.shadowProj.m[3][column] = static_cast<float>(
					eyeX * slot.projection.m[0][column] + eyeY * slot.projection.m[1][column] +
					eyeZ * slot.projection.m[2][column] + slot.projection.m[3][column]);
			}
			out.params = slot.params;
			if (slot.type != LocalShadowType::kSpot)
				out.params.w = FadeIn(slot);
			out.params2 = slot.params2;
			out.origin = { _eye.x, _eye.y, _eye.z, 0.0f };
			++cached;
		}
		_data->Update(_upload.data(), _upload.size() * sizeof(LocalShadowData));
		stats.cached = cached;
	}
}
