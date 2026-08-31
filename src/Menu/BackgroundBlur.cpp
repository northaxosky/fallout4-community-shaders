// Separable Gaussian blur after Unrimp by Christian Ofenberg (https://github.com/cofenberg/unrimp), MIT.

#include "Menu/BackgroundBlur.h"

#include "Log.h"
#include "Menu/Menu.h"
#include "Render/Annotation.h"
#include "Utils/CSUtil.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <string_view>

#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <wrl/client.h>

namespace
{
	auto* L = cs::log::Get("menu");

	// Heavily downscaled blur is very sensitive; this value matches upstream's tuning.
	constexpr float BLUR_INTENSITY = 0.03f;
	constexpr UINT DOWNSAMPLE_FACTOR = 8;
	constexpr float BLUR_RADIUS_SCALE = 10.0f;
	constexpr int BLUR_SAMPLE_COUNT = 9;
	constexpr float SCISSOR_AA_PADDING = 2.0f;
	constexpr UINT FULLSCREEN_TRIANGLE_VERTICES = 3;
	static_assert(DOWNSAMPLE_FACTOR >= 2 && DOWNSAMPLE_FACTOR % 2 == 0);

	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	std::mutex resourceMutex;
	bool enabled = true;
	bool initialized = false;
	bool initializationFailed = false;

	ComPtr<ID3D11VertexShader> vertexShader;
	ComPtr<ID3D11PixelShader> downsamplePixelShader;
	ComPtr<ID3D11PixelShader> horizontalPixelShader;
	ComPtr<ID3D11PixelShader> verticalPixelShader;
	ComPtr<ID3D11PixelShader> compositePixelShader;
	ComPtr<ID3D11Buffer> constantBuffer;
	ComPtr<ID3D11Buffer> windowConstantBuffer;
	ComPtr<ID3D11SamplerState> samplerState;
	ComPtr<ID3D11BlendState> blendState;
	ComPtr<ID3D11DepthStencilState> depthStencilState;
	ComPtr<ID3D11RasterizerState> scissorRasterizerState;

	ComPtr<ID3D11Texture2D> downsampleTexture;
	ComPtr<ID3D11RenderTargetView> downsampleRTV;
	ComPtr<ID3D11ShaderResourceView> downsampleSRV;

	ComPtr<ID3D11Texture2D> blurTexture1;
	ComPtr<ID3D11Texture2D> blurTexture2;
	ComPtr<ID3D11RenderTargetView> blurRTV1;
	ComPtr<ID3D11RenderTargetView> blurRTV2;
	ComPtr<ID3D11ShaderResourceView> blurSRV1;
	ComPtr<ID3D11ShaderResourceView> blurSRV2;

	ComPtr<ID3D11Texture2D> sourceCopyTexture;
	ComPtr<ID3D11ShaderResourceView> cachedSourceSRV;
	ID3D11Texture2D* cachedSourceTexture = nullptr;

	UINT textureWidth = 0;
	UINT textureHeight = 0;
	UINT downsampledWidth = 0;
	UINT downsampledHeight = 0;
	DXGI_FORMAT textureFormat = DXGI_FORMAT_UNKNOWN;

	struct BlurConstants
	{
		float texelSize[4];  // 1/width, 1/height, unused, downsample factor
		int blurParams[4];   // sample count, unused x3
	};

	struct WindowConstants
	{
		float windowRect[4];    // minX, minY, maxX, maxY in pixels
		float windowParams[4];  // corner radius, screen width, screen height, fullscreen flag
	};

