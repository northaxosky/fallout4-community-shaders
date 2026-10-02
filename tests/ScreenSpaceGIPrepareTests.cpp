#include "ScreenSpaceGIConstants.h"
#include "Utils/ShaderCompile.h"

#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

// Also callable from ShaderCompileTests; standalone builds define SSGI_PREPARE_TEST_MAIN.
std::string CheckScreenSpaceGIPrepare(const std::filesystem::path& a_root)
{
	using Microsoft::WRL::ComPtr;
	const auto check = [](HRESULT a_result) {
		if (FAILED(a_result))
			throw std::runtime_error("SSGI Prepare fixture D3D11 operation failed");
	};
	try {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
			D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
		struct Texture
		{
			ComPtr<ID3D11Texture2D> resource;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11UnorderedAccessView> uav;
		};
		constexpr UINT width = 37, height = 29;
		const auto texture = [&](DXGI_FORMAT a_format, UINT a_width, UINT a_height,
								 const void* a_data = nullptr, UINT a_pitch = 0) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = a_format;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			D3D11_SUBRESOURCE_DATA initial{ a_data, a_pitch, 0 };
			Texture result;
			check(device->CreateTexture2D(&desc, a_data ? &initial : nullptr, result.resource.GetAddressOf()));
			check(device->CreateShaderResourceView(result.resource.Get(), nullptr, result.srv.GetAddressOf()));
			check(device->CreateUnorderedAccessView(result.resource.Get(), nullptr, result.uav.GetAddressOf()));
			return result;
		};
		const auto buffer = [&](const void* a_data, UINT a_bytes) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = a_bytes;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_SUBRESOURCE_DATA initial{ a_data, 0, 0 };
			ComPtr<ID3D11Buffer> result;
			check(device->CreateBuffer(&desc, &initial, result.GetAddressOf()));
			return result;
		};
		const auto read = [&](const Texture& a_texture, UINT a_pixelBytes) {
			D3D11_TEXTURE2D_DESC desc{};
			a_texture.resource->GetDesc(&desc);
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Texture2D> staging;
			check(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()));
			context->CopyResource(staging.Get(), a_texture.resource.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
			std::vector<std::byte> bytes(desc.Width * desc.Height * a_pixelBytes);
			for (UINT y = 0; y < desc.Height; ++y)
				std::memcpy(bytes.data() + y * desc.Width * a_pixelBytes,
					static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch, desc.Width * a_pixelBytes);
			context->Unmap(staging.Get(), 0);
			return bytes;
		};
		using Defines = std::vector<std::pair<const char*, const char*>>;
		const auto compile = [&](const std::filesystem::path& a_path, const Defines& a_defines) {
			std::string error;
			auto blob = cs::util::CompileShaderToBlob(a_path.c_str(), a_defines, "cs_5_0", "main", &error, a_root);
			if (!blob)
				throw std::runtime_error(error);
			return blob;
		};
		const auto shader = [&](ID3DBlob* a_blob) {
			ComPtr<ID3D11ComputeShader> result;
			check(device->CreateComputeShader(a_blob->GetBufferPointer(), a_blob->GetBufferSize(), nullptr, result.GetAddressOf()));
			return result;
		};
		const auto dispatch = [&](ID3D11ComputeShader* a_shader, const auto& a_srvs, const auto& a_uavs, UINT a_width, UINT a_height) {
			context->CSSetShader(a_shader, nullptr, 0);
			context->CSSetShaderResources(0, static_cast<UINT>(a_srvs.size()), a_srvs.data());
			context->CSSetUnorderedAccessViews(0, static_cast<UINT>(a_uavs.size()), a_uavs.data(), nullptr);
			context->Dispatch((a_width + 7) / 8, (a_height + 7) / 8, 1);
			const std::array<ID3D11ShaderResourceView*, 6> nullSRVs{};
			const std::array<ID3D11UnorderedAccessView*, 2> nullUAVs{};
			context->CSSetShaderResources(0, 6, nullSRVs.data());
			context->CSSetUnorderedAccessViews(0, 2, nullUAVs.data(), nullptr);
		};
		std::array<std::array<float, 4>, width * height> input{};
		for (UINT i = 0; i < input.size(); ++i)
			input[i] = { (i % 17 + 1) / 19.0f, (i % 23 + 1) / 25.0f, (i % 31 + 1) / 33.0f, 0 };
		auto source = texture(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, input.data(), width * 16);
		const std::array<ID3D11ShaderResourceView*, 6> inputs{
			source.srv.Get(), source.srv.Get(), source.srv.Get(), source.srv.Get(), source.srv.Get(), source.srv.Get()
		};
		const std::array<UINT, 4> consumer{ 1, 1, 0, 0 };
		auto consumerCB = buffer(consumer.data(), sizeof(consumer));
		context->CSSetConstantBuffers(10, 1, consumerCB.GetAddressOf());
		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		ComPtr<ID3D11SamplerState> sampler;
		check(device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf()));
		context->CSSetSamplers(1, 1, sampler.GetAddressOf());
		const auto preparePath = a_root / "FO4/ScreenSpaceGI/Prepare.cs.hlsl";
		const auto probePath = a_root.parent_path().parent_path().parent_path() / "tests/shaders/ScreenSpaceGIPrepareProbe.hlsl";
		auto referenceShader = shader(compile(preparePath, { { "GI", "1" }, { "GI_SPECULAR", "1" } }).Get());
		auto referenceNormal = texture(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height);
		auto referenceDiffuse = texture(DXGI_FORMAT_R11G11B10_FLOAT, width, height);
		const std::array<ID3D11UnorderedAccessView*, 2> referenceOutputs{ referenceNormal.uav.Get(), referenceDiffuse.uav.Get() };
		const float poison[]{ 0.125f, 0.25f, 0.5f, 0 };
		for (const auto frame : { std::array<UINT, 2>{ width, height }, std::array<UINT, 2>{ 31, 23 } }) {
			cs::features::ssgi::Constants constants{};
			constants.TexDim = { width, height };
			constants.RcpTexDim = { 1.0f / width, 1.0f / height };
			constants.FrameDim = { static_cast<float>(frame[0]), static_cast<float>(frame[1]) };
			constants.RcpFrameDim = { 1.0f / frame[0], 1.0f / frame[1] };
			auto constantsCB = buffer(&constants, sizeof(constants));
			context->CSSetConstantBuffers(1, 1, constantsCB.GetAddressOf());
			context->ClearUnorderedAccessViewFloat(referenceDiffuse.uav.Get(), poison);
			dispatch(referenceShader.Get(), inputs, referenceOutputs, frame[0], frame[1]);
			const auto expectedNormal = read(referenceNormal, 8);
			for (UINT mode = 0; mode < 3; ++mode) {
				Defines resolution;
				if (mode)
					resolution.emplace_back(mode == 1 ? "HALF_RES" : "QUARTER_RES", "1");
				auto probe = shader(compile(probePath, resolution).Get());
				const UINT internalWidth = frame[0] >> mode, internalHeight = frame[1] >> mode;
				auto difference = texture(DXGI_FORMAT_R32G32B32A32_FLOAT,
					(internalWidth + 7) / 8 * 8, (internalHeight + 7) / 8 * 8);
				for (const bool gi : { false, true }) {
					for (const bool specular : { false, true }) {
						auto defines = resolution;
						if (gi)
							defines.emplace_back("GI", "1");
						if (specular)
							defines.emplace_back("GI_SPECULAR", "1");
						auto blob = compile(preparePath, defines);
						ComPtr<ID3D11ShaderReflection> reflection;
						check(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&reflection)));
						D3D11_SHADER_INPUT_BIND_DESC binding{};
						if ((!gi && SUCCEEDED(reflection->GetResourceBindingDescByName("ShadedDiffuse", &binding))) ||
							(!specular && SUCCEEDED(reflection->GetResourceBindingDescByName("Material", &binding))))
							return "Disabled Prepare channels still access their resources";
						auto preparedShader = shader(blob.Get());
						auto normal = texture(specular ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R16G16_FLOAT, width, height);
						auto diffuse = texture(DXGI_FORMAT_R11G11B10_FLOAT, width, height);
						context->ClearUnorderedAccessViewFloat(diffuse.uav.Get(), poison);
						const std::array<ID3D11UnorderedAccessView*, 2> outputs{ normal.uav.Get(), gi ? diffuse.uav.Get() : nullptr };
						dispatch(preparedShader.Get(), inputs, outputs, frame[0], frame[1]);
						const UINT stride = specular ? 8 : 4;
						const auto actualNormal = read(normal, stride);
						for (UINT y = 0; y < frame[1]; ++y)
							for (UINT x = 0; x < frame[0]; ++x)
								if (std::memcmp(actualNormal.data() + (y * width + x) * stride,
										expectedNormal.data() + (y * width + x) * 8, stride))
									return "Prepare changed normal/gloss precision or active full-resolution coverage";
						if (gi) {
							const std::array<ID3D11ShaderResourceView*, 2> compareInputs{ referenceDiffuse.srv.Get(), diffuse.srv.Get() };
							const std::array<ID3D11UnorderedAccessView*, 1> compareOutputs{ difference.uav.Get() };
							dispatch(probe.Get(), compareInputs, compareOutputs, internalWidth, internalHeight);
							const auto bytes = read(difference, 16);
							for (std::size_t i = 0; i < bytes.size(); i += sizeof(float)) {
								float value;
								std::memcpy(&value, bytes.data() + i, sizeof(value));
								if (value != 0)
									return "Prepare changed an upstream FULLRES_LOAD tap, including dispatch padding/texture clamp";
							}
						}
					}
				}
			}
		}
		return {};
	} catch (const std::exception& error) {
		return error.what();
	}
}

#ifdef SSGI_PREPARE_TEST_MAIN
int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	const auto error = CheckScreenSpaceGIPrepare(std::filesystem::path(argv[1]));
	std::cout << (error.empty() ? "SSGI Prepare WARP checks passed" : error) << '\n';
	return error.empty() ? 0 : 1;
}
#endif
