#include "Render/SharedDataLayout.h"
#include "ScreenSpaceGIConstants.h"
#include "Utils/ShaderCompile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

std::string CheckInverseSquareTileOverflow(const std::filesystem::path& a_root);
std::string CheckScreenSpaceGIPrepare(const std::filesystem::path& a_root);

namespace
{
	using ShaderDefines =
		std::vector<std::pair<std::string, std::string>>;

	enum class ResourceKind
	{
		kConstantBuffer,
		kTexture,
		kStructuredBuffer,
		kSampler
	};

	struct Resource
	{
		ResourceKind kind;
		UINT slot;

		auto operator<=>(const Resource&) const = default;
	};

	struct ShaderJob
	{
		std::filesystem::path path;
		ShaderDefines defines;
		const char* profile = "cs_5_0";
		const char* entryPoint = "main";
		const char* description = "";
		std::vector<Resource> required;
		std::vector<Resource> forbidden;
	};

	constexpr Resource CB(UINT a_slot)
	{
		return { ResourceKind::kConstantBuffer, a_slot };
	}

	constexpr Resource Texture(UINT a_slot)
	{
		return { ResourceKind::kTexture, a_slot };
	}

	constexpr Resource Sampler(UINT a_slot)
	{
		return { ResourceKind::kSampler, a_slot };
	}

	constexpr Resource StructuredBuffer(UINT a_slot)
	{
		return { ResourceKind::kStructuredBuffer, a_slot };
	}

	void AddFullscreenDebugVariant(std::vector<ShaderJob>& a_jobs,
		const ShaderJob& a_production, const char* a_define,
		std::initializer_list<Resource> a_required)
	{
		auto debug = a_production;
		debug.defines.emplace_back(a_define, "1");
		debug.description = a_define;
		for (const auto resource : a_required) {
			std::erase(debug.forbidden, resource);
			debug.required.push_back(resource);
		}
		a_jobs.push_back(std::move(debug));
	}

	std::set<Resource> ReflectResources(ID3DBlob* a_blob, std::string& a_error)
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(
				a_blob->GetBufferPointer(),
				a_blob->GetBufferSize(),
				__uuidof(ID3D11ShaderReflection),
				reinterpret_cast<void**>(reflection.GetAddressOf())))) {
			a_error = "D3DReflect failed";
			return {};
		}

		D3D11_SHADER_DESC shaderDesc{};
		if (FAILED(reflection->GetDesc(&shaderDesc))) {
			a_error = "shader reflection description failed";
			return {};
		}

		std::set<Resource> resources;
		for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDesc(index, &binding)))
				continue;

			std::optional<ResourceKind> kind;
			if (binding.Type == D3D_SIT_CBUFFER)
				kind = ResourceKind::kConstantBuffer;
			else if (binding.Type == D3D_SIT_TEXTURE)
				kind = ResourceKind::kTexture;
			else if (binding.Type == D3D_SIT_STRUCTURED)
				kind = ResourceKind::kStructuredBuffer;
			else if (binding.Type == D3D_SIT_SAMPLER)
				kind = ResourceKind::kSampler;
			if (!kind)
				continue;

			for (UINT slot = 0;
				slot < (std::max)(binding.BindCount, 1U);
				++slot) {
				resources.insert({ *kind, binding.BindPoint + slot });
			}
		}
		return resources;
	}

	const char* ResourceName(ResourceKind a_kind)
	{
		switch (a_kind) {
		case ResourceKind::kConstantBuffer:
			return "b";
		case ResourceKind::kTexture:
		case ResourceKind::kStructuredBuffer:
			return "t";
		case ResourceKind::kSampler:
			return "s";
		}
		return "?";
	}

	std::string Compile(const ShaderJob& a_job, const std::filesystem::path& a_shaderRoot)
	{
		std::vector<std::pair<const char*, const char*>> defines;
		defines.reserve(a_job.defines.size());
		for (const auto& [name, value] : a_job.defines)
			defines.emplace_back(name.c_str(), value.c_str());

		std::string error;
		auto blob = cs::util::CompileShaderToBlob(
			a_job.path.c_str(),
			defines,
			a_job.profile,
			a_job.entryPoint,
			&error,
			a_shaderRoot);
		if (!blob)
			return error;

		error.clear();
		const auto resources = ReflectResources(blob.Get(), error);
		if (!error.empty())
			return error;
		for (const auto resource : a_job.required) {
			if (!resources.contains(resource)) {
				return "missing reflected " + std::string(ResourceName(resource.kind)) + std::to_string(resource.slot);
			}
		}
		for (const auto resource : a_job.forbidden) {
			if (resources.contains(resource)) {
				return "unexpected reflected " + std::string(ResourceName(resource.kind)) + std::to_string(resource.slot);
			}
		}
		return {};
	}

	std::string VerifySSSReceiverGate(const std::filesystem::path& a_root)
	{
		try {
			const auto checked = [](HRESULT a_result) {
				if (FAILED(a_result))
					throw std::runtime_error("D3D11 SSS receiver-gate fixture failed");
			};
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
				D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
			std::string error;
			constexpr std::array partitioned{ 0.0f, 0.005f, 0.01f, 0.01001f, 0.5f, 1.0f };
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = static_cast<UINT>(partitioned.size());
			desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			const auto input = [&](const auto& a_values) {
				D3D11_SUBRESOURCE_DATA data{};
				data.pSysMem = a_values.data();
				data.SysMemPitch = static_cast<UINT>(sizeof(a_values));
				Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
				checked(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()));
				Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
				checked(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));
				return view;
			};
			const auto raw = input(partitioned);
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> output;
			checked(device->CreateTexture2D(&desc, nullptr, output.GetAddressOf()));
			Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
			checked(device->CreateUnorderedAccessView(output.Get(), nullptr, uav.GetAddressOf()));
			auto* rawView = raw.Get();
			auto* outputView = uav.Get();
			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
			checked(device->CreateTexture2D(&desc, nullptr, readback.GetAddressOf()));
			const auto read = [&] {
				context->CopyResource(readback.Get(), output.Get());
				D3D11_MAPPED_SUBRESOURCE mapped{};
				checked(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
				std::array<float, partitioned.size()> values{};
				std::memcpy(values.data(), mapped.pData, sizeof(values));
				context->Unmap(readback.Get(), 0);
				return values;
			};
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.CPUAccessFlags = 0;
			desc.Format = DXGI_FORMAT_R8G8_UNORM;
			const auto mask = input(std::array<std::uint8_t, 12>{
				25, 50, 50, 75, 75, 100, 100, 125, 125, 150, 150, 175 });
			for (const bool back : { false, true }) {
				const auto consumer = cs::util::CompileShaderToBlob((a_root / "SSSConsumerProbe.hlsl").c_str(),
					back ? std::vector<std::pair<const char*, const char*>>{ { "SSS_BACK_TRANSMISSION", "1" } } :
						   std::vector<std::pair<const char*, const char*>>{},
					"cs_5_0", "main", &error, a_root);
				if (!consumer)
					return error;
				Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
				checked(device->CreateComputeShader(consumer->GetBufferPointer(), consumer->GetBufferSize(), nullptr, shader.GetAddressOf()));
				auto* maskView = mask.Get();
				context->CSSetShaderResources(0, 1, &rawView);
				context->CSSetShaderResources(45, 1, &maskView);
				context->CSSetUnorderedAccessViews(0, 1, &outputView, nullptr);
				context->CSSetShader(shader.Get(), nullptr, 0);
				context->Dispatch(1, 1, 1);
				context->ClearState();
				const auto actual = read();
				for (std::size_t pixel = 0; pixel < actual.size(); ++pixel) {
					const auto visibility = pixel < 3 ? 1.0f :
					                                    (static_cast<float>(100 + (pixel - 3) * 25 + (back ? 25 : 0)) / 255.0f);
					if (std::abs(actual[pixel] - visibility) > 1e-6f)
						return "near receiver shadowed, mask pixel shifted, or front/back channel mismatched";
				}
			}
			return {};
		} catch (const std::exception& a_error) {
			return a_error.what();
		}
	}

	std::string VerifyCubemapCaptureBoundary(const std::filesystem::path& a_root)
	{
		using namespace DirectX;
		using Microsoft::WRL::ComPtr;
		try {
			const auto checked = [](HRESULT a_result) {
				if (FAILED(a_result))
					throw std::runtime_error("cubemap capture WARP fixture failed");
			};
			ComPtr<ID3D11Device> device;
			ComPtr<ID3D11DeviceContext> context;
			checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
				D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf()));
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			const auto input = [&](const XMFLOAT4& a_value) {
				D3D11_SUBRESOURCE_DATA data{ &a_value, sizeof(a_value), 0 };
				ComPtr<ID3D11Texture2D> texture;
				ComPtr<ID3D11ShaderResourceView> view;
				checked(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()));
				checked(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));
				return view;
			};
			const auto scene = input({ 2.0f, 3.0f, 4.0f, 1.0f });
			const auto albedo = input({ 0.2f, 0.3f, 0.4f, 1.0f });
			const auto diffuse = input({ 0.5f, 0.4f, 0.3f, 1.0f });
			const auto tiled = input({ 0.1f, 0.2f, 0.3f, 1.0f });
			const auto emissive = input({ 0.01f, 0.02f, 0.03f, 1.0f });
			ComPtr<ID3D11Texture2D> depth;
			ComPtr<ID3D11ShaderResourceView> depthView;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			checked(device->CreateTexture2D(&desc, nullptr, depth.GetAddressOf()));
			checked(device->CreateShaderResourceView(depth.Get(), nullptr, depthView.GetAddressOf()));

			desc.Width = desc.Height = 8;
			desc.ArraySize = 6;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			std::array<ComPtr<ID3D11Texture2D>, 3> output;
			std::array<ComPtr<ID3D11UnorderedAccessView>, 3> outputViews;
			std::array<ComPtr<ID3D11Texture2D>, 3> readback;
			for (std::size_t index = 0; index < output.size(); ++index) {
				desc.Format = index == 2 ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
				desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.CPUAccessFlags = 0;
				checked(device->CreateTexture2D(&desc, nullptr, output[index].GetAddressOf()));
				checked(device->CreateUnorderedAccessView(output[index].Get(), nullptr, outputViews[index].GetAddressOf()));
				desc.BindFlags = 0;
				desc.Usage = D3D11_USAGE_STAGING;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				checked(device->CreateTexture2D(&desc, nullptr, readback[index].GetAddressOf()));
			}
			const auto read = [&](std::size_t a_output, UINT a_face) {
				context->CopyResource(readback[a_output].Get(), output[a_output].Get());
				D3D11_MAPPED_SUBRESOURCE mapped{};
				checked(context->Map(readback[a_output].Get(), a_face, D3D11_MAP_READ, 0, &mapped));
				XMFLOAT4 value{};
				const auto stride = a_output == 2 ? sizeof(XMFLOAT2) : sizeof(value);
				std::memcpy(&value, static_cast<const std::byte*>(mapped.pData) + mapped.RowPitch * 4 + stride * 4, stride);
				context->Unmap(readback[a_output].Get(), a_face);
				return value;
			};
			const auto buffer = [&](const auto& a_value) {
				D3D11_BUFFER_DESC bufferDesc{};
				bufferDesc.ByteWidth = static_cast<UINT>(sizeof(a_value));
				bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				D3D11_SUBRESOURCE_DATA data{ &a_value, 0, 0 };
				ComPtr<ID3D11Buffer> result;
				checked(device->CreateBuffer(&bufferDesc, &data, result.GetAddressOf()));
				return result;
			};
			cs::render::FrameDataCB frame{};
			const auto view = XMMatrixRotationY(-XM_PIDIV2);
			const XMMATRIX projection(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, -10, 0);
			XMStoreFloat4x4(&frame.CameraView, XMMatrixTranspose(view));
			XMStoreFloat4x4(&frame.CameraViewInverse, XMMatrixTranspose(XMMatrixInverse(nullptr, view)));
			XMStoreFloat4x4(&frame.CameraProj, XMMatrixTranspose(projection));
			frame.DynamicResolutionParams1 = frame.DynamicResolutionParams2 = { 1, 1, 1, 1 };
			XMFLOAT4X4 inverseProjection;
			XMStoreFloat4x4(&inverseProjection, XMMatrixInverse(nullptr, projection));
			cs::render::SharedDataCB shared{};
			shared.CameraData.x = 1000;
			const auto prepareBuffer = buffer(inverseProjection);
			const auto frameBuffer = buffer(frame);
			const auto sharedBuffer = buffer(shared);
			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			ComPtr<ID3D11SamplerState> sampler;
			checked(device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf()));
			for (bool reflections : { false, true }) {
				std::string error;
				const auto blob = cs::util::CompileShaderToBlob((a_root / "FO4" / "DynamicCubemaps" / "PrepareCaptureCS.hlsl").c_str(),
					reflections ? std::vector<std::pair<const char*, const char*>>{ { "REFLECTIONS", "1" } } :
								  std::vector<std::pair<const char*, const char*>>{},
					"cs_5_0", "main", &error, a_root);
				if (!blob)
					return error;
				ComPtr<ID3D11ComputeShader> shader;
				checked(device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, shader.GetAddressOf()));
				for (float depthValue : { 0.005f, 0.02f, 0.5f, 1.0f }) {
					context->UpdateSubresource(depth.Get(), 0, nullptr, &depthValue, sizeof(depthValue), 0);
					std::array<ID3D11ShaderResourceView*, 6> inputs{ depthView.Get(), scene.Get(), albedo.Get(), diffuse.Get(), tiled.Get(), emissive.Get() };
					std::array<ID3D11UnorderedAccessView*, 3> uavs{ outputViews[0].Get(), outputViews[1].Get(), outputViews[2].Get() };
					auto* prepare = prepareBuffer.Get();
					auto* camera = frameBuffer.Get();
					auto* data = sharedBuffer.Get();
					auto* linear = sampler.Get();
					context->CSSetShaderResources(0, static_cast<UINT>(inputs.size()), inputs.data());
					context->CSSetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data(), nullptr);
					context->CSSetConstantBuffers(0, 1, &prepare);
					context->CSSetConstantBuffers(4, 1, &camera);
					context->CSSetConstantBuffers(5, 1, &data);
					context->CSSetSamplers(0, 1, &linear);
					context->CSSetShader(shader.Get(), nullptr, 0);
					context->Dispatch(1, 1, 6);
					context->ClearState();
					const auto position = read(0, 0);
					const bool valid = depthValue == 0.5f || (reflections && depthValue == 1.0f);
					if ((position.w > 0) != valid || read(0, 1).w > 0)
						return "first-person/near/sky exclusion or positive-Z cube orientation mismatch";
					if (!valid)
						continue;
					const float distance = depthValue == 1 ? 1.0f : 0.01f / (1.0f - (depthValue * 1.01f - 0.01f));
					const auto color = read(1, 0);
					const auto uv = read(2, 0);
					const XMFLOAT3 radiance = depthValue == 1 ? XMFLOAT3{ 2, 3, 4 } : XMFLOAT3{ 0.37f, 0.56f, 0.75f };
					if (std::abs(position.x - distance) > 1e-5f || std::abs(position.y + distance * 0.125f) > 1e-5f ||
						std::abs(position.z + distance * 0.125f) > 1e-5f || position.w != (depthValue == 1 ? 2.0f : 1.0f) ||
						std::abs(color.x - radiance.x) > 1e-5f || std::abs(color.y - radiance.y) > 1e-5f ||
						std::abs(color.z - radiance.z) > 1e-5f || std::abs(uv.x - 0.5625f) > 1e-5f || std::abs(uv.y - 0.5625f) > 1e-5f)
						return "capture radiance, camera rotation, depth reconstruction or packed screen coordinates mismatch";
				}
			}
			return {};
		} catch (const std::exception& a_error) {
			return a_error.what();
		}
	}

	struct ABIField
	{
		const char* name;
		std::size_t offset, size;
	};