	class PipelineStateScope
	{
	public:
		explicit PipelineStateScope(ID3D11DeviceContext* a_context) :
			context(a_context)
		{
			context->OMGetRenderTargets(1, renderTarget.GetAddressOf(), depthStencilView.GetAddressOf());
			context->OMGetBlendState(blend.GetAddressOf(), blendFactor.data(), &sampleMask);
			context->OMGetDepthStencilState(depthStencil.GetAddressOf(), &stencilRef);
			context->RSGetState(rasterizer.GetAddressOf());
			context->RSGetViewports(&viewportCount, viewports.data());
			context->RSGetScissorRects(&scissorCount, scissors.data());
			context->IAGetInputLayout(inputLayout.GetAddressOf());
			context->IAGetPrimitiveTopology(&topology);
			context->VSGetShader(vertex.GetAddressOf(), nullptr, nullptr);
			context->PSGetShader(pixel.GetAddressOf(), nullptr, nullptr);
			context->GSGetShader(geometry.GetAddressOf(), nullptr, nullptr);
			context->HSGetShader(hull.GetAddressOf(), nullptr, nullptr);
			context->DSGetShader(domain.GetAddressOf(), nullptr, nullptr);

			ID3D11SamplerState* samplerRaw = nullptr;
			context->PSGetSamplers(0, 1, &samplerRaw);
			sampler.Attach(samplerRaw);

			ID3D11Buffer* bufferRaw[2]{};
			context->PSGetConstantBuffers(0, 2, bufferRaw);
			constantBuffers[0].Attach(bufferRaw[0]);
			constantBuffers[1].Attach(bufferRaw[1]);

			ID3D11ShaderResourceView* resourceRaw = nullptr;
			context->PSGetShaderResources(0, 1, &resourceRaw);
			shaderResource.Attach(resourceRaw);
		}

		~PipelineStateScope()
		{
			auto* renderTargetRaw = renderTarget.Get();
			auto* samplerRaw = sampler.Get();
			auto bufferRaw = std::array{ constantBuffers[0].Get(), constantBuffers[1].Get() };
			auto* resourceRaw = shaderResource.Get();

			context->OMSetRenderTargets(1, &renderTargetRaw, depthStencilView.Get());
			context->OMSetBlendState(blend.Get(), blendFactor.data(), sampleMask);
			context->OMSetDepthStencilState(depthStencil.Get(), stencilRef);
			context->RSSetState(rasterizer.Get());
			context->RSSetViewports(viewportCount, viewportCount > 0 ? viewports.data() : nullptr);
			context->RSSetScissorRects(scissorCount, scissorCount > 0 ? scissors.data() : nullptr);
			context->IASetInputLayout(inputLayout.Get());
			context->IASetPrimitiveTopology(topology);
			context->VSSetShader(vertex.Get(), nullptr, 0);
			context->PSSetShader(pixel.Get(), nullptr, 0);
			context->GSSetShader(geometry.Get(), nullptr, 0);
			context->HSSetShader(hull.Get(), nullptr, 0);
			context->DSSetShader(domain.Get(), nullptr, 0);
			context->PSSetSamplers(0, 1, &samplerRaw);
			context->PSSetConstantBuffers(0, 2, bufferRaw.data());
			context->PSSetShaderResources(0, 1, &resourceRaw);
		}

	private:
		ID3D11DeviceContext* context;
		ComPtr<ID3D11RenderTargetView> renderTarget;
		ComPtr<ID3D11DepthStencilView> depthStencilView;
		ComPtr<ID3D11BlendState> blend;
		std::array<float, 4> blendFactor{};
		UINT sampleMask = 0;
		ComPtr<ID3D11DepthStencilState> depthStencil;
		UINT stencilRef = 0;
		ComPtr<ID3D11RasterizerState> rasterizer;
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
		UINT viewportCount = static_cast<UINT>(viewports.size());
		std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
		UINT scissorCount = static_cast<UINT>(scissors.size());
		ComPtr<ID3D11InputLayout> inputLayout;
		D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ComPtr<ID3D11VertexShader> vertex;
		ComPtr<ID3D11PixelShader> pixel;
		ComPtr<ID3D11GeometryShader> geometry;
		ComPtr<ID3D11HullShader> hull;
		ComPtr<ID3D11DomainShader> domain;
		ComPtr<ID3D11SamplerState> sampler;
		std::array<ComPtr<ID3D11Buffer>, 2> constantBuffers;
		ComPtr<ID3D11ShaderResourceView> shaderResource;
	};

