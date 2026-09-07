#include <array>
#include <cstring>
#include <cstdint>
#include <iostream>

#include <d3d11.h>
#include <winrt/base.h>

#include "UpscalingPublication.h"
#include "ProviderOutputPreview.h"

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
	ok &= CheckDepthSnapshot(device.get(), context.get());
	return ok ? 0 : 1;
}
