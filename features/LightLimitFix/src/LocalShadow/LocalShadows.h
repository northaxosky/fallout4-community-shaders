#pragma once

#include "LightData.h"
#include "LightLimitFixSettings.h"
#include "Render/RenderHooks.h"
#include "Utils/CSBuffer.h"

#include <RE/B/BSShadowLight.h>

#include <DirectXMath.h>
#include <winrt/base.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace cs::features::light_limit_fix
{
	// FO4: the engine reuses one depth array per light, so each map is copied out.
	class LocalShadows
	{
	public:
		// Throws when the copy shader cannot be compiled.
		void Create();
		// Prepass: applies settings, frees stale slots and rebases to the eye.
		void Begin(const LocalShadowConfig& a_config, const DirectX::XMFLOAT4& a_eye);
		// Gather: requests the light's copy and flags it once cached; assigns no slot.
		void Resolve(RE::BSShadowLight& a_light, LightData& a_data);
		// Right after the light's Render, while its map is still bound.
		void OnRendered(RE::BSShadowLight& a_light, engine::ShadowLightKind a_kind);
		// After every light rendered, before forward draws.
		void Upload();
		void Release(const RE::BSLight& a_light);
		void Clear();

		bool Ready() const noexcept { return _ready.load(std::memory_order_acquire); }
		std::uint32_t Samples() const noexcept { return _samples.load(std::memory_order_acquire); }
		float TexelSize() const noexcept { return _texelSize.load(std::memory_order_acquire); }
		float FilterRadius() const noexcept { return _filterRadius.load(std::memory_order_acquire); }
		ID3D11ShaderResourceView* DataView() const noexcept;
		ID3D11ShaderResourceView* CacheView() const noexcept;

		struct Stats
		{
			std::atomic<std::uint32_t> slots{ 0 }, cached{ 0 }, copies{ 0 }, resolution{ 0 };
			std::atomic<std::uint64_t> copyTotal{ 0 }, rejected{ 0 };
		};
		Stats stats;

	private:
		struct Slot
		{
			const RE::BSLight* owner = nullptr;
			std::uint32_t lastCopy = 0;
			std::uint32_t readyFrame = 0;
			LocalShadowType type = LocalShadowType::kSpot;
			// Absolute world, row-vector convention.
			DirectX::XMFLOAT4X4 projection{};
			DirectX::XMFLOAT4 params{};
			DirectX::XMFLOAT4 params2{};
		};

		struct Layout
		{
			std::uint32_t slots = 0;
			std::uint32_t cacheSize = 0;
			std::uint32_t engineSize = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			bool operator==(const Layout&) const = default;
		};

		struct CopyJob
		{
			std::uint32_t frontSlice, backSlice, mapSize;
			bool omni;
		};

		void Ensure(const LocalShadowConfig& a_config);
		void ReleaseResources();
		std::uint32_t Find(const RE::BSLight* a_owner) const;
		float FadeIn(const Slot& a_slot) const noexcept;
		std::optional<CopyJob> Capture(Slot& a_slot, RE::BSShadowLight& a_light, engine::ShadowLightKind a_kind);
		bool Copy(const CopyJob& a_job, std::uint32_t a_slot);

		std::mutex _mutex;
		std::atomic<bool> _ready{ false };
		std::atomic<std::uint32_t> _samples{ 0 };
		std::atomic<float> _texelSize{ 0 }, _filterRadius{ 0 };
		winrt::com_ptr<ID3D11ComputeShader> _copyShader;
		std::unique_ptr<buffer::ConstantBuffer> _copyCB;
		std::unique_ptr<buffer::Texture2D> _cache;
		std::unique_ptr<buffer::StructuredBuffer> _data;
		std::vector<Slot> _slots;
		std::vector<LocalShadowData> _upload;
		std::vector<const RE::BSLight*> _wanted;
		Layout _layout;
		// Last layout requested; a failed allocation is not retried.
		Layout _requested;
		std::uint32_t _frame = 0;
		DirectX::XMFLOAT4 _eye{};
	};
}