	void ReleaseBlurTextures()
	{
		downsampleTexture.Reset();
		downsampleRTV.Reset();
		downsampleSRV.Reset();
		blurTexture1.Reset();
		blurTexture2.Reset();
		blurRTV1.Reset();
		blurRTV2.Reset();
		blurSRV1.Reset();
		blurSRV2.Reset();

		textureWidth = 0;
		textureHeight = 0;
		downsampledWidth = 0;
		downsampledHeight = 0;
		textureFormat = DXGI_FORMAT_UNKNOWN;
	}

	bool CreateTextureSet(ID3D11Device* a_device, const D3D11_TEXTURE2D_DESC& a_desc,
		ComPtr<ID3D11Texture2D>& a_texture, ComPtr<ID3D11RenderTargetView>& a_rtv, ComPtr<ID3D11ShaderResourceView>& a_srv,
		const char* a_name)
	{
		if (FAILED(a_device->CreateTexture2D(&a_desc, nullptr, a_texture.ReleaseAndGetAddressOf()))) {
			L->error("BackgroundBlur: failed to create {} texture", a_name);
			return false;
		}
		const std::string baseName =
			"Menu/BackgroundBlur/" + std::string(a_name);
		cs::render::annotation::SetName(
			a_texture.Get(), baseName + ".Texture");
		if (FAILED(a_device->CreateRenderTargetView(a_texture.Get(), nullptr, a_rtv.ReleaseAndGetAddressOf()))) {
			L->error("BackgroundBlur: failed to create {} RTV", a_name);
			return false;
		}
		cs::render::annotation::SetName(
			a_rtv.Get(), baseName + ".RTV");
		if (FAILED(a_device->CreateShaderResourceView(a_texture.Get(), nullptr, a_srv.ReleaseAndGetAddressOf()))) {
			L->error("BackgroundBlur: failed to create {} SRV", a_name);
			return false;
		}
		cs::render::annotation::SetName(
			a_srv.Get(), baseName + ".SRV");
		return true;
	}

	void CreateBlurTextures(UINT a_width, UINT a_height, DXGI_FORMAT a_format)
	{
		if (a_width == textureWidth && a_height == textureHeight && a_format == textureFormat && blurTexture1 && blurTexture2)
			return;

		auto* device = cs::Menu::Get().GetDevice();
		if (!device)
			return;

		ReleaseBlurTextures();

		const UINT dsWidth = std::max(1u, a_width / DOWNSAMPLE_FACTOR);
		const UINT dsHeight = std::max(1u, a_height / DOWNSAMPLE_FACTOR);

		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = dsWidth;
		texDesc.Height = dsHeight;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = a_format;
		texDesc.SampleDesc.Count = 1;
		texDesc.Usage = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (!CreateTextureSet(device, texDesc, downsampleTexture, downsampleRTV, downsampleSRV, "Downsample") ||
			!CreateTextureSet(device, texDesc, blurTexture1, blurRTV1, blurSRV1, "BlurHorizontal") ||
			!CreateTextureSet(device, texDesc, blurTexture2, blurRTV2, blurSRV2, "BlurVertical")) {
			ReleaseBlurTextures();
			return;
		}

		textureWidth = a_width;
		textureHeight = a_height;
		downsampledWidth = dsWidth;
		downsampledHeight = dsHeight;
		textureFormat = a_format;
	}

