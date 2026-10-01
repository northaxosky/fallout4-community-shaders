#pragma once
#include "Render/SubstrateSlots.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <array>
#include <cstdint>

struct ID3D11Device;

namespace cs::engine
{
	enum class ShaderStage : std::uint8_t;
}

namespace cs::render
{
	void InitializeSharedData(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
	bool IsSharedDataReady() noexcept;
	bool IsSharedDataCurrent() noexcept;
	// Debug producers invalidate the cached packet when replacing or updating its texture.
	void InvalidateFullscreenDebugData() noexcept;

	// startup thread only
	void EnsureSharedDataUpdateInstalled();

	void BindSharedData(
		ID3D11DeviceContext* a_context,
		engine::ShaderStage a_stage) noexcept;

	class SubstrateBindingSnapshot
	{
	public:
		void Save(ID3D11DeviceContext* a_context, engine::ShaderStage a_stage) noexcept;
		void Restore(ID3D11DeviceContext* a_context, engine::ShaderStage a_stage) noexcept;

	private:
		std::array<winrt::com_ptr<ID3D11Buffer>, kSubstrateBufferCount> _buffers;
		winrt::com_ptr<ID3D11ShaderResourceView> _depth;
		winrt::com_ptr<ID3D11ShaderResourceView> _debugTexture;
	};

	// Preserve the substrate around one engine dispatch.
	class ScopedComputeSharedDataBinding
	{
	public:
		explicit ScopedComputeSharedDataBinding(
			ID3D11DeviceContext* a_context) noexcept;
		~ScopedComputeSharedDataBinding() noexcept;

		ScopedComputeSharedDataBinding(
			const ScopedComputeSharedDataBinding&) = delete;
		ScopedComputeSharedDataBinding(
			ScopedComputeSharedDataBinding&&) = delete;
		ScopedComputeSharedDataBinding& operator=(
			const ScopedComputeSharedDataBinding&) = delete;
		ScopedComputeSharedDataBinding& operator=(
			ScopedComputeSharedDataBinding&&) = delete;

		[[nodiscard]] bool IsActive() const noexcept { return _active; }

	private:
		ID3D11DeviceContext* _context = nullptr;
		SubstrateBindingSnapshot _bindings;
		bool _active = false;
	};

	[[nodiscard]] bool IsDeferredLightsActive() noexcept;
}
