#include "Utils/ShaderCompile.h"

#include <d3d11.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>

std::string CheckInverseSquareTileOverflow(const std::filesystem::path& a_root)
{
	using Microsoft::WRL::ComPtr;
	const auto check = [](HRESULT result) {
		if (FAILED(result))
			throw std::runtime_error("ISL overflow fixture D3D11 operation failed");
	};
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		const auto buffer = [&](UINT bytes, UINT stride, UINT bind, const void* data) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = bytes;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = bind;
			desc.MiscFlags = stride ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED : 0;
			desc.StructureByteStride = stride;
			D3D11_SUBRESOURCE_DATA initial{ data, 0, 0 };
			ComPtr<ID3D11Buffer> result;
			check(device->CreateBuffer(&desc, data ? &initial : nullptr, result.GetAddressOf()));
			return result;
		};
		std::array<std::array<float, 12>, 625> lights{};
		for (auto& light : lights)
			light[4] = 10000.0f;
		const std::array<float, 2> bounds{ 0, 100 };
		const std::array<float, 16> constants{
			0, 100, 0, 0, -1, -1, 1, 1, 625, 0, 1, 1, 0, 0, 2, -2
		};
		std::array<std::uint32_t, 256> lists;
		lists.fill(0xDEADBEEF);
		auto lightBuffer = buffer(sizeof(lights), 48, D3D11_BIND_SHADER_RESOURCE, lights.data());
		auto depthBuffer = buffer(sizeof(bounds), 8, D3D11_BIND_SHADER_RESOURCE, bounds.data());
		auto listBuffer = buffer(sizeof(lists), 512, D3D11_BIND_UNORDERED_ACCESS, lists.data());
		auto params = buffer(sizeof(constants), 0, D3D11_BIND_CONSTANT_BUFFER, constants.data());
		ComPtr<ID3D11ShaderResourceView> lightView, depthView;
		check(device->CreateShaderResourceView(lightBuffer.Get(), nullptr, lightView.GetAddressOf()));
		check(device->CreateShaderResourceView(depthBuffer.Get(), nullptr, depthView.GetAddressOf()));
		ComPtr<ID3D11UnorderedAccessView> listView;
		check(device->CreateUnorderedAccessView(listBuffer.Get(), nullptr, listView.GetAddressOf()));
		std::string error;
		const auto path = a_root / "DFTiledLighting.hlsl";
		auto blob = cs::util::CompileShaderToBlob(path.c_str(),
			{ { "DFTILEDLIGHTING_VARIANT", "3" }, { "DFTILEDLIGHTING_TILE_CULL_GROUP_DIM", "25" },
				{ "INVERSE_SQUARE_LIGHTING", "1" } },
			"cs_5_0", "main", &error, a_root);
		if (!blob)
			return error;
		ComPtr<ID3D11ComputeShader> shader;
		check(device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
			nullptr, shader.GetAddressOf()));
		context->CSSetShader(shader.Get(), nullptr, 0);
		context->CSSetConstantBuffers(0, 1, params.GetAddressOf());
		context->CSSetShaderResources(5, 1, depthView.GetAddressOf());
		context->CSSetShaderResources(6, 1, lightView.GetAddressOf());
		context->CSSetUnorderedAccessViews(7, 1, listView.GetAddressOf(), nullptr);
		context->Dispatch(1, 1, 1);
		ID3D11UnorderedAccessView* empty = nullptr;
		context->CSSetUnorderedAccessViews(7, 1, &empty, nullptr);
		D3D11_BUFFER_DESC readback{};
		readback.ByteWidth = sizeof(lists);
		readback.Usage = D3D11_USAGE_STAGING;
		readback.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Buffer> staging;
		check(device->CreateBuffer(&readback, nullptr, staging.GetAddressOf()));
		context->CopyResource(staging.Get(), listBuffer.Get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
		std::memcpy(lists.data(), mapped.pData, sizeof(lists));
		context->Unmap(staging.Get(), 0);
		if (lists[0] != 625)
			return "Overflow fixture did not admit all 625 expanded-radius lights";
		std::set<std::uint32_t> indices;
		for (std::size_t i = 1; i < 128; ++i)
			if (lists[i] >= 625 || !indices.insert(lists[i]).second)
				return "Guarded tile contains an invalid or duplicate index";
		for (std::size_t i = 128; i < lists.size(); ++i)
			if (lists[i] != 0xDEADBEEF)
				return "Expanded-radius tile append overwrote the adjacent tile";
		return {};
	} catch (const std::exception& error) {
		return error.what();
	}
}