	void PerformBlur(ID3D11DeviceContext* a_context, const D3D11_TEXTURE2D_DESC& a_sourceDesc,
		ID3D11ShaderResourceView* a_sourceSRV, ID3D11RenderTargetView* a_targetRTV,
		ImVec2 a_menuMin, ImVec2 a_menuMax, float a_cornerRadius)
	{
		if (!blurTexture1 || !blurTexture2)
			return;

		PipelineStateScope stateScope(a_context);

		auto* constantBufferPtr = constantBuffer.Get();
		auto* samplerStatePtr = samplerState.Get();
		ID3D11ShaderResourceView* nullSRV = nullptr;
		const float defaultBlendFactor[4]{};

		a_context->IASetInputLayout(nullptr);
		a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		a_context->GSSetShader(nullptr, nullptr, 0);
		a_context->HSSetShader(nullptr, nullptr, 0);
		a_context->DSSetShader(nullptr, nullptr, 0);
		a_context->OMSetBlendState(nullptr, defaultBlendFactor, 0xFFFFFFFF);
		a_context->OMSetDepthStencilState(depthStencilState.Get(), 0);
		a_context->RSSetState(scissorRasterizerState.Get());

		D3D11_VIEWPORT blurViewport{};
		blurViewport.Width = static_cast<FLOAT>(downsampledWidth);
		blurViewport.Height = static_cast<FLOAT>(downsampledHeight);
		blurViewport.MinDepth = 0.0f;
		blurViewport.MaxDepth = 1.0f;
		a_context->RSSetViewports(1, &blurViewport);
		const D3D11_RECT blurScissor{ 0, 0, static_cast<LONG>(downsampledWidth), static_cast<LONG>(downsampledHeight) };
		a_context->RSSetScissorRects(1, &blurScissor);

		auto* downsampleRTVPtr = downsampleRTV.Get();
		a_context->OMSetRenderTargets(1, &downsampleRTVPtr, nullptr);
		a_context->VSSetShader(vertexShader.Get(), nullptr, 0);
		a_context->PSSetShader(downsamplePixelShader.Get(), nullptr, 0);
		a_context->PSSetSamplers(0, 1, &samplerStatePtr);

		// A true box reduction prevents 8x aliasing from surviving the Gaussian passes.
		BlurConstants downsampleConstants{};
		downsampleConstants.texelSize[0] = 1.0f / static_cast<float>(a_sourceDesc.Width);
		downsampleConstants.texelSize[1] = 1.0f / static_cast<float>(a_sourceDesc.Height);
		downsampleConstants.texelSize[3] = static_cast<float>(DOWNSAMPLE_FACTOR);
		a_context->UpdateSubresource(constantBuffer.Get(), 0, nullptr, &downsampleConstants, 0, 0);
		a_context->PSSetConstantBuffers(0, 1, &constantBufferPtr);
		a_context->PSSetShaderResources(0, 1, &a_sourceSRV);
		a_context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);
		a_context->PSSetShaderResources(0, 1, &nullSRV);

		const float blurRadius = BLUR_INTENSITY * BLUR_RADIUS_SCALE;

		BlurConstants constants{};
		constants.texelSize[0] = blurRadius / static_cast<float>(downsampledWidth);
		constants.texelSize[1] = blurRadius / static_cast<float>(downsampledHeight);
		constants.texelSize[3] = static_cast<float>(DOWNSAMPLE_FACTOR);
		constants.blurParams[0] = BLUR_SAMPLE_COUNT;

		a_context->UpdateSubresource(constantBuffer.Get(), 0, nullptr, &constants, 0, 0);
		a_context->PSSetConstantBuffers(0, 1, &constantBufferPtr);

