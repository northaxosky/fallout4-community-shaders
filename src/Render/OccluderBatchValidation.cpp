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

		// Same square as the occlusion map the batch is checked against.
		constexpr std::uint32_t kMapSize = 512;
		constexpr std::uint32_t kThreadGroup = 8;

		struct ScratchMap
		{
			winrt::com_ptr<ID3D11Texture2D> texture;
			winrt::com_ptr<ID3D11DepthStencilView> depthView;
			winrt::com_ptr<ID3D11ShaderResourceView> depthSRV;

			void Create(ID3D11Device* a_device, std::string_view a_name)
			{
				const D3D11_TEXTURE2D_DESC textureDesc{
					.Width = kMapSize,
					.Height = kMapSize,
					.MipLevels = 1,
					.ArraySize = 1,
					.Format = DXGI_FORMAT_R16_TYPELESS,
					.SampleDesc = { 1, 0 },
					.Usage = D3D11_USAGE_DEFAULT,
					.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
				};
				const D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{
					.Format = DXGI_FORMAT_D16_UNORM,
					.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D
				};
				const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
					.Format = DXGI_FORMAT_R16_UNORM,
					.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
				};
				DX::ThrowIfFailed(a_device->CreateTexture2D(&textureDesc, nullptr, texture.put()));
				DX::ThrowIfFailed(a_device->CreateDepthStencilView(texture.get(), &dsvDesc, depthView.put()));
				DX::ThrowIfFailed(a_device->CreateShaderResourceView(texture.get(), &srvDesc, depthSRV.put()));
				cs::render::annotation::SetName(texture.get(), a_name);
			}

			[[nodiscard]] OcclusionCaptureTarget Target() const noexcept { return { texture.get(), depthView.get(), depthSRV.get() }; }
		};

		struct Validation
		{
			ScratchMap engine;
			ScratchMap batch;
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
			validation->compare.attach(static_cast<ID3D11ComputeShader*>(
				cs::util::CompileShader(L"Data\\Shaders\\FO4\\OccluderBatchCompareCS.hlsl", {}, "cs_5_0")));
			if (!validation->compare)
				return nullptr;
			validation->extent = std::make_unique<cs::buffer::ConstantBuffer>(cs::buffer::ConstantBufferDesc(16, false));

			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = 8;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, validation->result.put()));
			const D3D11_UNORDERED_ACCESS_VIEW_DESC view{
				.Format = DXGI_FORMAT_R32_TYPELESS,
				.ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
				.Buffer = { .FirstElement = 0, .NumElements = 2, .Flags = D3D11_BUFFER_UAV_FLAG_RAW }
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

		const UINT clear[4]{};
		context->ClearUnorderedAccessViewUint(validation.resultView.get(), clear);
		const std::uint32_t extent[4]{ a_extent, a_extent, 0, 0 };
		context->UpdateSubresource(validation.extent->CB(), 0, nullptr, extent, 0, 0);
		{
			const cs::render::ScopedContextState state{ context, validation.state.get() };
			if (!state.IsActive())
				return {};
			ID3D11ShaderResourceView* views[]{ validation.engine.depthSRV.get(), validation.batch.depthSRV.get() };
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
		const BatchValidationResult result{ .ok = true, .mismatchedTexels = values[0], .maxDelta = values[1] };
		context->Unmap(validation.readback.get(), 0);
		return result;
	}
}
