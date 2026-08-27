#include <array>
#include <cstring>
#include <cstdint>
#include <iostream>

#include <d3d11.h>
#include <winrt/base.h>

#include "UpscalingPublication.h"

namespace
{
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

	bool ReadPixel(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_source,
		std::array<std::uint8_t, 4>& a_pixel)
	{
		D3D11_TEXTURE2D_DESC desc{};
		a_source->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		winrt::com_ptr<ID3D11Texture2D> staging;
		if (FAILED(a_device->CreateTexture2D(&desc, nullptr, staging.put()))) {
			return false;
		}
		cs::engine::CopyResourcePreservingOM(a_context, staging.get(), a_source);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
			return false;
		}
		std::memcpy(a_pixel.data(), mapped.pData, a_pixel.size());
		a_context->Unmap(staging.get(), 0);
		return true;
	}

	bool IsRenderTargetBound(
		ID3D11DeviceContext* a_context,
		ID3D11RenderTargetView* a_expected)
	{
		winrt::com_ptr<ID3D11RenderTargetView> bound;
		a_context->OMGetRenderTargets(1, bound.put(), nullptr);
		return bound.get() == a_expected;
	}
}

int main()
{
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

	ok &= Check(
		cs::features::PublishUpscalingOutput(
			context.get(),
			frameBuffer.get(),
			providerOutput.get(),
			true),
		"successful provider dispatch was not published");
	ok &= Check(
		ReadPixel(device.get(), context.get(), frameBuffer.get(), observed) &&
			observed == providerPixel,
		"successful provider dispatch did not reach framebuffer");
	ok &= Check(
		IsRenderTargetBound(context.get(), frameBufferRTV.get()),
		"successful publication changed OM binding");

	context->OMSetRenderTargets(0, nullptr, nullptr);
	return ok ? 0 : 1;
}