		auto* rtv1Ptr = blurRTV1.Get();
		auto* downsampleSRVPtr = downsampleSRV.Get();
		a_context->OMSetRenderTargets(1, &rtv1Ptr, nullptr);
		a_context->PSSetShader(horizontalPixelShader.Get(), nullptr, 0);
		a_context->PSSetShaderResources(0, 1, &downsampleSRVPtr);
		a_context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);

		a_context->PSSetShaderResources(0, 1, &nullSRV);
		auto* rtv2Ptr = blurRTV2.Get();
		auto* srv1Ptr = blurSRV1.Get();
		a_context->OMSetRenderTargets(1, &rtv2Ptr, nullptr);
		a_context->PSSetShader(verticalPixelShader.Get(), nullptr, 0);
		a_context->PSSetShaderResources(0, 1, &srv1Ptr);
		a_context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);
		a_context->PSSetShaderResources(0, 1, &nullSRV);

		D3D11_VIEWPORT targetViewport{};
		targetViewport.Width = static_cast<FLOAT>(a_sourceDesc.Width);
		targetViewport.Height = static_cast<FLOAT>(a_sourceDesc.Height);
		targetViewport.MinDepth = 0.0f;
		targetViewport.MaxDepth = 1.0f;
		a_context->RSSetViewports(1, &targetViewport);

		// Pad the scissor so anti-aliased rounded corners are not clipped.
		D3D11_RECT scissorRect;
		scissorRect.left = static_cast<LONG>(std::max(0.0f, a_menuMin.x - SCISSOR_AA_PADDING));
		scissorRect.top = static_cast<LONG>(std::max(0.0f, a_menuMin.y - SCISSOR_AA_PADDING));
		scissorRect.right = static_cast<LONG>(std::min(static_cast<float>(a_sourceDesc.Width), a_menuMax.x + SCISSOR_AA_PADDING));
		scissorRect.bottom = static_cast<LONG>(std::min(static_cast<float>(a_sourceDesc.Height), a_menuMax.y + SCISSOR_AA_PADDING));

		a_context->RSSetScissorRects(1, &scissorRect);

		const bool useRoundedCorners = compositePixelShader && windowConstantBuffer;
		if (useRoundedCorners) {
			WindowConstants windowConstants{};
			windowConstants.windowRect[0] = a_menuMin.x;
			windowConstants.windowRect[1] = a_menuMin.y;
			windowConstants.windowRect[2] = a_menuMax.x;
			windowConstants.windowRect[3] = a_menuMax.y;
			windowConstants.windowParams[0] = a_cornerRadius;
			windowConstants.windowParams[1] = static_cast<float>(a_sourceDesc.Width);
			windowConstants.windowParams[2] = static_cast<float>(a_sourceDesc.Height);
			windowConstants.windowParams[3] = 0.0f;
			a_context->UpdateSubresource(windowConstantBuffer.Get(), 0, nullptr, &windowConstants, 0, 0);
			auto* windowConstantBufferPtr = windowConstantBuffer.Get();
			a_context->PSSetConstantBuffers(1, 1, &windowConstantBufferPtr);
		}

		a_context->OMSetRenderTargets(1, &a_targetRTV, nullptr);
		a_context->OMSetBlendState(blendState.Get(), nullptr, 0xFFFFFFFF);
		a_context->PSSetShader(useRoundedCorners ? compositePixelShader.Get() : verticalPixelShader.Get(), nullptr, 0);
		auto* srv2Ptr = blurSRV2.Get();
		a_context->PSSetShaderResources(0, 1, &srv2Ptr);
		a_context->Draw(FULLSCREEN_TRIANGLE_VERTICES, 0);
		a_context->PSSetShaderResources(0, 1, &nullSRV);
	}
}

