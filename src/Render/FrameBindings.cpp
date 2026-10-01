#include "Render/FrameBindings.h"

#include <d3d11.h>

namespace cs::engine
{
	namespace
	{
		template <class T, std::size_t Count>
		struct Bindings
		{
			std::array<T*, Count> expected{};
			std::bitset<Count> tracked;

			void Record(UINT a_start, UINT a_count, T* const* a_values) noexcept
			{
				for (UINT i = 0; i < a_count; ++i) {
					expected[a_start + i] = a_values[i];
					tracked.set(a_start + i);
				}
			}

			template <class Get>
			void Verify(Get a_get, std::bitset<Count>& a_lost, FrameBindingMetrics& a_metrics) const noexcept
			{
				for (UINT slot = 0; slot < Count; ++slot) {
					if (!tracked.test(slot))
						continue;
					T* actual = nullptr;
					a_get(slot, &actual);
					++a_metrics.checks;
					if (actual != expected[slot]) {
						++a_metrics.lost;
						++a_metrics.lostTotal;
						a_lost.set(slot);
					}
					if (actual)
						actual->Release();
				}
			}
		};

		struct StageBindings
		{
			Bindings<ID3D11ShaderResourceView, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources;
			Bindings<ID3D11Buffer, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers;
			std::bitset<static_cast<std::size_t>(ShaderInjectionTarget::kCount)> sampled;
		};

		thread_local std::array<StageBindings, 3> t_bindings;
		thread_local FrameBindingMetrics t_metrics;
	}

	void ResetFrameBindings() noexcept
	{
		t_bindings = {};
		t_metrics = { .lostTotal = t_metrics.lostTotal };
	}

	FrameBindingMetrics GetFrameBindingMetrics() noexcept { return t_metrics; }

	void BindFrameShaderResources(ID3D11DeviceContext* a_context, ShaderStage a_stage, UINT a_start, UINT a_count, ID3D11ShaderResourceView* const* a_views) noexcept
	{
		switch (a_stage) {
		case ShaderStage::kVertex:
			a_context->VSSetShaderResources(a_start, a_count, a_views);
			break;
		case ShaderStage::kPixel:
			a_context->PSSetShaderResources(a_start, a_count, a_views);
			break;
		case ShaderStage::kCompute:
			a_context->CSSetShaderResources(a_start, a_count, a_views);
			break;
		default:
			return;
		}
		auto& stage = t_bindings[static_cast<std::size_t>(a_stage)];
		stage.resources.Record(a_start, a_count, a_views);
		stage.sampled.reset();
	}

	void BindFrameConstantBuffers(ID3D11DeviceContext* a_context, ShaderStage a_stage, UINT a_start, UINT a_count, ID3D11Buffer* const* a_buffers) noexcept
	{
		switch (a_stage) {
		case ShaderStage::kVertex:
			a_context->VSSetConstantBuffers(a_start, a_count, a_buffers);
			break;
		case ShaderStage::kPixel:
			a_context->PSSetConstantBuffers(a_start, a_count, a_buffers);
			break;
		case ShaderStage::kCompute:
			a_context->CSSetConstantBuffers(a_start, a_count, a_buffers);
			break;
		default:
			return;
		}
		auto& stage = t_bindings[static_cast<std::size_t>(a_stage)];
		stage.buffers.Record(a_start, a_count, a_buffers);
		stage.sampled.reset();
	}

	void VerifyFrameBindings(ID3D11DeviceContext* a_context, ShaderStage a_stage, ShaderInjectionTarget a_target) noexcept
	{
		const auto index = static_cast<std::size_t>(a_stage);
		auto& stage = t_bindings[index];
		const auto target = static_cast<std::size_t>(a_target);
		if (stage.sampled.test(target))
			return;
		stage.sampled.set(target);
		stage.resources.Verify([&](UINT slot, ID3D11ShaderResourceView** value) {
			switch (a_stage) {
			case ShaderStage::kVertex:
				a_context->VSGetShaderResources(slot, 1, value);
				break;
			case ShaderStage::kPixel:
				a_context->PSGetShaderResources(slot, 1, value);
				break;
			case ShaderStage::kCompute:
				a_context->CSGetShaderResources(slot, 1, value);
				break;
			default:
				break;
			}
		},
			t_metrics.resources[index], t_metrics);
		stage.buffers.Verify([&](UINT slot, ID3D11Buffer** value) {
			switch (a_stage) {
			case ShaderStage::kVertex:
				a_context->VSGetConstantBuffers(slot, 1, value);
				break;
			case ShaderStage::kPixel:
				a_context->PSGetConstantBuffers(slot, 1, value);
				break;
			case ShaderStage::kCompute:
				a_context->CSGetConstantBuffers(slot, 1, value);
				break;
			default:
				break;
			}
		},
			t_metrics.buffers[index], t_metrics);
	}
}
