#include <array>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <iostream>

#include <DirectXPackedVector.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <winrt/base.h>

#include "UpscalingPublication.h"
#include "ProviderOutputPreview.h"
#include "Render/RenderUIPathGate.h"
#include "Render/TemporalRenderSettings.h"

namespace
{
	using cs::render::temporal::IsExternalUpscaler;
	using cs::render::temporal::UpscaleMethod;
	static_assert(!IsExternalUpscaler(UpscaleMethod::kNONE));
	static_assert(!IsExternalUpscaler(UpscaleMethod::kTAA));
	static_assert(IsExternalUpscaler(UpscaleMethod::kFSR));
	static_assert(IsExternalUpscaler(UpscaleMethod::kDLSS));
	static_assert(!IsExternalUpscaler(UpscaleMethod::kCount));

	bool Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
		}
		return a_condition;
	}

	winrt::com_ptr<ID3D11Texture2D> CreateTexture(
		ID3D11Device* a_device,
		UINT a_bindFlags,
		const std::array<std::uint8_t, 4>& a_pixel)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = 1;
		desc.Height = 1;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = a_bindFlags;

		D3D11_SUBRESOURCE_DATA initialData{};
		initialData.pSysMem = a_pixel.data();
		initialData.SysMemPitch = static_cast<UINT>(a_pixel.size());

		winrt::com_ptr<ID3D11Texture2D> texture;
		if (FAILED(a_device->CreateTexture2D(&desc, &initialData, texture.put()))) {
			return {};
		}
		return texture;
	}

	struct ShaderTexture
	{
		winrt::com_ptr<ID3D11Texture2D> resource;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
	};

	ShaderTexture CreateShaderTexture(
		ID3D11Device* a_device,
		UINT a_width,
		UINT a_height,
		DXGI_FORMAT a_format,
		UINT a_bindFlags,
		const void* a_pixels = nullptr,
		UINT a_rowPitch = 0)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = a_format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = a_bindFlags;

		D3D11_SUBRESOURCE_DATA initialData{};
		initialData.pSysMem = a_pixels;
		initialData.SysMemPitch = a_rowPitch;

		ShaderTexture texture;
		if (FAILED(a_device->CreateTexture2D(
				&desc,
				a_pixels ? &initialData : nullptr,
				texture.resource.put()))) {
			return {};
		}
		if ((a_bindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 &&
			FAILED(a_device->CreateShaderResourceView(
				texture.resource.get(), nullptr, texture.srv.put()))) {
			return {};
		}
		if ((a_bindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0 &&
			FAILED(a_device->CreateUnorderedAccessView(
				texture.resource.get(), nullptr, texture.uav.put()))) {
			return {};
		}
		return texture;
	}

	bool ReadTextureData(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_source,
		void* a_destination,
		std::size_t a_destinationSize,
		std::size_t a_rowSize)
	{
		D3D11_TEXTURE2D_DESC desc{};
		a_source->GetDesc(&desc);
		if (a_destinationSize != a_rowSize * desc.Height) {
			return false;
		}
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;

		winrt::com_ptr<ID3D11Texture2D> staging;
		if (FAILED(a_device->CreateTexture2D(&desc, nullptr, staging.put()))) {
			return false;
		}
		cs::engine::CopyResourcePreservingOM(a_context, staging.get(), a_source);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
			return false;
		}
		if (a_rowSize > mapped.RowPitch) {
			a_context->Unmap(staging.get(), 0);
			return false;
		}
		auto* destination = static_cast<std::byte*>(a_destination);
		for (UINT y = 0; y < desc.Height; ++y) {
			std::memcpy(
				destination + y * a_rowSize,
				static_cast<const std::byte*>(mapped.pData) + y * mapped.RowPitch,
				a_rowSize);
		}
		a_context->Unmap(staging.get(), 0);
		return true;
	}

	bool ReadPixel(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_source,
		std::array<std::uint8_t, 4>& a_pixel)
	{
		return ReadTextureData(
			a_device,
			a_context,
			a_source,
			a_pixel.data(),
			a_pixel.size(),
			a_pixel.size());
	}

	bool IsRenderTargetBound(
		ID3D11DeviceContext* a_context,
		ID3D11RenderTargetView* a_expected)
	{
		winrt::com_ptr<ID3D11RenderTargetView> bound;
		a_context->OMGetRenderTargets(1, bound.put(), nullptr);
		return bound.get() == a_expected;
	}

	bool CheckDepthSnapshot(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		D3D11_TEXTURE2D_DESC description{};
		description.Width = 1;
		description.Height = 1;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R24G8_TYPELESS;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		winrt::com_ptr<ID3D11Texture2D> source;
		winrt::com_ptr<ID3D11Texture2D> snapshot;
		if (FAILED(a_device->CreateTexture2D(&description, nullptr, source.put())))
			return Check(false, "could not create source depth map");
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(a_device->CreateTexture2D(&description, nullptr, snapshot.put())))
			return Check(false, "could not create raw depth snapshot");

		D3D11_DEPTH_STENCIL_VIEW_DESC depthDescription{};
		depthDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		winrt::com_ptr<ID3D11DepthStencilView> depth;
		if (FAILED(a_device->CreateDepthStencilView(source.get(), &depthDescription, depth.put())))
			return Check(false, "could not create source DSV");
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
		viewDescription.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		viewDescription.Texture2D.MipLevels = 1;
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		if (FAILED(a_device->CreateShaderResourceView(snapshot.get(), &viewDescription, view.put())))
			return Check(false, "could not create a sampleable raw depth snapshot view");

		a_context->ClearDepthStencilView(depth.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.25f, 0);
		cs::engine::CopyResourcePreservingOM(a_context, snapshot.get(), source.get());
		std::array<std::uint8_t, 4> captured{};
		if (!ReadPixel(a_device, a_context, snapshot.get(), captured))
			return Check(false, "could not read captured depth");
		a_context->ClearDepthStencilView(depth.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.75f, 0);
		std::array<std::uint8_t, 4> live{};
		std::array<std::uint8_t, 4> unchanged{};
		if (!ReadPixel(a_device, a_context, source.get(), live) ||
			!ReadPixel(a_device, a_context, snapshot.get(), unchanged))
			return Check(false, "could not compare live and captured depth");
		bool ok = Check(captured != live && unchanged == captured,
			"producer changes must not alter the raw snapshot");
		cs::engine::CopyResourcePreservingOM(a_context, snapshot.get(), source.get());
		ok &= Check(ReadPixel(a_device, a_context, snapshot.get(), unchanged) && unchanged == live,
			"refresh must update the shared raw snapshot");
		return ok;
	}

	bool TestRenderUIPathGateDecoder()
	{
		std::array<std::uint8_t, 32> image{};
		constexpr std::size_t gateOffset = 16;
		image[0] = 0x80;
		image[1] = 0x3D;
		const auto displacement =
			static_cast<std::int32_t>(gateOffset - cs::engine::RenderUIPathGate::kInstructionLength);
		std::memcpy(image.data() + 2, &displacement, sizeof(displacement));
		image[6] = 0x00;
		image[gateOffset] = 1;

		const auto imageAddress =
			reinterpret_cast<std::uintptr_t>(image.data());
		const auto expectedTarget = imageAddress + gateOffset;
		const auto decoded = cs::engine::RenderUIPathGate::Decode(
			imageAddress,
			imageAddress,
			image.size(),
			expectedTarget);
		bool ok = Check(
			decoded && decoded->Address() == expectedTarget &&
				decoded->TakesFullEffectsPath(),
			"Render_UI gate decoder accepted the validated RIP-relative byte compare");

		image[gateOffset] = 0;
		ok &= Check(
			decoded && !decoded->TakesFullEffectsPath(),
			"Render_UI gate accessor observes the live Gamma-only selection");

		image[0] = 0x81;
		ok &= Check(
			!cs::engine::RenderUIPathGate::Decode(
				imageAddress, imageAddress, image.size(), expectedTarget),
			"Render_UI gate decoder rejects an unexpected opcode");
		image[0] = 0x80;
		image[6] = 1;
		ok &= Check(
			!cs::engine::RenderUIPathGate::Decode(
				imageAddress, imageAddress, image.size(), expectedTarget),
			"Render_UI gate decoder rejects a nonzero compare immediate");
		image[6] = 0;
		ok &= Check(
			!cs::engine::RenderUIPathGate::Decode(
				imageAddress, imageAddress, gateOffset, expectedTarget),
			"Render_UI gate decoder rejects a target outside the validated data range");
		ok &= Check(
			!cs::engine::RenderUIPathGate::Decode(
				imageAddress, imageAddress, image.size(), expectedTarget + 1),
			"Render_UI gate decoder rejects an unexpected runtime target");
		ok &= Check(
			cs::engine::ClassifyRenderUIOutputExtent(
				0.0f, 0.0f, 3840.0f, 2160.0f, 2560, 1440, 3840, 2160) ==
				cs::engine::RenderUIOutputExtent::kFull,
			"a full-size Gamma output selects passthrough");
		ok &= Check(
			cs::engine::ClassifyRenderUIOutputExtent(
				0.0f, 0.0f, 2560.0f, 1440.0f, 2560, 1440, 3840, 2160) ==
				cs::engine::RenderUIOutputExtent::kCommitted,
			"a committed subrect Gamma output selects spatial recovery");
		ok &= Check(
			cs::engine::ClassifyRenderUIOutputExtent(
				0.0f, 0.0f, 3840.0f, 2160.0f, 3840, 2160, 3840, 2160) ==
				cs::engine::RenderUIOutputExtent::kFull,
			"native-size output is not double-scaled");
		ok &= Check(
			cs::engine::ClassifyRenderUIOutputExtent(
				1.0f, 0.0f, 2560.0f, 1440.0f, 2560, 1440, 3840, 2160) ==
				cs::engine::RenderUIOutputExtent::kInvalid,
			"an offset or unknown output extent is rejected");
		return ok;
	}

	winrt::com_ptr<ID3DBlob> CompileShader(
		const std::filesystem::path& a_path,
		const D3D_SHADER_MACRO* a_defines,
		const char* a_target)
	{
		winrt::com_ptr<ID3DBlob> bytecode;
		winrt::com_ptr<ID3DBlob> errors;
		if (FAILED(D3DCompileFromFile(
				a_path.c_str(),
				a_defines,
				D3D_COMPILE_STANDARD_FILE_INCLUDE,
				"main",
				a_target,
				0,
				0,
				bytecode.put(),
				errors.put()))) {
			if (errors) {
				std::cerr << static_cast<const char*>(errors->GetBufferPointer()) << '\n';
			}
			return {};
		}
		return bytecode;
	}

	bool TestFsrEncodeShader(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		const std::filesystem::path& a_computeShaderPath)
	{
		const D3D_SHADER_MACRO defines[]{
			{ "FO4CS_SUBSTRATE", "1" },
			{ "FSR", "1" },
			{ "DEPTH_OUTPUT", "1" },
			{ nullptr, nullptr }
		};
		auto bytecode = CompileShader(a_computeShaderPath, defines, "cs_5_0");
		if (!Check(
				bytecode.get() != nullptr,
				"could not compile the production FSR encode shader")) {
			return false;
		}

		winrt::com_ptr<ID3D11ComputeShader> shader;
		if (!Check(
				SUCCEEDED(a_device->CreateComputeShader(
					bytecode->GetBufferPointer(),
					bytecode->GetBufferSize(),
					nullptr,
					shader.put())),
				"could not create the production FSR encode compute shader")) {
			return false;
		}

		using DirectX::PackedVector::XMHALF2;
		constexpr UINT width = 3;
		constexpr UINT height = 2;
		constexpr std::size_t pixelCount = width * height;
		const std::array<XMHALF2, pixelCount> motionInput{
			XMHALF2(0.25f, -0.5f),
			XMHALF2(-0.75f, 0.125f),
			XMHALF2(0.375f, 0.625f),
			XMHALF2(-0.25f, -0.375f),
			XMHALF2(0.5f, -0.125f),
			XMHALF2(-0.625f, 0.25f)
		};
		const std::array<float, pixelCount> depthInput{
			0.75f, 0.25f, 0.5f, 0.125f, 0.625f, 0.875f
		};
		const XMHALF2 motionSentinel(0.0625f, -0.0625f);
		std::array<XMHALF2, pixelCount> initialMotionOutput;
		initialMotionOutput.fill(motionSentinel);
		constexpr float depthSentinel = -1.0f;
		std::array<float, pixelCount> initialDepthOutput;
		initialDepthOutput.fill(depthSentinel);
		const std::array<std::uint16_t, pixelCount> taaInput{};
		const std::array<std::uint32_t, pixelCount> normalsInput{};

		const auto taa = CreateShaderTexture(
			a_device,
			width,
			height,
			DXGI_FORMAT_R8G8_UNORM,
			D3D11_BIND_SHADER_RESOURCE,
			taaInput.data(),
			static_cast<UINT>(width * sizeof(taaInput[0])));
		const auto normals = CreateShaderTexture(
			a_device,
			width,
			height,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			D3D11_BIND_SHADER_RESOURCE,
			normalsInput.data(),
			static_cast<UINT>(width * sizeof(normalsInput[0])));
		const auto motionSource = CreateShaderTexture(
			a_device,
			width,
			height,
			DXGI_FORMAT_R16G16_FLOAT,
			D3D11_BIND_SHADER_RESOURCE,
			motionInput.data(),
			static_cast<UINT>(width * sizeof(motionInput[0])));
		const auto depthSource = CreateShaderTexture(
			a_device,
			width,
			height,
			DXGI_FORMAT_R32_FLOAT,
			D3D11_BIND_SHADER_RESOURCE,
			depthInput.data(),
			static_cast<UINT>(width * sizeof(depthInput[0])));
		const auto reactiveOutput = CreateShaderTexture(
			a_device,
			width,
			height,
			DXGI_FORMAT_R8_UNORM,
			D3D11_BIND_UNORDERED_ACCESS);
		const auto transparencyOutput = CreateShaderTexture(
			a_device,
			width,
			height,
			DXGI_FORMAT_R8_UNORM,
			D3D11_BIND_UNORDERED_ACCESS);
		const auto motionOutput = CreateShaderTexture(
			a_device,
			width,
			height,
			DXGI_FORMAT_R16G16_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS,
			initialMotionOutput.data(),
			static_cast<UINT>(width * sizeof(initialMotionOutput[0])));
		const auto depthOutput = CreateShaderTexture(
			a_device,
			width,
			height,
			DXGI_FORMAT_R32_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS,
			initialDepthOutput.data(),
			static_cast<UINT>(width * sizeof(initialDepthOutput[0])));
		if (!Check(
				taa.resource && taa.srv &&
					normals.resource && normals.srv &&
					motionSource.resource && motionSource.srv &&
					depthSource.resource && depthSource.srv &&
					reactiveOutput.resource && reactiveOutput.uav &&
					transparencyOutput.resource && transparencyOutput.uav &&
					motionOutput.resource && motionOutput.uav &&
					depthOutput.resource && depthOutput.uav,
				"could not create real D3D11 FSR encode resources")) {
			return false;
		}

		constexpr std::array<float, 4> constants{ 2.0f, 1.0f, 0.0f, 0.0f };
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = static_cast<UINT>(sizeof(constants));
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		D3D11_SUBRESOURCE_DATA bufferData{};
		bufferData.pSysMem = constants.data();
		winrt::com_ptr<ID3D11Buffer> constantBuffer;
		if (!Check(
				SUCCEEDED(a_device->CreateBuffer(
					&bufferDesc, &bufferData, constantBuffer.put())),
				"could not create the FSR encode TrueSamplingDim buffer")) {
			return false;
		}

		ID3D11ShaderResourceView* views[]{
			taa.srv.get(),
			normals.srv.get(),
			motionSource.srv.get(),
			depthSource.srv.get()
		};
		ID3D11UnorderedAccessView* outputs[]{
			reactiveOutput.uav.get(),
			transparencyOutput.uav.get(),
			motionOutput.uav.get(),
			depthOutput.uav.get()
		};
		auto* constantBufferPointer = constantBuffer.get();
		a_context->CSSetShaderResources(0, static_cast<UINT>(std::size(views)), views);
		a_context->CSSetUnorderedAccessViews(
			0, static_cast<UINT>(std::size(outputs)), outputs, nullptr);
		a_context->CSSetConstantBuffers(0, 1, &constantBufferPointer);
		a_context->CSSetShader(shader.get(), nullptr, 0);
		a_context->Dispatch(1, 1, 1);

		ID3D11ShaderResourceView* nullViews[std::size(views)]{};
		ID3D11UnorderedAccessView* nullOutputs[std::size(outputs)]{};
		ID3D11Buffer* nullBuffer = nullptr;
		a_context->CSSetShaderResources(
			0, static_cast<UINT>(std::size(nullViews)), nullViews);
		a_context->CSSetUnorderedAccessViews(
			0, static_cast<UINT>(std::size(nullOutputs)), nullOutputs, nullptr);
		a_context->CSSetConstantBuffers(0, 1, &nullBuffer);
		a_context->CSSetShader(nullptr, nullptr, 0);

		std::array<XMHALF2, pixelCount> observedMotion{};
		std::array<float, pixelCount> observedDepth{};
		if (!Check(
				ReadTextureData(
					a_device,
					a_context,
					motionOutput.resource.get(),
					observedMotion.data(),
					sizeof(observedMotion),
					width * sizeof(observedMotion[0])),
				"could not read the FSR encoded motion output") ||
			!Check(
				ReadTextureData(
					a_device,
					a_context,
					depthOutput.resource.get(),
					observedDepth.data(),
					sizeof(observedDepth),
					width * sizeof(observedDepth[0])),
				"could not read the FSR typed depth output")) {
			return false;
		}

		bool ok = true;
		ok &= Check(
			observedMotion[0].v == motionInput[0].v,
			"FSR encode changed the signed +X/-Y motion sample");
		ok &= Check(
			observedMotion[1].v == motionInput[1].v,
			"FSR encode changed the signed -X/+Y motion sample");
		ok &= Check(
			observedDepth[0] == depthInput[0] &&
				observedDepth[1] == depthInput[1],
			"FSR encode did not publish the typed depth snapshot");

		bool motionBoundsPreserved = true;
		bool depthBoundsPreserved = true;
		for (std::size_t index = 2; index < pixelCount; ++index) {
			motionBoundsPreserved &=
				observedMotion[index].v == motionSentinel.v;
			depthBoundsPreserved &= observedDepth[index] == depthSentinel;
		}
		ok &= Check(
			motionBoundsPreserved,
			"FSR encode wrote motion outside TrueSamplingDim");
		ok &= Check(
			depthBoundsPreserved,
			"FSR encode wrote depth outside TrueSamplingDim");
		return ok;
	}

	bool TestSpatialFallback(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		const std::filesystem::path& a_pixelShaderPath,
		const std::filesystem::path& a_vertexShaderPath)
	{
		const D3D_SHADER_MACRO vertexDefines[]{
			{ "VSHADER", "1" },
			{ nullptr, nullptr }
		};
		auto vertexBytecode =
			CompileShader(a_vertexShaderPath, vertexDefines, "vs_5_0");
		auto pixelBytecode =
			CompileShader(a_pixelShaderPath, nullptr, "ps_5_0");
		if (!vertexBytecode || !pixelBytecode) {
			return false;
		}

		winrt::com_ptr<ID3D11VertexShader> vertexShader;
		winrt::com_ptr<ID3D11PixelShader> pixelShader;
		if (FAILED(a_device->CreateVertexShader(
				vertexBytecode->GetBufferPointer(),
				vertexBytecode->GetBufferSize(),
				nullptr,
				vertexShader.put())) ||
			FAILED(a_device->CreatePixelShader(
				pixelBytecode->GetBufferPointer(),
				pixelBytecode->GetBufferSize(),
				nullptr,
				pixelShader.put()))) {
			return false;
		}

		constexpr UINT width = 4;
		constexpr UINT height = 4;
		constexpr std::array<std::uint8_t, 4> active{ 200, 10, 20, 255 };
		constexpr std::array<std::uint8_t, 4> stale{ 5, 240, 6, 255 };
		std::array<std::uint8_t, width * height * 4> sourcePixels{};
		for (UINT y = 0; y < height; ++y) {
			for (UINT x = 0; x < width; ++x) {
				const auto& color = x < 2 && y < 2 ? active : stale;
				std::memcpy(
					sourcePixels.data() + (y * width + x) * 4,
					color.data(),
					color.size());
			}
		}

		D3D11_TEXTURE2D_DESC sourceDesc{};
		sourceDesc.Width = width;
		sourceDesc.Height = height;
		sourceDesc.MipLevels = 1;
		sourceDesc.ArraySize = 1;
		sourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sourceDesc.SampleDesc.Count = 1;
		sourceDesc.Usage = D3D11_USAGE_DEFAULT;
		sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA sourceData{};
		sourceData.pSysMem = sourcePixels.data();
		sourceData.SysMemPitch = width * 4;
		winrt::com_ptr<ID3D11Texture2D> source;
		winrt::com_ptr<ID3D11ShaderResourceView> sourceView;
		if (FAILED(a_device->CreateTexture2D(
				&sourceDesc, &sourceData, source.put())) ||
			FAILED(a_device->CreateShaderResourceView(
				source.get(), nullptr, sourceView.put()))) {
			return false;
		}

		auto targetDesc = sourceDesc;
		targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		winrt::com_ptr<ID3D11Texture2D> target;
		winrt::com_ptr<ID3D11RenderTargetView> targetView;
		if (FAILED(a_device->CreateTexture2D(
				&targetDesc, nullptr, target.put())) ||
			FAILED(a_device->CreateRenderTargetView(
				target.get(), nullptr, targetView.put()))) {
			return false;
		}

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		winrt::com_ptr<ID3D11SamplerState> sampler;
		if (FAILED(a_device->CreateSamplerState(&samplerDesc, sampler.put()))) {
			return false;
		}

		constexpr std::array<float, 4> constants{ 2.0f, 2.0f, 0.0f, 0.0f };
		D3D11_BUFFER_DESC bufferDesc{};
		bufferDesc.ByteWidth = sizeof(constants);
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		D3D11_SUBRESOURCE_DATA bufferData{};
		bufferData.pSysMem = constants.data();
		winrt::com_ptr<ID3D11Buffer> constantBuffer;
		if (FAILED(a_device->CreateBuffer(
				&bufferDesc, &bufferData, constantBuffer.put()))) {
			return false;
		}

		const D3D11_VIEWPORT viewport{
			.TopLeftX = 0.0f,
			.TopLeftY = 0.0f,
			.Width = static_cast<float>(width),
			.Height = static_cast<float>(height),
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f
		};
		auto* targetViewPointer = targetView.get();
		auto* sourceViewPointer = sourceView.get();
		auto* samplerPointer = sampler.get();
		auto* constantBufferPointer = constantBuffer.get();
		a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		a_context->VSSetShader(vertexShader.get(), nullptr, 0);
		a_context->RSSetViewports(1, &viewport);
		a_context->OMSetRenderTargets(1, &targetViewPointer, nullptr);
		a_context->PSSetConstantBuffers(0, 1, &constantBufferPointer);
		a_context->PSSetShaderResources(0, 1, &sourceViewPointer);
		a_context->PSSetSamplers(0, 1, &samplerPointer);
		a_context->PSSetShader(pixelShader.get(), nullptr, 0);
		a_context->Draw(3, 0);

		ID3D11ShaderResourceView* nullView = nullptr;
		a_context->PSSetShaderResources(0, 1, &nullView);
		a_context->OMSetRenderTargets(0, nullptr, nullptr);

		auto stagingDesc = targetDesc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		winrt::com_ptr<ID3D11Texture2D> staging;
		if (FAILED(a_device->CreateTexture2D(
				&stagingDesc, nullptr, staging.put()))) {
			return false;
		}
		a_context->CopyResource(staging.get(), target.get());
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
			return false;
		}
		bool valid = true;
		for (UINT y = 0; y < height; ++y) {
			const auto* row =
				static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
			for (UINT x = 0; x < width; ++x) {
				valid &= std::abs(static_cast<int>(row[x * 4]) - active[0]) <= 1;
				valid &= std::abs(static_cast<int>(row[x * 4 + 1]) - active[1]) <= 1;
				valid &= std::abs(static_cast<int>(row[x * 4 + 2]) - active[2]) <= 1;
			}
		}
		a_context->Unmap(staging.get(), 0);
		return valid;
	}
}