namespace cs::BackgroundBlur
{
	bool Initialize()
	{
		std::lock_guard lock(resourceMutex);

		if (initialized || initializationFailed)
			return initialized;

		auto* device = Menu::Get().GetDevice();
		if (!device) {
			initializationFailed = true;
			return false;
		}

		auto compileShader = [&](auto& a_shader, const wchar_t* a_path, const char* a_target, const char* a_entry, const char* a_name) -> bool {
			auto* compiled = util::CompileShader(a_path, {}, a_target, a_entry);
			if (!compiled) {
				L->error("BackgroundBlur: failed to compile {}", a_name);
				initializationFailed = true;
				return false;
			}
			a_shader.Attach(static_cast<std::remove_reference_t<decltype(*a_shader.Get())>*>(compiled));
			cs::render::annotation::SetName(compiled, a_name);
			return true;
		};

		if (!compileShader(vertexShader, L"Data\\Shaders\\Menu\\BackgroundBlurDownsample.hlsl", "vs_5_0", "VS_Main", "Menu/BackgroundBlur/Fullscreen.VS") ||
			!compileShader(downsamplePixelShader, L"Data\\Shaders\\Menu\\BackgroundBlurDownsample.hlsl", "ps_5_0", "PS_Main", "Menu/BackgroundBlur/Downsample.PS") ||
			!compileShader(horizontalPixelShader, L"Data\\Shaders\\Menu\\BackgroundBlurHorizontal.hlsl", "ps_5_0", "PS_Main", "Menu/BackgroundBlur/Horizontal.PS") ||
			!compileShader(verticalPixelShader, L"Data\\Shaders\\Menu\\BackgroundBlurVertical.hlsl", "ps_5_0", "PS_Main", "Menu/BackgroundBlur/Vertical.PS") ||
			!compileShader(compositePixelShader, L"Data\\Shaders\\Menu\\BackgroundBlurComposite.hlsl", "ps_5_0", "PS_Main", "Menu/BackgroundBlur/Composite.PS"))
			return false;

		auto checkCreate = [&](HRESULT a_hr, const char* a_name) -> bool {
			if (FAILED(a_hr)) {
				L->error("BackgroundBlur: failed to create {}", a_name);
				initializationFailed = true;
				return false;
			}
			return true;
		};

		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.Usage = D3D11_USAGE_DEFAULT;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		cbDesc.ByteWidth = sizeof(BlurConstants);
		if (!checkCreate(device->CreateBuffer(&cbDesc, nullptr, constantBuffer.GetAddressOf()), "blur constant buffer"))
			return false;
		cs::render::annotation::SetName(
			constantBuffer.Get(), "Menu/BackgroundBlur/BlurConstants.Buffer");

		cbDesc.ByteWidth = sizeof(WindowConstants);
		if (!checkCreate(device->CreateBuffer(&cbDesc, nullptr, windowConstantBuffer.GetAddressOf()), "window constant buffer"))
			return false;
		cs::render::annotation::SetName(
			windowConstantBuffer.Get(), "Menu/BackgroundBlur/WindowConstants.Buffer");

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (!checkCreate(device->CreateSamplerState(&samplerDesc, samplerState.GetAddressOf()), "blur sampler state"))
			return false;
		cs::render::annotation::SetName(
			samplerState.Get(), "Menu/BackgroundBlur/LinearClamp.Sampler");

		D3D11_BLEND_DESC blendDesc{};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (!checkCreate(device->CreateBlendState(&blendDesc, blendState.GetAddressOf()), "blur blend state"))
			return false;
		cs::render::annotation::SetName(
			blendState.Get(), "Menu/BackgroundBlur/Composite.BlendState");

		D3D11_DEPTH_STENCIL_DESC depthDesc{};
		depthDesc.DepthEnable = FALSE;
		depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		if (!checkCreate(device->CreateDepthStencilState(&depthDesc, depthStencilState.GetAddressOf()), "blur depth state"))
			return false;
		cs::render::annotation::SetName(
			depthStencilState.Get(), "Menu/BackgroundBlur/Disabled.DepthStencilState");

		D3D11_RASTERIZER_DESC rsDesc{};
		rsDesc.FillMode = D3D11_FILL_SOLID;
		rsDesc.CullMode = D3D11_CULL_NONE;
		rsDesc.FrontCounterClockwise = FALSE;
		rsDesc.DepthClipEnable = TRUE;
		rsDesc.ScissorEnable = TRUE;
		if (!checkCreate(device->CreateRasterizerState(&rsDesc, scissorRasterizerState.GetAddressOf()), "scissor rasterizer state"))
			return false;
		cs::render::annotation::SetName(
			scissorRasterizerState.Get(), "Menu/BackgroundBlur/Scissor.RasterizerState");

		initialized = true;
		return true;
	}

