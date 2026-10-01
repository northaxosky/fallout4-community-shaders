#include "Render/CanonicalDepth.h"

#include "Render/Annotation.h"
#include "Render/Engine.h"
#include "Render/PixelShaderSwapBroker.h"
#include "Render/RenderExtents.h"
#include "Render/RendererContext.h"
#include "Render/SharedData.h"
#include "Utils/CSBuffer.h"
#include "Utils/CSUtil.h"

#include <REX/TScopeExit.h>
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

namespace cs::render
{
	namespace
	{
		struct alignas(16) CanonicalDepthData
		{
			DirectX::XMFLOAT4X4 nearInverse{}, worldProjection{};
			std::uint32_t allocation[2]{}, active[2]{};
		};
		std::unique_ptr<buffer::Texture2D> depth;
		std::unique_ptr<buffer::ConstantBuffer> constants;
		winrt::com_ptr<ID3D11ComputeShader> shader;
		std::uint32_t publishedFrame = UINT32_MAX;

		void Resize(std::uint32_t a_width, std::uint32_t a_height)
		{
			if (depth && depth->desc.Width == a_width && depth->desc.Height == a_height)
				return;
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			auto texture = std::make_unique<buffer::Texture2D>(desc);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = desc.Format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			texture->CreateSRV(srv);
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = desc.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			texture->CreateUAV(uav);
			texture->SetName("Render/CanonicalDepth", "Render/CanonicalDepth.SRV", "Render/CanonicalDepth.UAV");
			depth = std::move(texture);
		}
	}

	void InitializeCanonicalDepth(ID3D11Device* a_device)
	{
		if (!a_device)
			return;
		shader.attach(static_cast<ID3D11ComputeShader*>(util::CompileShader(
			L"Data\\Shaders\\FO4\\CanonicalDepthCS.hlsl", {}, "cs_5_0")));
		if (!shader)
			throw std::runtime_error("Canonical depth shader compilation failed.");
		constants = std::make_unique<buffer::ConstantBuffer>(buffer::ConstantBufferDesc<CanonicalDepthData>());
		constants->SetName("Render/CanonicalDepth.Constants");
	}

	void UpdateCanonicalDepth(ID3D11DeviceContext* a_context, const engine::WorldCameraRecord& a_camera)
	{
		if (publishedFrame == a_camera.frameCount)
			return;
		publishedFrame = UINT32_MAX;
		auto* source = engine::GetSceneDepthSRV();
		const auto nearInverse = engine::GetPrepassFirstPersonProjectionInverse();
		if (!a_context || !shader || !constants || !source || !nearInverse)
			return;
		winrt::com_ptr<ID3D11Resource> resource;
		source->GetResource(resource.put());
		const auto texture = resource.try_as<ID3D11Texture2D>();
		if (!texture)
			return;
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		const auto* state = engine::GetGraphicsState();
		if (desc.SampleDesc.Count != 1 || !state)
			return;
		const auto active = GetActiveExtent(state->screenWidth, state->screenHeight);
		if (!active.width || !active.height)
			return;
		Resize(desc.Width, desc.Height);
		CanonicalDepthData data{};
		data.nearInverse = *nearInverse;
		// The jittered world projection rasterizes the g-buffer.
		DirectX::XMStoreFloat4x4(&data.worldProjection, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&a_camera.Projection)));
		data.allocation[0] = desc.Width;
		data.allocation[1] = desc.Height;
		data.active[0] = (std::min)(desc.Width, active.width);
		data.active[1] = (std::min)(desc.Height, active.height);
		constants->Update(data);
		annotation::ScopedEvent annotationScope("Render/CanonicalDepth");
		constexpr engine::ShaderStage stages[]{ engine::ShaderStage::kVertex, engine::ShaderStage::kPixel, engine::ShaderStage::kCompute };
		std::array<SubstrateBindingSnapshot, std::size(stages)> bindings;
		for (std::size_t index = 0; index < std::size(stages); ++index)
			bindings[index].Save(a_context, stages[index]);
		REX::TScopeExit restore([&] {
			for (std::size_t index = 0; index < std::size(stages); ++index)
				bindings[index].Restore(a_context, stages[index]);
		});
		ID3D11ShaderResourceView* absent = nullptr;
		a_context->VSSetShaderResources(kCanonicalDepthSlot, 1, &absent);
		a_context->PSSetShaderResources(kCanonicalDepthSlot, 1, &absent);
		a_context->CSSetShaderResources(kCanonicalDepthSlot, 1, &absent);
		{
			engine::ComputeOMScope scope(a_context);
			auto* uav = depth->uav.get();
			auto* cb = constants->CB();
			a_context->CSSetShaderResources(0, 1, &source);
			a_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			a_context->CSSetConstantBuffers(0, 1, &cb);
			a_context->CSSetShader(shader.get(), nullptr, 0);
			a_context->Dispatch((desc.Width + 7) / 8, (desc.Height + 7) / 8, 1);
		}
		publishedFrame = a_camera.frameCount;
	}

	ID3D11ShaderResourceView* GetCanonicalSceneDepthSRV() noexcept
	{
		const auto* state = engine::GetGraphicsState();
		return state && publishedFrame == state->frameCount && depth ? depth->srv.get() : nullptr;
	}
}
