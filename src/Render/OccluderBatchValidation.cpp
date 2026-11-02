#include "Render/OccluderBatch.h"

#include "Log.h"
#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/ScopedContextState.h"
#include "Utils/CSBuffer.h"
#include "Utils/CSUtil.h"

#include <d3d11.h>

#include <array>
#include <memory>

#include <winrt/base.h>

namespace cs::engine
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.skylighting.capture");

		constexpr std::uint32_t kThreadGroup = 8;
		constexpr std::uint32_t kResultWords = 8;

		struct Validation
		{
			OcclusionDepthMap engine;
			OcclusionDepthMap batch;
			winrt::com_ptr<ID3DDeviceContextState> state;
			winrt::com_ptr<ID3D11ComputeShader> compare;
			std::unique_ptr<cs::buffer::ConstantBuffer> extent;
			winrt::com_ptr<ID3D11Buffer> result;
			winrt::com_ptr<ID3D11UnorderedAccessView> resultView;
			winrt::com_ptr<ID3D11Buffer> readback;
		};

		std::unique_ptr<Validation> g_validation;
		bool g_failed = false;

		[[nodiscard]] std::unique_ptr<Validation> Create()
		{
			auto* device = GetDevice();
			if (!device)
				return nullptr;
			auto validation = std::make_unique<Validation>();
			validation->engine.Create(device, "Skylighting/OccluderBatch.Validate.Engine");
			validation->batch.Create(device, "Skylighting/OccluderBatch.Validate.Batch");
			validation->state = cs::render::CreateContextState(device);
			validation->compare = cs::util::CompileComputeShader(L"Data\\Shaders\\FO4\\OccluderBatchCompareCS.hlsl");
			if (!validation->compare)
				return nullptr;
			validation->extent = std::make_unique<cs::buffer::ConstantBuffer>(cs::buffer::ConstantBufferDesc(16, false));

			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(std::uint32_t) * kResultWords;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, validation->result.put()));
			const D3D11_UNORDERED_ACCESS_VIEW_DESC view{
				.Format = DXGI_FORMAT_R32_TYPELESS,
				.ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
				.Buffer = { .FirstElement = 0, .NumElements = kResultWords, .Flags = D3D11_BUFFER_UAV_FLAG_RAW }
			};
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(validation->result.get(), &view, validation->resultView.put()));
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.MiscFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, validation->readback.put()));
			return validation;
		}

		[[nodiscard]] bool Ensure()
		{
			if (g_failed)
				return false;
			if (!g_validation) {
				try {
					g_validation = Create();
				} catch (const std::exception& error) {
					L->error("occluder batch validation resources failed: {}", error.what());
				}
				if (!g_validation) {
					g_failed = true;
					return false;
				}
			}
			return true;
		}
	}

	bool AcquireValidationTargets(BatchValidationTargets& a_targets)
	{
		if (!Ensure())
			return false;
		a_targets.engine = g_validation->engine.Target();
		a_targets.batch = g_validation->batch.Target();
		return true;
	}

	BatchValidationResult CompareValidationTargets(std::uint32_t a_extent)
	{
		auto* context = GetImmediateContext();
		if (!g_validation || !context)
			return {};
		auto& validation = *g_validation;

		// The bounds start inverted so the first mismatch sets them.
		constexpr std::uint32_t initial[kResultWords]{ 0, 0, 0, 0, ~0u, ~0u, 0, 0 };
		context->UpdateSubresource(validation.result.get(), 0, nullptr, initial, 0, 0);
		const std::uint32_t extent[4]{ a_extent, a_extent, 0, 0 };
		validation.extent->Update(extent, sizeof(extent));
		{
			const cs::render::ScopedContextState state{ context, validation.state.get() };
			if (!state.IsActive())
				return {};
			ID3D11ShaderResourceView* views[]{ validation.engine.Target().depthSRV, validation.batch.Target().depthSRV };
			ID3D11UnorderedAccessView* uavs[]{ validation.resultView.get() };
			ID3D11Buffer* constants[]{ validation.extent->CB() };
			context->CSSetShader(validation.compare.get(), nullptr, 0);
			context->CSSetShaderResources(0, 2, views);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetConstantBuffers(0, 1, constants);
			const auto groups = (a_extent + kThreadGroup - 1) / kThreadGroup;
			context->Dispatch(groups, groups, 1);
		}
		context->CopyResource(validation.readback.get(), validation.result.get());

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(validation.readback.get(), 0, D3D11_MAP_READ, 0, &mapped)))
			return {};
		const auto* values = static_cast<const std::uint32_t*>(mapped.pData);
		const BatchValidationResult result{
			.ok = true,
			.mismatchedTexels = values[0],
			.maxDelta = values[1],
			.batchNearer = values[2],
			.engineNearer = values[3],
			.bounds = { values[4], values[5], values[6], values[7] }
		};
		context->Unmap(validation.readback.get(), 0);
		return result;
	}
}