	void RenderBackgroundBlur()
	{
		std::lock_guard lock(resourceMutex);

		if (!enabled || !initialized || initializationFailed)
			return;

		auto& menu = Menu::Get();
		auto* device = menu.GetDevice();
		auto* context = menu.GetContext();
		auto* targetRTV = menu.GetBackbufferRTV();
		if (!device || !context || !targetRTV)
			return;

		ComPtr<ID3D11Resource> targetResource;
		targetRTV->GetResource(targetResource.GetAddressOf());
		ComPtr<ID3D11Texture2D> targetTexture;
		if (FAILED(targetResource.As(&targetTexture)) || !targetTexture)
			return;

		// The backbuffer is not shader-readable, so blur a copy of it instead.
		D3D11_TEXTURE2D_DESC targetDesc{};
		targetTexture->GetDesc(&targetDesc);

		if (cachedSourceTexture != targetTexture.Get() ||
			textureWidth != targetDesc.Width || textureHeight != targetDesc.Height || textureFormat != targetDesc.Format) {
			cachedSourceSRV.Reset();
			sourceCopyTexture.Reset();
			cachedSourceTexture = nullptr;

			D3D11_TEXTURE2D_DESC copyDesc = targetDesc;
			copyDesc.Usage = D3D11_USAGE_DEFAULT;
			copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			copyDesc.CPUAccessFlags = 0;
			copyDesc.MiscFlags = 0;
			copyDesc.MipLevels = 1;
			copyDesc.ArraySize = 1;
			copyDesc.SampleDesc.Count = 1;

			if (FAILED(device->CreateTexture2D(&copyDesc, nullptr, sourceCopyTexture.GetAddressOf()))) {
				L->error("BackgroundBlur: failed to create the backbuffer copy");
				initializationFailed = true;
				return;
			}
			cs::render::annotation::SetName(
				sourceCopyTexture.Get(), "Menu/BackgroundBlur/SourceCopy.Texture");
			if (FAILED(device->CreateShaderResourceView(sourceCopyTexture.Get(), nullptr, cachedSourceSRV.GetAddressOf()))) {
				L->error("BackgroundBlur: failed to create the backbuffer copy SRV");
				initializationFailed = true;
				return;
			}
			cs::render::annotation::SetName(
				cachedSourceSRV.Get(), "Menu/BackgroundBlur/SourceCopy.SRV");

			CreateBlurTextures(targetDesc.Width, targetDesc.Height, targetDesc.Format);
			cachedSourceTexture = targetTexture.Get();
		}

		if (!blurTexture1 || !blurTexture2 || !cachedSourceSRV || !sourceCopyTexture)
			return;

		context->CopyResource(sourceCopyTexture.Get(), targetTexture.Get());

		ImGuiContext* ctx = ImGui::GetCurrentContext();
		if (!ctx || ctx->Windows.Size == 0)
			return;

		for (int i = 0; i < ctx->Windows.Size; ++i) {
			ImGuiWindow* window = ctx->Windows[i];
			// Active stays true after Render; WasActive is stale until the next NewFrame.
			if (!window || !window->Active || window->SkipItems)
				continue;

			// Docked windows are visually independent even though ParentWindow is set.
			if (window->ParentWindow != nullptr && !window->DockIsActive)
				continue;
			if (window->Flags & ImGuiWindowFlags_Tooltip)
				continue;
			if (window->Flags & ImGuiWindowFlags_NoBackground)
				continue;
			if (window->Name && std::string_view(window->Name).starts_with("##cs_"))
				continue;

			const ImRect windowRect = window->Rect();
			PerformBlur(context, targetDesc, cachedSourceSRV.Get(), targetRTV,
				windowRect.Min, windowRect.Max, window->WindowRounding);
		}
	}

	void Cleanup()
	{
		std::lock_guard lock(resourceMutex);

		vertexShader.Reset();
		downsamplePixelShader.Reset();
		horizontalPixelShader.Reset();
		verticalPixelShader.Reset();
		compositePixelShader.Reset();
		constantBuffer.Reset();
		windowConstantBuffer.Reset();
		samplerState.Reset();
		blendState.Reset();
		depthStencilState.Reset();
		scissorRasterizerState.Reset();

		ReleaseBlurTextures();

		cachedSourceSRV.Reset();
		sourceCopyTexture.Reset();
		cachedSourceTexture = nullptr;

		initialized = false;
		initializationFailed = false;
	}

	void SetEnabled(bool a_enable)
	{
		enabled = a_enable;
	}

	bool IsEnabled()
	{
		return enabled;
	}
}
