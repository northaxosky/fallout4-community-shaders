#pragma once

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
	// reserved on every contributed stage
	inline constexpr std::uint32_t kSharedDataSlot = 5;
	inline constexpr std::uint32_t kFeatureDataSlot = 6;
	static_assert(kFeatureDataSlot == kSharedDataSlot + 1);

	void InitializeSharedData(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
	bool IsSharedDataReady() noexcept;

	// startup thread only
	void EnsureSharedDataUpdateInstalled();

	void BindSharedData(
		ID3D11DeviceContext* a_context,
		engine::ShaderStage a_stage) noexcept;

	// Preserve only b5-b6 around one engine dispatch.
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
		std::array<winrt::com_ptr<ID3D11Buffer>, 2> _buffers;
		bool _active = false;
	};

	[[nodiscard]] bool IsDeferredLightsActive() noexcept;
}