#define ABI(type, member) \
	ABIField { #member, offsetof(type, member), sizeof(type::member) }

	std::string CheckStructLayout(ID3D11ShaderReflectionType* a_type, std::span<const ABIField> a_fields)
	{
		D3D11_SHADER_TYPE_DESC desc{};
		if (FAILED(a_type->GetDesc(&desc)) || desc.Members != a_fields.size())
			return "struct member count mismatch";
		for (UINT index = 0; index < desc.Members; ++index) {
			const auto* name = a_type->GetMemberTypeName(index);
			const auto expected = std::ranges::find_if(a_fields, [&](const auto& a_field) { return std::string_view(name) == a_field.name; });
			D3D11_SHADER_TYPE_DESC member{};
			if (expected == a_fields.end() || FAILED(a_type->GetMemberTypeByIndex(index)->GetDesc(&member)))
				return std::string(name) + ": missing struct member";
			const UINT size = member.Class == D3D_SVC_MATRIX_ROWS    ? 16 * member.Rows :
			                  member.Class == D3D_SVC_MATRIX_COLUMNS ? 16 * member.Columns :
			                                                           4 * member.Rows * member.Columns;
			if (member.Offset != expected->offset || size != expected->size || member.Elements != 0)
				return std::string(name) + ": struct offset/size mismatch";
		}
		return {};
	}

	std::string CheckSubstrateBlocks(ID3D11ShaderReflection* a_reflection)
	{
		using namespace cs::render;
		// The local alias checks each descriptor against the real C++ member.
#define F(Field) ABI(T, Field)
#define FIELDS(Type, ...) [] { using T = Type; return std::array{ __VA_ARGS__ }; }()
		const auto grass = FIELDS(GrassLightingSettings, F(Glossiness), F(SpecularStrength), F(SubsurfaceScatteringAmount), F(OverrideComplexGrassSettings),
			F(BasicGrassBrightness), F(ComplexGrassThreshold), F(MidLODBrightness), F(FarLODBrightness));
		const auto material = FIELDS(CPMSettings, F(EnableComplexMaterial), F(EnableParallax), F(EnableTerrainParallax), F(EnableHeightBlending),
			F(EnableShadows), F(EnableParallaxWarpingFix), F(pad0));
		const auto cube = FIELDS(CubemapCreatorSettings, F(Enabled), F(pad0), F(CubemapColor));
		const auto terrain = FIELDS(TerraOccSettings, F(EnableTerrainShadow), F(Scale), F(ZRange), F(Offset), F(ZBlur), F(pad0));
		const auto lights = FIELDS(LightLimitFixSettings, F(EnableLightsVisualisation), F(LightsVisualisationMode), F(pad0), F(ClusterSize));
		const auto wetness = FIELDS(WetnessEffectsSettings, F(OcclusionViewProj), F(Time), F(Raining), F(Wetness), F(PuddleWetness), F(EnableWetnessEffects),
			F(MaxRainWetness), F(MaxPuddleWetness), F(MaxShoreWetness), F(ShoreRange), F(PuddleRadius), F(PuddleMaxAngle), F(PuddleMinWetness),
			F(MinRainWetness), F(SkinWetness), F(WeatherTransitionSpeed), F(EnableRaindropFx), F(EnableSplashes), F(EnableRipples), F(EnableVanillaRipples),
			F(RaindropFxRange), F(RaindropGridSizeRcp), F(RaindropIntervalRcp), F(RaindropChance), F(SplashesLifetime), F(SplashesStrength),
			F(SplashesMinRadius), F(SplashesMaxRadius), F(RippleStrength), F(RippleRadius), F(RippleBreadth), F(RippleLifetimeRcp), F(pad0));
		const auto sky = FIELDS(SkylightingSettings, F(OcclusionViewProj), F(OcclusionDir), F(PosOffset), F(ArrayOrigin), F(ValidMargin),
			F(MinDiffuseVisibility), F(MinSpecularVisibility), F(pad0));
		const auto cloud = FIELDS(CloudShadowsSettings, F(Opacity), F(pad0));
		const auto lod = FIELDS(LODBlendingSettings, F(LODTerrainBrightness), F(LODObjectBrightness), F(LODObjectSnowBrightness),
			F(DisableTerrainVertexColors), F(LODTerrainGamma), F(LODObjectGamma), F(LODObjectSnowGamma), F(pad0));
		const auto hair = FIELDS(HairSpecularSettings, F(Enabled), F(HairGlossiness), F(SpecularMult), F(DiffuseMult), F(EnableTangentShift),
			F(PrimaryTangentShift), F(SecondaryTangentShift), F(HairSaturation), F(SpecularIndirectMult), F(DiffuseIndirectMult), F(BaseColorMult),
			F(Transmission), F(EnableSelfShadow), F(SelfShadowStrength), F(SelfShadowExponent), F(SelfShadowScale), F(HairMode), F(pad));
		const auto variation = FIELDS(TerrainVariationSettings, F(enableLODTerrainTilingFix), F(enableMeshSupport), F(pad));
		const auto ibl = FIELDS(IBLSettings, F(EnableIBL), F(PreserveFogLuminance), F(UseStaticIBL), F(DALCAmount), F(EnvIBLScale), F(SkyIBLScale),
			F(EnvIBLSaturation), F(SkyIBLSaturation), F(FogAmount), F(DALCMode), F(pad0), F(pad1));
		const auto translucency = FIELDS(ExtendedTranslucencySettings, F(MaterialModel), F(Reduction), F(Softness), F(Strength));
		const auto linear = FIELDS(LinearLightingSettings, F(enableLinearLighting), F(isDirLightLinear), F(dirLightMult), F(lightGamma), F(colorGamma),
			F(emitColorGamma), F(glowmapGamma), F(ambientGamma), F(fogGamma), F(fogAlphaGamma), F(effectGamma), F(effectAlphaGamma), F(skyGamma),
			F(waterGamma), F(vlGamma), F(vanillaDiffuseColorMult), F(directionalLightMult), F(pointLightMult), F(ambientMult), F(emitColorMult),
			F(glowmapMult), F(effectLightingMult), F(membraneEffectMult), F(bloodEffectMult), F(projectedEffectMult), F(deferredEffectMult), F(otherEffectMult), F(pad0));
		const auto enb = FIELDS(ENBSettings, F(Enable), F(ColorPow), F(LightSpriteIntensity), F(FireIntensity), F(FireCurve), F(EnableRain),
			F(RainMotionStretch), F(RainMotionTransparency), F(CloudsCurve), F(CloudsDesaturation), F(CloudsEdgeIntensity), F(CloudsEdgeMoonMultiplier),
			F(EnableProceduralSun), F(ProceduralSunDiskRadiusSq), F(ProceduralSunDiskEdgeScale), F(ProceduralSunGlowIntensity),
			F(ProceduralSunCoronaFalloff), F(ProceduralSunCoronaScale), F(UseProceduralGradientWeights), F(ProceduralGradientWeightCurve),
			F(LightSpriteCurve), F(pad1), F(ParticleIntensity), F(ParticleLightingInfluence), F(ParticleAmbientInfluence), F(ParticlePointLightingInfluence),
			F(EnableVolumetricRays), F(VolumetricRaysIntensity), F(VolumetricRaysExtinction), F(VolumetricRaysSkyColorAmount), F(VolumetricRaysDesaturation), F(VolumetricRaysColorFilter));
		const auto blending = FIELDS(TerrainBlendingSettings, F(Enabled), F(_padding));
		const auto fog = FIELDS(ExponentialHeightFogSettings, F(enabled), F(useDynamicCubemaps), F(startDistance), F(fogHeight), F(fogHeightFalloff),
			F(fogDensity), F(directionalInscatteringMultiplier), F(directionalInscatteringAnisotropy), F(inscatteringTint), F(cubemapMipLevel),
			F(sunlightAttenuationAmount), F(respectVanillaFogFade), F(disableVanillaFog), F(fogInscatteringColor), F(originalFogColorAmount),
			F(volumetricFogEnabled), F(volumetricGridPixelSize), F(volumetricGridSizeZ), F(volumetricFogDistance), F(volumetricFogStartDistance),
			F(volumetricFogNearFadeInDistance), F(volumetricFogExtinctionScale), F(volumetricFogAlbedo), F(volumetricFogEmissive),
			F(volumetricDirectionalScatteringIntensity), F(volumetricShadowBias), F(volumetricDepthDistributionScale), F(volumetricSkyLightingIntensity),
			F(volumetricFogScatteringDistribution), F(volumetricHistoryWeight), F(volumetricHistoryMissSampleCount), F(volumetricSampleJitterMultiplier),
			F(volumetricUpsampleJitterMultiplier), F(volumetricLocalLightScatteringIntensity), F(pad0));
		const auto pbr = FIELDS(TruePBRSettings, F(VertexAOStrength), F(EnableMicroShadows), F(MicroShadowStrength), F(pad));
		const auto skin = FIELDS(SkinData, F(skinParams), F(skinParams2), F(skinDetailParams), F(sssParams), F(fuzzParams), F(physicalParams), F(wetParams));
		const auto horizon = FIELDS(HorizonFixSettings, F(farWaterDistance), F(pad));
#undef FIELDS
#undef F
		struct Block
		{
			UINT slot;
			const char* name;
			std::span<const ABIField> fields;
		};
		const Block blocks[]{
			{ 6, "grassLightingSettings", grass }, { 6, "extendedMaterialSettings", material },
			{ 6, "cubemapCreatorSettings", cube }, { 6, "terraOccSettings", terrain },
			{ 6, "lightLimitFixSettings", lights }, { 6, "wetnessEffectsSettings", wetness },
			{ 6, "skylightingSettings", sky }, { 6, "cloudShadowsSettings", cloud },
			{ 6, "lodBlendingSettings", lod }, { 6, "hairSpecularSettings", hair },
			{ 6, "terrainVariationSettings", variation }, { 6, "iblSettings", ibl },
			{ 6, "extendedTranslucencySettings", translucency }, { 6, "linearLightingSettings", linear },
			{ 6, "enbSettings", enb }, { 6, "terrainBlendingSettings", blending },
			{ 6, "exponentialHeightFogSettings", fog }, { 6, "truePBRSettings", pbr },
			{ 6, "skinData", skin }, { 6, "horizonFixSettings", horizon }
		};
		D3D11_SHADER_DESC shader{};
		a_reflection->GetDesc(&shader);
		for (const auto& block : blocks) {
			bool found = false;
			for (UINT index = 0; index < shader.BoundResources && !found; ++index) {
				D3D11_SHADER_INPUT_BIND_DESC binding{};
				a_reflection->GetResourceBindingDesc(index, &binding);
				if (binding.Type != D3D_SIT_CBUFFER || binding.BindPoint != block.slot)
					continue;
				auto* buffer = a_reflection->GetConstantBufferByName(binding.Name);
				D3D11_SHADER_BUFFER_DESC desc{};
				buffer->GetDesc(&desc);
				for (UINT member = 0; member < desc.Variables; ++member) {
					auto* variable = buffer->GetVariableByIndex(member);
					D3D11_SHADER_VARIABLE_DESC value{};
					variable->GetDesc(&value);
					const auto name = std::string_view(value.Name);
					if (name != block.name && !name.ends_with(std::string("::") + block.name))
						continue;
					if (const auto error = CheckStructLayout(variable->GetType(), block.fields); !error.empty())
						return std::string(block.name) + "." + error;
					found = true;
					break;
				}
			}
			if (!found)
				return std::string(block.name) + ": missing ABI block";
		}
		return {};
	}
	std::string VerifySubstrateABI(const std::filesystem::path& a_root)
	{
		using namespace cs::render;
		const ABIField frame[]{
			ABI(FrameDataCB, CameraView), ABI(FrameDataCB, CameraProj), ABI(FrameDataCB, CameraViewProj),
			ABI(FrameDataCB, CameraViewProjUnjittered), ABI(FrameDataCB, CameraPreviousViewProjUnjittered),
			ABI(FrameDataCB, CameraProjUnjittered), ABI(FrameDataCB, CameraProjUnjitteredInverse),
			ABI(FrameDataCB, CameraViewInverse), ABI(FrameDataCB, CameraViewProjInverse), ABI(FrameDataCB, CameraProjInverse),
			ABI(FrameDataCB, CameraPosAdjust), ABI(FrameDataCB, CameraPreviousPosAdjust), ABI(FrameDataCB, FrameParams),
			ABI(FrameDataCB, DynamicResolutionParams1), ABI(FrameDataCB, DynamicResolutionParams2)
		};
		const ABIField shared[]{
			ABI(SharedDataCB, WaterData), ABI(SharedDataCB, DirLightDirection), ABI(SharedDataCB, DirLightColor),
			ABI(SharedDataCB, SunDirection), ABI(SharedDataCB, SunColor), ABI(SharedDataCB, MasserDirection),
			ABI(SharedDataCB, MasserColor), ABI(SharedDataCB, SecundaDirection), ABI(SharedDataCB, SecundaColor),
			ABI(SharedDataCB, CameraData), ABI(SharedDataCB, BufferDim), ABI(SharedDataCB, Timer),
			ABI(SharedDataCB, FrameCount), ABI(SharedDataCB, FrameCountAlwaysActive), ABI(SharedDataCB, InInterior),
			ABI(SharedDataCB, HasDirectionalShadows), ABI(SharedDataCB, InMapMenu), ABI(SharedDataCB, HideSky),
			ABI(SharedDataCB, MipBias), ABI(SharedDataCB, WaterSystemHeight), ABI(SharedDataCB, pad0),
			ABI(SharedDataCB, AmbientSHR), ABI(SharedDataCB, AmbientSHG), ABI(SharedDataCB, AmbientSHB), ABI(SharedDataCB, HDRData)
		};
		const ABIField feature[]{
			ABI(SharedFeatureDataCB, grassLightingSettings), ABI(SharedFeatureDataCB, extendedMaterialSettings),
			ABI(SharedFeatureDataCB, cubemapCreatorSettings), ABI(SharedFeatureDataCB, terraOccSettings),
			ABI(SharedFeatureDataCB, lightLimitFixSettings), ABI(SharedFeatureDataCB, wetnessEffectsSettings),
			ABI(SharedFeatureDataCB, skylightingSettings), ABI(SharedFeatureDataCB, cloudShadowsSettings),
			ABI(SharedFeatureDataCB, lodBlendingSettings), ABI(SharedFeatureDataCB, hairSpecularSettings),
			ABI(SharedFeatureDataCB, terrainVariationSettings), ABI(SharedFeatureDataCB, iblSettings),
			ABI(SharedFeatureDataCB, extendedTranslucencySettings), ABI(SharedFeatureDataCB, linearLightingSettings),
			ABI(SharedFeatureDataCB, enbSettings), ABI(SharedFeatureDataCB, terrainBlendingSettings),
			ABI(SharedFeatureDataCB, exponentialHeightFogSettings), ABI(SharedFeatureDataCB, truePBRSettings),
			ABI(SharedFeatureDataCB, skinData), ABI(SharedFeatureDataCB, horizonFixSettings)
		};
		const ABIField fo4[]{
			ABI(FO4SharedDataCB, DebugOwner), ABI(FO4SharedDataCB, DebugMode),
			ABI(FO4SharedDataCB, EnabledSSR),
			ABI(FO4SharedDataCB, DeltaTime), ABI(FO4SharedDataCB, DebugParams),
			ABI(FO4SharedDataCB, EnabledDynamicCubemaps), ABI(FO4SharedDataCB, pad0)
		};
		struct Buffer
		{
			const char* name;
			UINT slot;
			std::size_t size;
			std::span<const ABIField> fields;
		};
		const Buffer buffers[]{
			{ "PerFrame", 4, sizeof(FrameDataCB), frame }, { "SharedData", 5, sizeof(SharedDataCB), shared },
			{ "FeatureData", 6, sizeof(SharedFeatureDataCB), feature }, { "FO4SharedData", 7, sizeof(FO4SharedDataCB), fo4 }
		};
		std::string error;
		auto blob = cs::util::CompileShaderToBlob((a_root / "SharedDataProbe.hlsl").c_str(),
			{ { "FO4CS_SUBSTRATE", "1" } }, "ps_5_0", "main", &error, a_root);
		if (!blob)
			return error;
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf()))))
			return "ABI reflection failed";
		for (const auto& buffer : buffers) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			D3D11_SHADER_DESC shaderDesc{};
			reflection->GetDesc(&shaderDesc);
			bool found = false;
			for (UINT index = 0; index < shaderDesc.BoundResources; ++index) {
				reflection->GetResourceBindingDesc(index, &binding);
				if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == buffer.slot) {
					found = true;
					break;
				}
			}
			if (!found)
				return std::string(buffer.name) + ": ABI slot mismatch";
			auto* cb = reflection->GetConstantBufferByName(binding.Name);
			D3D11_SHADER_BUFFER_DESC desc{};
			if (FAILED(cb->GetDesc(&desc)) || desc.Size != buffer.size || desc.Variables != buffer.fields.size())
				return std::string(buffer.name) + ": ABI buffer size/member count mismatch";
			for (UINT index = 0; index < desc.Variables; ++index) {
				D3D11_SHADER_VARIABLE_DESC variable{};
				if (FAILED(cb->GetVariableByIndex(index)->GetDesc(&variable)))
					return "ABI member reflection failed";
				const auto name = std::string_view(variable.Name);
				const auto field = std::ranges::find_if(buffer.fields, [&](const auto& a_field) {
					return name == a_field.name || name.ends_with(std::string("::") + a_field.name);
				});
				if (field == buffer.fields.end() || variable.StartOffset != field->offset || variable.Size != field->size)
					return std::string(buffer.name) + "." + variable.Name + ": ABI offset/size mismatch";
			}
		}
		for (const char* slot : { "b4", "b7" }) {
			if (cs::util::CompileShaderToBlob((a_root / "SharedDataProbe.hlsl").c_str(),
					{ { "FO4CS_SUBSTRATE", "1" }, { "ABI_SLOT_COLLISION", slot } }, "ps_5_0", "main", &error, a_root))
				return std::string("ABI collision accepted at ") + slot;
			if (error.find("slot collision") == std::string::npos && error.find("overlap") == std::string::npos && error.find("cbuffer bank") == std::string::npos)
				return "ABI collision failed for an unrelated reason: " + error;
		}
		return CheckSubstrateBlocks(reflection.Get());
	}
	std::string VerifySSGIABI(const std::filesystem::path& a_root)
	{
		using Constants = cs::features::ssgi::Constants;
		const ABIField fields[]{
			ABI(Constants, PrevInvViewMat), ABI(Constants, NDCToViewMul), ABI(Constants, NDCToViewAdd),
			ABI(Constants, TexDim), ABI(Constants, RcpTexDim), ABI(Constants, FrameDim), ABI(Constants, RcpFrameDim),
			ABI(Constants, FrameIndex), ABI(Constants, NumSlices), ABI(Constants, NumSteps), ABI(Constants, MinScreenRadius),
			ABI(Constants, AORadius), ABI(Constants, GIRadius), ABI(Constants, EffectRadius), ABI(Constants, Thickness),
			ABI(Constants, DepthFadeRange), ABI(Constants, DepthFadeScaleConst), ABI(Constants, GISaturation),
			ABI(Constants, GIDistanceCompensation), ABI(Constants, GICompensationMaxDist), ABI(Constants, pad1),
			ABI(Constants, AOPower), ABI(Constants, GIStrength), ABI(Constants, DepthDisocclusion), ABI(Constants, NormalDisocclusion),
			ABI(Constants, MaxAccumFrames), ABI(Constants, BlurRadius), ABI(Constants, DistanceNormalisation), ABI(Constants, pad)
		};
		std::string error;
		auto blob = cs::util::CompileShaderToBlob((a_root / "ScreenSpaceGI/gi.cs.hlsl").c_str(),
			{ { "GI", "1" }, { "GI_SPECULAR", "1" }, { "TEMPORAL_DENOISER", "1" }, { "HALF_RES", "1" } },
			"cs_5_0", "main", &error, a_root);
		if (!blob)
			return error;
		Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
		if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf()))))
			return "SSGI ABI reflection failed";
		D3D11_SHADER_INPUT_BIND_DESC binding{};
		if (FAILED(reflection->GetResourceBindingDescByName("SSGICB", &binding)) || binding.BindPoint != 1)
			return "SSGICB: ABI slot mismatch";
		auto* cb = reflection->GetConstantBufferByName("SSGICB");
		D3D11_SHADER_BUFFER_DESC desc{};
		if (FAILED(cb->GetDesc(&desc)) || desc.Size != sizeof(Constants) || desc.Variables != std::size(fields))
			return "SSGICB: ABI buffer size/member count mismatch";
		for (const auto& field : fields) {
			auto* variable = cb->GetVariableByName(field.name);
			D3D11_SHADER_VARIABLE_DESC value{};
			if (FAILED(variable->GetDesc(&value)) || value.StartOffset != field.offset || value.Size != field.size)
				return std::string("SSGICB.") + field.name + ": ABI offset/size mismatch";
		}
		D3D11_SHADER_TYPE_DESC matrix{};
		if (FAILED(cb->GetVariableByName("PrevInvViewMat")->GetType()->GetDesc(&matrix)) || matrix.Class != D3D_SVC_MATRIX_COLUMNS)
			return "SSGICB.PrevInvViewMat: expected column-major storage";
		return {};
	}