int main(int argc, char** argv)
{
	if (!Check(
			argc == 4,
			"expected spatial fallback pixel/vertex and FSR encode shaders")) {
		return 1;
	}
	constexpr D3D_FEATURE_LEVEL featureLevels[]{ D3D_FEATURE_LEVEL_11_0 };
	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	const HRESULT deviceResult = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_WARP,
		nullptr,
		0,
		featureLevels,
		static_cast<UINT>(std::size(featureLevels)),
		D3D11_SDK_VERSION,
		device.put(),
		nullptr,
		context.put());
	if (!Check(SUCCEEDED(deviceResult) && device && context, "could not create WARP device")) {
		return 1;
	}

	constexpr std::array<std::uint8_t, 4> nativePixel{ 1, 2, 3, 4 };
	constexpr std::array<std::uint8_t, 4> providerPixel{ 10, 20, 30, 40 };
	auto frameBuffer = CreateTexture(device.get(), D3D11_BIND_RENDER_TARGET, nativePixel);
	auto providerOutput = CreateTexture(device.get(), D3D11_BIND_SHADER_RESOURCE, providerPixel);
	if (!Check(frameBuffer && providerOutput, "could not create publication textures")) {
		return 1;
	}

	winrt::com_ptr<ID3D11RenderTargetView> frameBufferRTV;
	if (!Check(
			SUCCEEDED(device->CreateRenderTargetView(
				frameBuffer.get(),
				nullptr,
				frameBufferRTV.put())),
			"could not create framebuffer RTV")) {
		return 1;
	}
	ID3D11RenderTargetView* renderTargets[]{ frameBufferRTV.get() };
	context->OMSetRenderTargets(1, renderTargets, nullptr);

	bool ok = true;
	ok &= TestRenderUIPathGateDecoder();
	ok &= Check(
		cs::features::GetProviderOutputPreviewViewFormat(
			DXGI_FORMAT_R8G8B8A8_UNORM) == DXGI_FORMAT_R8G8B8A8_UNORM,
		"typed RT0 format changed");
	ok &= Check(
		cs::features::GetProviderOutputPreviewViewFormat(
			DXGI_FORMAT_R8G8B8A8_TYPELESS) == DXGI_FORMAT_R8G8B8A8_UNORM,
		"RGBA8 typeless RT0 did not select an UNORM view");
	ok &= Check(
		cs::features::GetProviderOutputPreviewViewFormat(
			DXGI_FORMAT_B8G8R8A8_TYPELESS) == DXGI_FORMAT_B8G8R8A8_UNORM,
		"BGRA8 typeless RT0 did not select an UNORM view");
	ok &= Check(
		cs::features::GetProviderOutputPreviewViewFormat(
			DXGI_FORMAT_R16G16B16A16_TYPELESS) == DXGI_FORMAT_R16G16B16A16_FLOAT,
		"RGBA16 typeless RT0 did not select a FLOAT view");
	ok &= Check(
		cs::features::GetProviderOutputPreviewViewFormat(
			DXGI_FORMAT_R8_TYPELESS) == DXGI_FORMAT_UNKNOWN,
		"ambiguous typeless format was accepted");
	ok &= Check(
		!cs::features::PublishUpscalingOutput(
			context.get(),
			frameBuffer.get(),
			providerOutput.get(),
			false),
		"failed provider dispatch published output");
	std::array<std::uint8_t, 4> observed{};
	ok &= Check(
		ReadPixel(device.get(), context.get(), frameBuffer.get(), observed) &&
			observed == nativePixel,
		"failed provider dispatch changed framebuffer");
	ok &= Check(
		IsRenderTargetBound(context.get(), frameBufferRTV.get()),
		"failed publication path changed OM binding");

	auto passthroughOutput = CreateTexture(device.get(), 0, nativePixel);
	ok &= Check(
		passthroughOutput &&
			cs::features::PrepareUpscalingPassthrough(
				context.get(),
				passthroughOutput.get(),
				providerOutput.get()),
		"full-extent recovery did not prepare its private passthrough output");
	ok &= Check(
		cs::features::PublishUpscalingOutput(
			context.get(),
			frameBuffer.get(),
			passthroughOutput.get(),
			true),
		"prepared full-extent passthrough was not published");
	ok &= Check(
		ReadPixel(device.get(), context.get(), frameBuffer.get(), observed) &&
			observed == providerPixel,
		"successful provider dispatch did not reach framebuffer");
	ok &= Check(
		IsRenderTargetBound(context.get(), frameBufferRTV.get()),
		"successful publication changed OM binding");

	ok &= Check(
		TestSpatialFallback(device.get(), context.get(), argv[1], argv[2]),
		"spatial recovery sampled stale pixels outside the committed render subrect");
	ok &= TestFsrEncodeShader(device.get(), context.get(), argv[3]);

	context->OMSetRenderTargets(0, nullptr, nullptr);
	ok &= CheckDepthSnapshot(device.get(), context.get());
	return ok ? 0 : 1;
}