#undef ABI

	void AddStandaloneFeatureShaders(
		std::vector<ShaderJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const auto ssgi = a_root / "ScreenSpaceGI";
		// Runtime resolution, GI and temporal permutations.
		for (const char* resolution : { "", "HALF_RES", "QUARTER_RES" }) {
			const auto withResolution = [&](ShaderDefines a_defines) {
				a_defines.push_back({ "FO4CS_SUBSTRATE", "1" });
				if (*resolution)
					a_defines.push_back({ resolution, "1" });
				return a_defines;
			};
			a_jobs.push_back({ .path = ssgi / "prefilterDepths.cs.hlsl", .defines = withResolution({ { "LINEAR_FILTER", "1" } }), .description = "prefilterDepths.cs.hlsl" });
			for (const char* file : { "prefilterRadiance.cs.hlsl", "prefilterNormal.cs.hlsl", "gi.cs.hlsl" })
				a_jobs.push_back({ .path = ssgi / file, .defines = withResolution({}), .description = file });
			for (const char* file : { "radianceDisocc.cs.hlsl", "gi.cs.hlsl", "blur.cs.hlsl" }) {
				a_jobs.push_back({ .path = ssgi / file, .defines = withResolution({ { "GI", "1" } }), .description = file });
				a_jobs.push_back({ .path = ssgi / file, .defines = withResolution({ { "GI", "1" }, { "TEMPORAL_DENOISER", "1" } }), .description = file });
				a_jobs.push_back({ .path = ssgi / file, .defines = withResolution({ { "GI", "1" }, { "GI_SPECULAR", "1" }, { "TEMPORAL_DENOISER", "1" } }), .description = file });
			}
			if (*resolution)
				a_jobs.push_back({ .path = ssgi / "upsample.cs.hlsl", .defines = withResolution({ { "GI", "1" }, { "GI_SPECULAR", "1" } }), .description = "upsample.cs.hlsl" });
		}
		a_jobs.push_back({ .path = a_root / "FO4" / "ScreenSpaceGI" / "Prepare.cs.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" }, { "GI", "1" }, { "GI_SPECULAR", "1" } },
			.description = "SSGI native normal/radiance input boundary",
			.required = { CB(1), CB(10), Texture(0), Texture(5) },
			.forbidden = { CB(7) } });

		const auto cubemaps = a_root / "DynamicCubemaps";
		const auto cubemapBoundary = a_root / "FO4" / "DynamicCubemaps";
		const ShaderDefines substrate{ { "FO4CS_SUBSTRATE", "1" } };
		for (const char* file : { "DetectCaptureLightingCS.hlsl", "UpdateCubemapCS.hlsl" }) {
			a_jobs.push_back({ .path = cubemaps / file,
				.defines = substrate,
				.description = "Dynamic Cubemaps default upstream capture input" });
		}
		for (const char* file : {
				 "DetectCaptureLightingCS.hlsl",
				 "UpdateCubemapCS.hlsl",
				 "InferCubemapCS.hlsl" }) {
			for (const auto& variant : std::vector<ShaderDefines>{
					 {},
					 { { "REFLECTIONS", "" } },
					 { { "FAKEREFLECTIONS", "" } },
					 { { "REFLECTIONS", "" }, { "FAKEREFLECTIONS", "" } } }) {
				auto defines = substrate;
				if (std::string_view(file) != "InferCubemapCS.hlsl")
					defines.emplace_back("DYNAMIC_CUBEMAPS_PREPARED_CAPTURE", "1");
				defines.insert(defines.end(), variant.begin(), variant.end());
				a_jobs.push_back({ .path = cubemaps / file,
					.defines = std::move(defines),
					.description = file });
			}
		}
		for (const char* file : {
				 "SpecularIrradianceCS.hlsl",
				 "BC6HEncodeCS.hlsl",
				 "CubemapPreviewCS.hlsl" }) {
			a_jobs.push_back({ .path = (std::string_view(file) == "CubemapPreviewCS.hlsl" ? cubemapBoundary : cubemaps) / file,
				.defines = substrate,
				.description = file });
		}
		for (bool reflections : { false, true }) {
			auto defines = substrate;
			if (reflections)
				defines.emplace_back("REFLECTIONS", "1");
			a_jobs.push_back({ .path = cubemapBoundary / "PrepareCaptureCS.hlsl",
				.defines = std::move(defines),
				.description = "Dynamic Cubemaps native capture preparation",
				.required = { CB(0), CB(4), Texture(0), Texture(1), Texture(2), Texture(3), Texture(4), Texture(5), Sampler(0) },
				.forbidden = { CB(7), CB(12) } });
		}

		const auto terrain = a_root / "TerrainShadows";
		a_jobs.push_back({ .path = a_root / "ScreenSpaceShadows" / "RaymarchCS.hlsl",
			.defines = { { "SAMPLE_COUNT", "64" }, { "TERRAIN_BLENDING", "" } },
			.description = "upstream Bend on R32 canonical world depth",
			.required = { CB(1), Texture(0), Sampler(0) },
			.forbidden = { CB(7) } });
		a_jobs.push_back({ .path = a_root / "FO4" / "WaterEffects" / "Debug.hlsl",
			.profile = "vs_5_0",
			.description = "water debug fullscreen vertex" });
		a_jobs.push_back({ .path = a_root / "FO4" / "WaterEffects" / "Debug.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "water debug upstream caustics",
			.required = { CB(4), CB(5), Texture(17), Texture(65), Sampler(14) },
			.forbidden = { CB(7) } });
		a_jobs.push_back({ .path = a_root / "FO4" / "WaterEffects" / "Debug.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" }, { "WATER_SUBMERSION_DEBUG", "1" } },
			.profile = "ps_5_0",
			.description = "water debug submersion",
			.required = { CB(4), CB(5), Texture(17) },
			.forbidden = { CB(7), Texture(65), Sampler(14) } });
		a_jobs.push_back({ .path = terrain / "ShadowUpdate.cs.hlsl",
			.description = "terrain shadow update" });
		a_jobs.push_back({ .path = terrain / "ShadowStatistics.cs.hlsl",
			.description = "terrain shadow statistics" });

		const auto upscaling = a_root / "Upscaling";
		a_jobs.push_back({ .path = a_root / "FO4" / "Upscaling" / "EncodeTexturesCS.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" } },
			.description = "temporal input encoding" });
		a_jobs.push_back({ .path = a_root / "FO4" / "Upscaling" / "DepthRefractionUpscalePS.hlsl",
			.defines = {
				{ "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "depth refraction upscale" });
		a_jobs.push_back({ .path = a_root / "FO4" / "Upscaling" / "UpscaleVS.hlsl",
			.profile = "vs_5_0",
			.description = "upscale fullscreen vertex" });
		a_jobs.push_back({ .path = upscaling / "RCAS" / "RCAS.hlsl",
			.profile = "cs_5_1",
			.description = "RCAS" });
	}

	void AddFeatureConsumers(
		std::vector<ShaderJob>& a_jobs,
		const std::filesystem::path& a_root)
	{
		const std::vector<Resource> reserved{ CB(4), CB(5), CB(6), CB(7), Texture(17) };
		const std::vector<Resource> shared{ CB(5), CB(6), CB(7) };

		a_jobs.push_back({ .path = a_root / "ISSSLRRaytracing.hlsl",
			.defines = { { "PSHADER", "1" }, { "UPSCALING", "1" }, { "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "SSLR dynamic resolution",
			.required = { CB(0), CB(4), Texture(0), Texture(1), Texture(2), Texture(3) } });
		a_jobs.push_back({ .path = a_root / "ISSSLRRaytracing.hlsl",
			.defines = { { "PSHADER", "1" }, { "UPSCALING", "1" }, { "FO4CS_SUBSTRATE", "1" }, { "DYNAMIC_CUBEMAPS", "1" } },
			.profile = "ps_5_0",
			.description = "SSLR live DC setting with upscaling",
			.required = { CB(0), CB(4), CB(7), Texture(0), Texture(1), Texture(2), Texture(3) },
			.forbidden = { CB(6) } });

		a_jobs.push_back({ .path = a_root / "SharedDataProbe.hlsl",
			.profile = "ps_5_0",
			.description = "shared data off",
			.forbidden = reserved });
		a_jobs.push_back({ .path = a_root / "SharedDataProbe.hlsl",
			.defines = { { "FO4CS_SUBSTRATE", "1" } },
			.profile = "ps_5_0",
			.description = "shared substrate b4/b5/b6/b7/t17",
			.required = reserved });
		a_jobs.push_back({ .path = a_root / "FO4" / "CanonicalDepthCS.hlsl",
			.description = "canonical depth boundary",
			.required = { CB(0), Texture(0) },
			.forbidden = reserved });

		const auto bsdfLight = a_root / "DFLight.hlsl";
		const ShaderDefines directional{
			{ "BSDFLIGHT_PS_DIRSPLITS2", "1" },
			{ "RGBSPEC", "1" },
			{ "SPECULAR", "1" },
			{ "LIGHT_TYPE", "1" },
			{ "DIRECTIONAL", "1" },
			{ "SHADOW", "1" },
			{ "DIRSPLITS", "2" }
		};
		a_jobs.push_back({ .path = bsdfLight,
			.defines = directional,
			.profile = "ps_5_0",
			.description = "BSDFLight feature off",
			.forbidden = {
				CB(4), CB(5), CB(6), CB(7), Texture(17), Texture(45), Texture(60), Texture(65),
				Sampler(13), Sampler(14) } });
		auto directionalFeatures = directional;
		directionalFeatures.insert(
			directionalFeatures.end(),
			{ { "FO4CS_SUBSTRATE", "1" },
				{ "SCREEN_SPACE_SHADOWS", "1" },
				{ "TERRAIN_SHADOWS", "1" },
				{ "WETNESS_EFFECTS", "1" },
				{ "DYNAMIC_CUBEMAPS", "1" },
				{ "WATER_EFFECTS", "1" } });
		a_jobs.push_back({ .path = bsdfLight,
			.defines = std::move(directionalFeatures),
			.profile = "ps_5_0",
			.description = "BSDFLight feature composition",
			.required = {
				CB(4), CB(5), CB(6), Texture(45), Texture(60), Texture(65),
				Sampler(13), Sampler(14) },
			.forbidden = { CB(7), Texture(24), Texture(30), Texture(32) } });
		for (const auto& [family, splits, shadowOnly, blend] : {
				 std::tuple{ "BSDFLIGHT_PS_DIRSPLITS1", "1", false, false },
				 std::tuple{ "BSDFLIGHT_PS_DIRSPLITS3", "3", false, false },
				 std::tuple{ "BSDFLIGHT_PS_SHADOW_ONLY", "1", true, false },
				 std::tuple{ "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1", true, true } }) {
			ShaderDefines defines{
				{ family, "1" }, { "DIRECTIONAL", "1" },
				{ "SHADOW", "1" }, { "DIRSPLITS", splits }, { "SPECULAR", "1" },
				{ "RGBSPEC", "1" }, { "FILTER_PCF1", "1" }, { "SCREEN_SPACE_SHADOWS", "1" }
			};
			if (shadowOnly)
				defines.emplace_back("SHADOW_ONLY", "1");
			if (!blend)
				defines.emplace_back("LIGHT_TYPE", "1");
			if (blend)
				defines.emplace_back("BLENDSPLIT", "1");
			a_jobs.push_back({ .path = bsdfLight,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = family,
				.required = { Texture(3), Texture(45) },
				.forbidden = { CB(7), Texture(24) } });
		}
		for (const bool specular : { false, true }) {
			ShaderDefines defines{ { "BSDFLIGHT_PS_UNSHADOWED", "1" }, { "DIRECTIONAL", "1" },
				{ "LIGHT_TYPE", "1" }, { "DIRSPLITS", "2" }, { "RGBSPEC", "1" }, { "SCREEN_SPACE_SHADOWS", "1" } };
			if (specular)
				defines.insert(defines.end(), { { "SPECULAR", "1" }, { "WETNESS_EFFECTS", "1" }, { "FO4CS_SUBSTRATE", "1" } });
			a_jobs.push_back({ .path = bsdfLight,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = specular ? "SSS no-cascade wetness composition" : "SSS directional light without cascades",
				.required = { Texture(3), Texture(45) },
				.forbidden = specular ? std::vector<Resource>{ Texture(24) } : std::vector<Resource>{ CB(7), Texture(24) } });
		}

		const auto composite = a_root / "DFComposite.hlsl";
		for (const char* file : { "VolumetricFogConservativeDepthCS.hlsl", "VolumetricFogMaterialCS.hlsl",
				 "VolumetricFogLightScatteringCS.hlsl", "VolumetricFogIntegrationCS.hlsl" }) {
			a_jobs.push_back({ .path = a_root / "ExponentialHeightFog" / file,
				.description = file,
				.required = { CB(0) },
				.forbidden = { CB(7), CB(12) } });
		}
		a_jobs.push_back({ .path = a_root / "ExponentialHeightFog" / "VolumetricFogLightScatteringCS.hlsl",
			.defines = { { "TERRAIN_SHADOWS", "1" } },
			.description = "VolumetricFog terrain scattering",
			.required = { CB(0), CB(4), CB(6), Texture(60) },
			.forbidden = { CB(7), CB(12) } });
		a_jobs.push_back({ .path = a_root / "FO4" / "ExponentialHeightFog" / "SkyCompositeCS.hlsl",
			.description = "ExponentialHeightFog post-sky boundary",
			.required = { CB(4), CB(5), CB(6), Texture(17), Texture(19) },
			.forbidden = { CB(7), CB(12) } });
		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY", "1" } },
				 { { "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" }, { "FOGSTACK", "1" } },
				 { { "BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY", "1" }, { "FOGSTACK", "1" } },
				 { { "BSDFCOMPOSITE_PS_2D_FOG", "1" }, { "COMPOSITE_HAS_LIGHT", "1" } },
				 { { "BSDFCOMPOSITE_PS_CUBE_IBL", "1" } },
				 { { "BSDFCOMPOSITE_PS_NO_T0_FOG", "1" }, { "WAVE5A_FOG_SHAPE", "1" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" }, { "EXPONENTIAL_HEIGHT_FOG", "1" } });
			a_jobs.push_back({ .path = composite, .defines = std::move(defines), .profile = "ps_5_0", .description = "ExponentialHeightFog composite consumer", .required = { CB(4), CB(5), CB(6), Texture(17), Texture(19), Sampler(13) }, .forbidden = { CB(7), CB(8), CB(9), CB(13), Texture(33), Texture(61) } });
			AddFullscreenDebugVariant(a_jobs, a_jobs.back(), "EXPONENTIAL_HEIGHT_FOG_FULLSCREEN_DEBUG", { CB(7) });
		}
		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFLIGHT_PS_DEFERRED", "1" }, { "AMBIENT_IBL_IN_LIGHT", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS1", "1" }, { "DIRSPLITS", "1" }, { "SHADOW", "1" }, { "FILTER_PCF1", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS2", "1" }, { "DIRSPLITS", "2" }, { "SHADOW", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS3", "1" }, { "DIRSPLITS", "3" }, { "SHADOW", "1" } },
				 { { "BSDFLIGHT_PS_SHADOW_ONLY", "1" }, { "DIRSPLITS", "1" }, { "SHADOW", "1" }, { "SHADOW_ONLY", "1" }, { "FILTER_PCF1", "1" } },
				 { { "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" }, { "DIRSPLITS", "1" }, { "SHADOW", "1" }, { "SHADOW_ONLY", "1" }, { "BLENDSPLIT", "1" }, { "FILTER_PCF1", "1" }, { "AMBIENT", "1" } },
				 { { "BSDFLIGHT_PS_UNSHADOWED", "1" }, { "DIRSPLITS", "2" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" }, { "EXPONENTIAL_HEIGHT_FOG", "1" },
											  { "DIRECTIONAL", "1" }, { "SPECULAR", "1" }, { "RGBSPEC", "1" },
											  { "WATER_EFFECTS", "1" }, { "WETNESS_EFFECTS", "1" }, { "TERRAIN_SHADOWS", "1" } });
			a_jobs.push_back({ .path = bsdfLight, .defines = std::move(defines), .profile = "ps_5_0", .description = "ExponentialHeightFog directional and coat consumer", .required = { CB(4), CB(5), CB(6) }, .forbidden = { Texture(19) } });
		}
		for (const auto& [file, defines] : std::vector<std::pair<const char*, ShaderDefines>>{
				 { "Water.hlsl", { { "BSWATER_PIXEL_SHADER", "1" }, { "REFLECTIONS", "1" } } },
				 { "Water.hlsl", { { "BSWATER_PIXEL_SHADER", "1" }, { "LOD", "1" } } },
				 { "DistantTree.hlsl", { { "BSDISTANTTREE_PS_SOURCE", "1" } } },
				 { "Effect.hlsl", { { "BSEFFECT_PS_SOURCE", "1" } } },
				 { "Effect.hlsl", { { "BSEFFECT_PS_SOURCE", "1" }, { "LIGHTING", "1" }, { "ADDBLEND", "1" } } },
				 { "Effect.hlsl", { { "BSEFFECT_PS_SOURCE", "1" }, { "MULTBLEND", "1" } } } }) {
			auto fogDefines = defines;
			fogDefines.insert(fogDefines.end(), { { "FO4CS_SUBSTRATE", "1" }, { "EXPONENTIAL_HEIGHT_FOG", "1" } });
			a_jobs.push_back({ .path = a_root / file, .defines = std::move(fogDefines), .profile = "ps_5_0", .description = "ExponentialHeightFog forward consumer", .required = { CB(4), CB(5), CB(6), Texture(19), Sampler(15) }, .forbidden = { CB(7) } });
		}
		// SSGI's vertex-AO write must compile for opaque, vertex-colour and blended prepass bodies.
		const auto prepass = a_root / "DFPrepass.hlsl";
		for (const ShaderDefines& defines : {
				 ShaderDefines{ { "BSDFPREPASS_PS_SOURCE", "1" }, { "SSGI", "1" } },
				 ShaderDefines{ { "BSDFPREPASS_PS_SOURCE", "1" }, { "SSGI", "1" }, { "VC", "1" } },
				 ShaderDefines{ { "BSDFPREPASS_PS_SOURCE", "1" }, { "SSGI", "1" }, { "VC", "1" }, { "BLEND", "1" } },
				 ShaderDefines{ { "BSDFPREPASS_PS_SOURCE", "1" }, { "SSGI", "1" }, { "HAIR", "1" }, { "BLEND", "1" } } }) {
			a_jobs.push_back({ .path = prepass,
				.defines = defines,
				.profile = "ps_5_0",
				.description = "BSDFPrePass SSGI vertex AO" });
		}
		a_jobs.push_back({ .path = composite,
			.defines = {
				{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" } },
			.profile = "ps_5_0",
			.description = "BSDFComposite feature off",
			.forbidden = { CB(4), CB(5), CB(6), CB(7), Texture(17), Texture(25), Texture(26), Texture(27), Texture(28), Texture(29), Texture(33), Texture(34), Texture(35), Texture(36), Texture(61) } });
		a_jobs.push_back({ .path = composite,
			.defines = {
				{ "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY", "1" },
				{ "FO4CS_SUBSTRATE", "1" },
				{ "SSGI", "1" },
				{ "WETNESS_EFFECTS", "1" },
				{ "DYNAMIC_CUBEMAPS", "1" },
				{ "TERRAIN_SHADOWS", "1" },
				{ "EXPONENTIAL_HEIGHT_FOG", "1" },
				{ "WATER_EFFECTS", "1" } },
			.profile = "ps_5_0",
			.description = "BSDFComposite feature composition",
			.required = { CB(4), CB(5), CB(6), CB(7), Texture(25), Texture(71), Texture(34), Texture(35) },
			.forbidden = { CB(8), CB(9), CB(13), Texture(33), Texture(26), Texture(27), Texture(28), Texture(29), Texture(61), Texture(65) } });
		const auto productionComposite = a_jobs.back();
		AddFullscreenDebugVariant(a_jobs, productionComposite, "TERRAIN_SHADOWS_FULLSCREEN_DEBUG", { CB(7), Texture(60), Texture(61), Sampler(13) });
		AddFullscreenDebugVariant(a_jobs, productionComposite, "EXPONENTIAL_HEIGHT_FOG_FULLSCREEN_DEBUG", { CB(7) });
		AddFullscreenDebugVariant(a_jobs, productionComposite, "WETNESS_EFFECTS_FULLSCREEN_DEBUG", { CB(7) });
		AddFullscreenDebugVariant(a_jobs, productionComposite, "WATER_EFFECTS_FULLSCREEN_DEBUG", { CB(7), Texture(61) });

		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFLIGHT_PS_DIRSPLITS1", "1" }, { "DIRSPLITS", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS3", "1" }, { "DIRSPLITS", "3" } },
				 { { "BSDFLIGHT_PS_SHADOW_ONLY", "1" }, { "DIRSPLITS", "1" }, { "SHADOW_ONLY", "1" } },
				 { { "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" }, { "DIRSPLITS", "1" }, { "SHADOW_ONLY", "1" }, { "BLENDSPLIT", "1" }, { "AMBIENT", "1" } },
				 { { "BSDFLIGHT_PS_UNSHADOWED", "1" }, { "DIRSPLITS", "2" } } }) {
			const bool unshadowed = defines.front().first == "BSDFLIGHT_PS_UNSHADOWED";
			defines.insert(defines.end(), { { "DIRECTIONAL", "1" }, { "SPECULAR", "1" }, { "RGBSPEC", "1" },
											  { "FO4CS_SUBSTRATE", "1" }, { "WATER_EFFECTS", "1" }, { "WETNESS_EFFECTS", "1" } });
			if (!unshadowed) {
				defines.emplace_back("SHADOW", "1");
				defines.emplace_back("FILTER_PCF1", "1");
			}
			a_jobs.push_back({ .path = bsdfLight,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "water RGB directional and coat consumer",
				.required = { CB(4), CB(5), Texture(65), Sampler(14) },
				.forbidden = { Texture(32) } });
		}
		// SSGI composes where diffuse light meets albedo.
		const std::pair<const char*, ShaderDefines> ssgiFamilies[] = {
			{ "2D accumulator SSGI", { { "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" },
										 { "COMPOSITE_CB2_COUNT", "6" },
										 { "COMPOSITE_MATERIAL_5", "1" },
										 { "COMPOSITE_MODULATION", "1" },
										 { "TILED_LIGHTS", "1" } } },
			{ "2D fog SSGI", { { "BSDFCOMPOSITE_PS_2D_FOG", "1" },
								 { "COMPOSITE_HAS_LIGHT", "1" },
								 { "COMPOSITE_MODULATION", "1" },
								 { "COMPOSITE_SCENE_BLEND", "1" } } },
			{ "2D fog material 5 SSGI", { { "BSDFCOMPOSITE_PS_2D_FOG", "1" },
											{ "COMPOSITE_HAS_TYPE", "1" },
											{ "COMPOSITE_MATERIAL_5", "1" },
											{ "COMPOSITE_HAS_LIGHT", "1" } } },
			{ "cube IBL SSGI", { { "BSDFCOMPOSITE_PS_CUBE_IBL", "1" } } },
			{ "cube IBL unfogged SSGI", { { "BSDFCOMPOSITE_PS_CUBE_IBL", "1" },
											{ "COMPOSITE_MATERIAL_EXCLUSION", "0" },
											{ "COMPOSITE_FOG_STACK", "0" } } }
		};
		for (const auto& [description, defines] : ssgiFamilies) {
			auto familyDefines = defines;
			familyDefines.push_back({ "FO4CS_SUBSTRATE", "1" });
			familyDefines.push_back({ "SSGI", "1" });
			a_jobs.push_back({ .path = composite,
				.defines = std::move(familyDefines),
				.profile = "ps_5_0",
				.description = description,
				.required = {
					CB(5), CB(10), Texture(26), Texture(27), Texture(28), Texture(29) },
				.forbidden = { CB(7) } });
			if (defines.front().first == "BSDFCOMPOSITE_PS_CUBE_IBL")
				a_jobs.back().required.push_back(Texture(38));
		}

		for (const char* family : {
				 "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY",
				 "BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY",
				 "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY",
				 "BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY",
				 "BSDFCOMPOSITE_PS_CUBE_IBL" }) {
			for (bool dynamicCubemaps : { false, true }) {
				ShaderDefines defines{
					{ family, "1" },
					{ "FO4CS_SUBSTRATE", "1" },
					{ "WETNESS_EFFECTS", "1" }
				};
				if (dynamicCubemaps)
					defines.emplace_back("DYNAMIC_CUBEMAPS", "1");
				a_jobs.push_back({ .path = composite,
					.defines = std::move(defines),
					.profile = "ps_5_0",
					.description = family,
					.required = dynamicCubemaps ?
				                    std::vector<Resource>{ CB(7), Texture(25), Texture(34), Texture(35), Texture(71) } :
				                    std::vector<Resource>{},
					.forbidden = dynamicCubemaps ?
				                     std::vector<Resource>{ CB(8), CB(9), CB(13), Texture(33), Texture(61) } :
				                     std::vector<Resource>{ CB(7), CB(8), CB(9), CB(13), Texture(33), Texture(61), Texture(34), Texture(35), Texture(71) } });
				AddFullscreenDebugVariant(a_jobs, a_jobs.back(), "WETNESS_EFFECTS_FULLSCREEN_DEBUG", { CB(7), Texture(25), Texture(71) });
			}
		}

		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFCOMPOSITE_PS_2D_ACCUMULATOR", "1" }, { "COMPOSITE_CB2_COUNT", "1" } },
				 { { "BSDFCOMPOSITE_PS_2D_FOG", "1" }, { "COMPOSITE_HAS_LIGHT", "1" } },
				 { { "BSDFCOMPOSITE_PS_NO_SRV_POSITION", "1" } },
				 { { "BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR", "1" }, { "WAVE5A_ACCUMULATOR_SHAPE", "1" } },
				 { { "BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL", "1" }, { "WAVE5B_SSS_RECORD_NORMAL_SHAPE", "1" } },
				 { { "BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT", "1" }, { "WAVE5B_SSS_SURFACE_CONTACT_SHAPE", "1" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" },
											  { "WETNESS_EFFECTS", "1" } });
			a_jobs.push_back({ .path = composite,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "BSDFComposite without debug film reads",
				.forbidden = { CB(7), CB(8), CB(9), CB(13), Texture(33), Texture(61), Texture(71) } });
			AddFullscreenDebugVariant(a_jobs, a_jobs.back(), "WETNESS_EFFECTS_FULLSCREEN_DEBUG", { CB(4), CB(6), CB(7), Texture(25), Texture(71) });
		}

		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY", "1" }, { "FOGSTACK", "1" }, { "SSGI", "1" } },
				 { { "BSDFCOMPOSITE_PS_CUBE_IBL", "1" }, { "COMPOSITE_MATERIAL_EXCLUSION", "0" }, { "COMPOSITE_FOG_STACK", "0" }, { "COMPOSITE_CB12_COUNT", "31" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" },
											  { "WETNESS_EFFECTS", "1" },
											  { "DYNAMIC_CUBEMAPS", "1" } });
			a_jobs.push_back({ .path = composite,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "BSDFComposite wet reflection reconstruction branches",
				.required = { Texture(34), Texture(35) } });
		}

		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFLIGHT_PS_DEFERRED", "1" }, { "AMBIENT_IBL_IN_LIGHT", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS1", "1" }, { "DIRSPLITS", "1" }, { "SHADOW", "1" }, { "FILTER_PCF1", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS2", "1" }, { "DIRSPLITS", "2" }, { "SHADOW", "1" } },
				 { { "BSDFLIGHT_PS_DIRSPLITS3", "1" }, { "DIRSPLITS", "3" }, { "SHADOW", "1" } },
				 { { "BSDFLIGHT_PS_SHADOW_ONLY_BLEND_SPLIT", "1" }, { "DIRSPLITS", "1" }, { "SHADOW", "1" }, { "SHADOW_ONLY", "1" }, { "BLENDSPLIT", "1" }, { "FILTER_PCF1", "1" } },
				 { { "BSDFLIGHT_PS_UNSHADOWED", "1" }, { "DIRSPLITS", "2" } },
				 { { "BSDFLIGHT_PS_AMBIENT", "1" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" },
											  { "WETNESS_EFFECTS", "1" },
											  { "DYNAMIC_CUBEMAPS", "1" },
											  { "AMBIENT", "1" },
											  { "DIRECTIONAL", "1" },
											  { "SPECULAR", "1" },
											  { "RGBSPEC", "1" } });
			a_jobs.push_back({ .path = bsdfLight,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "BSDFLight wet indirect diffuse",
				.required = { CB(6) },
				.forbidden = { Texture(30), Texture(31), Texture(34), Texture(35) } });
		}

		for (auto defines : std::vector<ShaderDefines>{
				 { { "BSDFLIGHT_PS_DEFERRED", "1" }, { "LIGHT_TYPE", "3" }, { "SPOT", "1" } },
				 { { "BSDFLIGHT_PS_GOBO", "1" }, { "POINTOMNI", "1" }, { "GOBOPROJECTION", "1" }, { "RGBSPEC", "1" }, { "DIRSPLITS", "2" } },
				 { { "BSDFLIGHT_PS_UNSHADOWED", "1" }, { "POINTOMNI", "1" }, { "RGBSPEC", "1" }, { "DIRSPLITS", "2" } } }) {
			defines.insert(defines.end(), { { "FO4CS_SUBSTRATE", "1" },
											  { "WETNESS_EFFECTS", "1" } });
			a_jobs.push_back({ .path = bsdfLight,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "BSDFLight wet direct coat camera reconstruction",
				.required = { CB(6), CB(12) } });
		}

		for (const auto& material : std::vector<ShaderDefines>{
				 {}, { { "SKINNED", "1" }, { "FACE", "1" } },
				 { { "LANDSCAPE", "1" } }, { { "BLEND", "1" } },
				 { { "MODELSPACENORMALS", "1" } }, { { "TESSELLATE_DISP_HEIGHT", "1" } },
				 { { "LOD_LANDSCAPE", "1" } }, { { "LOD_LANDSCAPE", "1" }, { "LOD_OBJECT_INSTANCED", "1" } },
				 { { "LANDSCAPE", "1" }, { "INSTANCED", "1" } },
				 { { "MERGE_INSTANCED", "1" } },
				 { { "SKINNED", "1" }, { "MODELSPACENORMALS", "1" } },
				 { { "MERGE_INSTANCED", "1" }, { "MODELSPACENORMALS", "1" } } }) {
			auto defines = material;
			defines.insert(defines.end(), { { "BSDFPREPASS_PS_SOURCE", "1" },
											  { "WETNESS_EFFECTS", "1" }, { "FO4CS_SUBSTRATE", "1" }, { "NORMALS", "1" } });
			a_jobs.push_back({ .path = prepass,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "Wetness deferred material producer",
				.required = { CB(4), CB(5), CB(6), Texture(71) },
				.forbidden = { CB(7), CB(8), CB(9), CB(13), Texture(33), Texture(61) } });
			auto vertexDefines = material;
			vertexDefines.insert(vertexDefines.end(), { { "BSDFPREPASS_VS_SOURCE", "1" },
														  { "WETNESS_EFFECTS", "1" }, { "NORMALS", "1" }, { "BINORMAL_TANGENT", "1" }, { "TEXTURE", "1" } });
			a_jobs.push_back({ .path = prepass,
				.defines = std::move(vertexDefines),
				.profile = "vs_5_0",
				.description = "Wetness deferred geometry interface",
				.forbidden = { CB(4), CB(5), CB(6), CB(7), CB(8), Texture(71) } });
		}
		for (const auto& material : std::vector<ShaderDefines>{
				 { { "GRASS", "1" } }, { { "GRASS", "1" }, { "ALPHA_TEST", "1" } },
				 { { "EYE", "1" } }, { { "TREE_ANIM", "1" } }, { { "LOD_OBJECT_INSTANCED", "1" } } }) {
			auto defines = material;
			defines.insert(defines.end(), { { "BSDFPREPASS_PS_SOURCE", "1" },
											  { "WETNESS_EFFECTS", "1" }, { "FO4CS_SUBSTRATE", "1" }, { "NORMALS", "1" } });
			a_jobs.push_back({ .path = prepass,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "Wetness upstream-excluded class writes a dry film",
				.required = { CB(6), Texture(71) },
				.forbidden = { CB(4), CB(5), CB(7), CB(8), CB(9), CB(13), Texture(33), Texture(61) } });
		}
		for (const auto& material : std::vector<ShaderDefines>{
				 { { "SKINNED", "1" } }, { { "MODELSPACENORMALS", "1" } } }) {
			auto defines = material;
			defines.insert(defines.end(), { { "BSDFPREPASS_PS_SOURCE", "1" },
											  { "WETNESS_EFFECTS", "1" }, { "FO4CS_SUBSTRATE", "1" }, { "TESSELLATE_DISP_HEIGHT", "1" } });
			a_jobs.push_back({ .path = prepass,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "Wetness incomplete domain interface retains native material behavior",
				.forbidden = { CB(4), CB(5), CB(6), CB(7), CB(8), Texture(71) } });
		}

		const auto tiled = a_root / "DFTiledLighting.hlsl";
		a_jobs.push_back({ .path = tiled,
			.defines = { { "DFTILEDLIGHTING_VARIANT", "1" } },
			.description = "DFTiled final 1 feature off",
			.forbidden = reserved });
		for (const char* variant : { "1", "2" }) {
			a_jobs.push_back({ .path = tiled,
				.defines = {
					{ "DFTILEDLIGHTING_VARIANT", variant },
					{ "FO4CS_SUBSTRATE", "1" },
					{ "WETNESS_EFFECTS", "1" },
					{ "DYNAMIC_CUBEMAPS", "1" },
					{ "INVERSE_SQUARE_LIGHTING", "1" } },
				.description = variant[0] == '1' ? "DFTiled final 1 inverse square" : "DFTiled final 2 inverse square",
				.required = { CB(6), StructuredBuffer(6), StructuredBuffer(7), StructuredBuffer(8) } });
		}
		for (int dimension = 10; dimension <= 25; ++dimension) {
			a_jobs.push_back({ .path = tiled,
				.defines = {
					{ "DFTILEDLIGHTING_VARIANT", "3" },
					{ "DFTILEDLIGHTING_TILE_CULL_GROUP_DIM", std::to_string(dimension) },
					{ "INVERSE_SQUARE_LIGHTING", "1" } },
				.description = "ISL guarded tile producer",
				.required = { StructuredBuffer(6) },
				.forbidden = reserved });
		}
		for (const ShaderDefines& local : {
				 ShaderDefines{ { "BSDFLIGHT_PS_UNSHADOWED", "1" }, { "POINTOMNI", "1" }, { "SPECULAR", "1" } },
				 ShaderDefines{ { "BSDFLIGHT_PS_GOBO", "1" }, { "POINTOMNI", "1" }, { "SPECULAR", "1" }, { "GOBOPROJECTION", "1" } },
				 ShaderDefines{ { "BSDFLIGHT_PS_ATTENUATION_ONLY", "1" }, { "POINTOMNI", "1" }, { "ATTENUATION_ONLY", "1" } },
				 ShaderDefines{ { "BSDFLIGHT_PS_DEFERRED", "1" }, { "POINTOMNI", "1" }, { "LIGHT_TYPE", "2" }, { "SPECULAR", "1" } },
				 ShaderDefines{ { "BSDFLIGHT_PS_DEFERRED", "1" }, { "SPOT", "1" }, { "LIGHT_TYPE", "3" }, { "SPECULAR", "1" } },
				 ShaderDefines{ { "BSDFLIGHT_PS_DEFERRED", "1" }, { "POINTSPOT", "1" }, { "LIGHT_TYPE", "3" },
					 { "SHADOW", "1" }, { "FILTER_PCF1", "1" }, { "SPECULAR", "1" } } }) {
			auto defines = local;
			defines.insert(defines.end(), { { "DIRSPLITS", "2" }, { "RGBSPEC", "1" },
											  { "FO4CS_SUBSTRATE", "1" }, { "INVERSE_SQUARE_LIGHTING", "1" } });
			a_jobs.push_back({ .path = bsdfLight,
				.defines = std::move(defines),
				.profile = "ps_5_0",
				.description = "ISL raster light consumer",
				.required = { CB(6), CB(11) },
				.forbidden = { CB(4), CB(5), CB(7) } });
		}

		const auto water = a_root / "Water.hlsl";
		a_jobs.push_back({ .path = a_root / "DistantTree.hlsl",
			.defines = { { "BSDISTANTTREE_PS_SOURCE", "1" }, { "FO4CS_SUBSTRATE", "1" }, { "TERRAIN_SHADOWS", "1" } },
			.profile = "ps_5_0",
			.description = "BSDistantTree terrain directional consumer",
			.required = { CB(4), CB(6), Texture(60), Sampler(13) } });
		a_jobs.push_back({ .path = a_root / "Effect.hlsl",
			.defines = { { "BSEFFECT_PS_SOURCE", "1" }, { "LIGHTING", "1" }, { "FO4CS_SUBSTRATE", "1" }, { "TERRAIN_SHADOWS", "1" } },
			.profile = "ps_5_0",
			.description = "BSEffect terrain directional consumer",
			.required = { CB(4), CB(6), Texture(60), Sampler(13) } });
		for (const char* family : { "LOD", "BSWATER_SURFACE" }) {
			a_jobs.push_back({ .path = water,
				.defines = { { "BSWATER_PIXEL_SHADER", "1" }, { family, "1" }, { "FO4CS_SUBSTRATE", "1" }, { "TERRAIN_SHADOWS", "1" } },
				.profile = "ps_5_0",
				.description = "BSWater terrain directional consumer",
				.required = { CB(4), CB(6), Texture(60), Sampler(13) } });
		}
		const ShaderDefines waterBase{
			{ "BSWATER_PIXEL_SHADER", "1" },
			{ "REFLECTIONS", "1" }
		};
		a_jobs.push_back({ .path = water,
			.defines = waterBase,
			.profile = "ps_5_0",
			.description = "BSWater dynamic cubemaps off",
			.forbidden = { CB(4), CB(5), CB(6), CB(7), Texture(17), Texture(30), Texture(31), Sampler(3) } });
		auto waterFeatures = waterBase;
		waterFeatures.emplace_back("FO4CS_SUBSTRATE", "1");
		waterFeatures.emplace_back("DYNAMIC_CUBEMAPS", "1");
		a_jobs.push_back({ .path = water,
			.defines = std::move(waterFeatures),
			.profile = "ps_5_0",
			.description = "BSWater dynamic cubemaps",
			.required = { CB(5), CB(6), Texture(30), Texture(31), Sampler(3) } });

		a_jobs.push_back({ .path = composite,
			.defines = { { "BSDFCOMPOSITE_PS_CUBE_IBL", "1" }, { "FO4CS_SUBSTRATE", "1" },
				{ "SSGI", "1" }, { "WETNESS_EFFECTS", "1" }, { "DYNAMIC_CUBEMAPS", "1" },
				{ "EXPONENTIAL_HEIGHT_FOG", "1" } },
			.profile = "ps_5_0",
			.description = "SSGI wet cubemap and fog composition",
			.required = { CB(6), CB(7), CB(10), Texture(19), Texture(26), Texture(34), Texture(38), Texture(71) } });
		a_jobs.push_back({ .path = bsdfLight,
			.defines = { { "BSDFLIGHT_PS_DEFERRED", "1" }, { "LIGHT_TYPE", "3" }, { "SPOT", "1" },
				{ "SPECULAR", "1" }, { "RGBSPEC", "1" }, { "DIRSPLITS", "2" },
				{ "FO4CS_SUBSTRATE", "1" }, { "INVERSE_SQUARE_LIGHTING", "1" },
				{ "WETNESS_EFFECTS", "1" }, { "DYNAMIC_CUBEMAPS", "1" }, { "EXPONENTIAL_HEIGHT_FOG", "1" } },
			.profile = "ps_5_0",
			.description = "ISL wet coat with fog and cubemap consumers",
			.required = { CB(6), CB(11) },
			.forbidden = { CB(7) } });
		a_jobs.push_back({ .path = prepass,
			.defines = { { "BSDFPREPASS_PS_SOURCE", "1" }, { "FO4CS_SUBSTRATE", "1" },
				{ "NORMALS", "1" }, { "VC", "1" }, { "SSGI", "1" }, { "WETNESS_EFFECTS", "1" } },
			.profile = "ps_5_0",
			.description = "Wet material and SSGI vertex AO producer",
			.required = { CB(6), Texture(71) },
			.forbidden = { CB(7), CB(8), CB(9), CB(13), Texture(33), Texture(61) } });
		a_jobs.push_back({ .path = water,
			.defines = { { "BSWATER_PIXEL_SHADER", "1" }, { "REFLECTIONS", "1" },
				{ "FO4CS_SUBSTRATE", "1" }, { "DYNAMIC_CUBEMAPS", "1" },
				{ "EXPONENTIAL_HEIGHT_FOG", "1" }, { "TERRAIN_SHADOWS", "1" } },
			.profile = "ps_5_0",
			.description = "Water cubemap fog and terrain composition",
			.required = { CB(6), CB(7), Texture(19), Texture(30), Texture(31), Texture(60) } });
	}
}

int main(int argc, char** argv)
{
	if (argc != 2) {
		std::fprintf(stderr, "Usage: ShaderCompileTests <shader directory>\n");
		return 2;
	}

	std::vector<ShaderJob> jobs;
	AddFeatureConsumers(jobs, argv[1]);
	AddStandaloneFeatureShaders(jobs, argv[1]);

	int failures = 0;
	if (const auto error = CheckScreenSpaceGIPrepare(argv[1]); !error.empty()) {
		std::printf("FAIL: SSGI prepare: %s\n", error.c_str());
		++failures;
	}
	if (const auto error = CheckInverseSquareTileOverflow(argv[1]); !error.empty()) {
		std::printf("FAIL: ISL tile overflow: %s\n", error.c_str());
		++failures;
	}
	if (const auto error = VerifySubstrateABI(argv[1]); !error.empty()) {
		std::printf("FAIL: substrate ABI: %s\n", error.c_str());
		++failures;
	}
	if (const auto error = VerifySSSReceiverGate(argv[1]); !error.empty()) {
		std::printf("FAIL: SSS receiver gate: %s\n", error.c_str());
		++failures;
	}
	if (const auto error = VerifySSGIABI(argv[1]); !error.empty()) {
		std::printf("FAIL: SSGI ABI: %s\n", error.c_str());
		++failures;
	}
	if (const auto error = VerifyCubemapCaptureBoundary(argv[1]); !error.empty()) {
		std::printf("FAIL: cubemap capture boundary: %s\n", error.c_str());
		++failures;
	}
	for (const auto& job : jobs) {
		if (const auto error = Compile(job, argv[1]); !error.empty()) {
			std::printf(
				"FAIL: %s: %s\n%s\n",
				job.description,
				job.path.string().c_str(),
				error.c_str());
			++failures;
		}
	}

	if (failures == 0)
		std::printf("ShaderCompile passed (%zu focused jobs)\n", jobs.size());
	return failures == 0 ? 0 : 1;
}
