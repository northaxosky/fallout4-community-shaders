#include "XeSS.h"

#include <filesystem>
#include <format>

#include "Log.h"
#include "Render/Annotation.h"
#include "Utils/ShaderCompile.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.xess");

		render::temporal::ProviderResult Success()
		{
			return { .code = render::temporal::ProviderResultCode::kSuccess };
		}

		render::temporal::ProviderResult Failure(
			std::string a_message,
			xess_result_t a_result = XESS_RESULT_ERROR_UNKNOWN)
		{
			return {
				.code = render::temporal::ProviderResultCode::kFailure,
				.sdkResult = a_result,
				.message = std::move(a_message)
			};
		}

		template <class T>
		T Load(HMODULE a_module, const char* a_name)
		{
			return reinterpret_cast<T>(GetProcAddress(a_module, a_name));
		}
	}

	XeSSSuperResolution::~XeSSSuperResolution()
	{
		DestroyAfterDrain();
	}

	const char* XeSSSuperResolution::Name() const noexcept
	{
		return _d3d12 ? "XeSS-SR D3D12" : "XeSS-SR D3D11";
	}

	bool XeSSSuperResolution::LoadD3D11()
	{
		const auto path =
			std::filesystem::path(PluginDir) / L"libxess_dx11.dll";
		_module = LoadLibraryW(path.c_str());
		if (!_module) {
			return false;
		}
		_createD3D11 = Load<CreateD3D11Context>(
			_module, "xessD3D11CreateContext");
		_initD3D11 = Load<InitD3D11>(_module, "xessD3D11Init");
		_executeD3D11 = Load<ExecuteD3D11>(_module, "xessD3D11Execute");
		_destroy = Load<DestroyContext>(_module, "xessDestroyContext");
		_setVelocityScale =
			Load<SetVelocityScale>(_module, "xessSetVelocityScale");
		_getOptimalInputResolution = Load<GetOptimalInputResolution>(
			_module, "xessGetOptimalInputResolution");
		return _createD3D11 && _initD3D11 && _executeD3D11 &&
			_destroy && _setVelocityScale && _getOptimalInputResolution;
	}

	bool XeSSSuperResolution::LoadD3D12()
	{
		const auto path = std::filesystem::path(PluginDir) / L"libxess.dll";
		_module = LoadLibraryW(path.c_str());
		if (!_module) {
			return false;
		}
		_createD3D12 = Load<CreateD3D12Context>(
			_module, "xessD3D12CreateContext");
		_initD3D12 = Load<InitD3D12>(_module, "xessD3D12Init");
		_executeD3D12 = Load<ExecuteD3D12>(_module, "xessD3D12Execute");
		_destroy = Load<DestroyContext>(_module, "xessDestroyContext");
		_setVelocityScale =
			Load<SetVelocityScale>(_module, "xessSetVelocityScale");
		_getOptimalInputResolution = Load<GetOptimalInputResolution>(
			_module, "xessGetOptimalInputResolution");
		return _createD3D12 && _initD3D12 && _executeD3D12 &&
			_destroy && _setVelocityScale && _getOptimalInputResolution;
	}

	bool XeSSSuperResolution::IsIntelD3D11Device(
		ID3D11Device* a_device) const
	{
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		winrt::com_ptr<IDXGIAdapter> adapter;
		DXGI_ADAPTER_DESC desc{};
		return a_device &&
			SUCCEEDED(a_device->QueryInterface(
				IID_PPV_ARGS(dxgiDevice.put()))) &&
			SUCCEEDED(dxgiDevice->GetAdapter(adapter.put())) &&
			SUCCEEDED(adapter->GetDesc(&desc)) &&
			desc.VendorId == 0x8086;
	}

	render::temporal::ProviderResult XeSSSuperResolution::Initialize(
		const render::temporal::SuperResolutionInitContext& a_context)
	{
		DestroyAfterDrain();
		_sizeCache.Clear();
		if (const auto* device =
				std::get_if<ID3D11Device*>(&a_context.device)) {
			if (!*device || !IsIntelD3D11Device(*device)) {
				return Failure(
					"Native XeSS D3D11 requires a supported Intel adapter.",
					XESS_RESULT_ERROR_UNSUPPORTED_DEVICE);
			}
			if (!LoadD3D11()) {
				DestroyAfterDrain();
				return Failure("libxess_dx11.dll or required exports are unavailable.");
			}
			_device11.copy_from(*device);
			_d3d12 = false;
		} else if (const auto* d3d12Device =
					   std::get_if<ID3D12Device*>(&a_context.device)) {
			if (!*d3d12Device || !LoadD3D12()) {
				DestroyAfterDrain();
				return Failure("libxess.dll or required exports are unavailable.");
			}
			_device12.copy_from(*d3d12Device);
			_d3d12 = true;
		} else {
			return Failure("XeSS received an unsupported device context.");
		}
		return Success();
	}

	std::optional<xess_quality_settings_t> XeSSSuperResolution::ToQuality(
		std::uint32_t a_quality)
	{
		switch (a_quality) {
		case 0:
			return XESS_QUALITY_SETTING_AA;
		case 1:
			return XESS_QUALITY_SETTING_QUALITY;
		case 2:
			return XESS_QUALITY_SETTING_BALANCED;
		case 3:
			return XESS_QUALITY_SETTING_PERFORMANCE;
		case 4:
			return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
		default:
			return std::nullopt;
		}
	}

	render::temporal::ProviderResult
		XeSSSuperResolution::EnsureCreatedContext()
	{
		if (_context) {
			return Success();
		}

		const auto result = _d3d12
			? _createD3D12(_device12.get(), &_context)
			: _createD3D11(_device11.get(), &_context);
		if (result != XESS_RESULT_SUCCESS || !_context) {
			_context = nullptr;
			return Failure("XeSS context creation failed.", result);
		}
		return Success();
	}

	render::temporal::SuperResolutionSizeResult
		XeSSSuperResolution::QueryRenderSize(
			const render::temporal::SuperResolutionSizeRequest& a_request)
	{
		if (const auto* cached = _sizeCache.Find(a_request)) {
			return *cached;
		}
		const auto quality = ToQuality(a_request.qualityMode);
		if (!quality || !a_request.outputWidth ||
			!a_request.outputHeight || !_getOptimalInputResolution) {
			return {
				.result = Failure(
					"XeSS received an invalid render-size request.")
			};
		}
		const auto contextResult = EnsureCreatedContext();
		if (!contextResult.Succeeded()) {
			return { .result = contextResult };
		}

		const xess_2d_t output{
			.x = a_request.outputWidth,
			.y = a_request.outputHeight
		};
		xess_2d_t optimal{};
		xess_2d_t minimum{};
		xess_2d_t maximum{};
		const auto sdkResult = _getOptimalInputResolution(
			_context,
			&output,
			*quality,
			&optimal,
			&minimum,
			&maximum);
		render::temporal::SuperResolutionSizeResult result{
			.result = sdkResult == XESS_RESULT_SUCCESS
				? Success()
				: Failure(
					"XeSS optimal-input-resolution query failed.",
					sdkResult),
			.renderWidth = optimal.x,
			.renderHeight = optimal.y
		};
		if (!result.Succeeded()) {
			return result;
		}
		_sizeCache.Store(a_request, result);
		return result;
	}

	bool XeSSSuperResolution::RequiresContextReinitialization(
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight,
		std::uint32_t a_qualityMode) const noexcept
	{
		const auto quality = ToQuality(a_qualityMode);
		return _contextInitialized &&
			(!quality || _outputWidth != a_outputWidth ||
				_outputHeight != a_outputHeight || _quality != *quality);
	}

	render::temporal::ProviderResult XeSSSuperResolution::EnsureContext(
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight,
		xess_quality_settings_t a_quality,
		bool a_contextDrained)
	{
		if (_contextInitialized && _outputWidth == a_outputWidth &&
			_outputHeight == a_outputHeight && _quality == a_quality) {
			return Success();
		}
		if (_contextInitialized && !a_contextDrained) {
			return Failure(
				"XeSS context reinitialization requires a completed GPU drain.");
		}
		if (_contextInitialized && _context) {
			_destroy(_context);
			_context = nullptr;
			_contextInitialized = false;
		}
		const auto createResult = EnsureCreatedContext();
		if (!createResult.Succeeded()) {
			return createResult;
		}

		constexpr std::uint32_t flags = XESS_INIT_FLAG_LDR_INPUT_COLOR;
		xess_result_t result = XESS_RESULT_ERROR_UNINITIALIZED;
		if (_d3d12) {
			const xess_d3d12_init_params_t init{
				.outputResolution = { a_outputWidth, a_outputHeight },
				.qualitySetting = a_quality,
				.initFlags = flags,
				.creationNodeMask = 0,
				.visibleNodeMask = 0
			};
			result = _initD3D12(_context, &init);
		} else {
			const xess_d3d11_init_params_t init{
				.outputResolution = { a_outputWidth, a_outputHeight },
				.qualitySetting = a_quality,
				.initFlags = flags
			};
			result = _initD3D11(_context, &init);
		}
		if (result != XESS_RESULT_SUCCESS) {
			if (_context) {
				_destroy(_context);
				_context = nullptr;
			}
			_contextInitialized = false;
			return Failure("XeSS context initialization failed.", result);
		}
		_contextInitialized = true;
		_outputWidth = a_outputWidth;
		_outputHeight = a_outputHeight;
		_quality = a_quality;
		return Success();
	}

	render::temporal::ProviderResult XeSSSuperResolution::Preflight(
		ID3D11Device* a_conversionDevice,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight,
		std::uint32_t a_qualityMode,
		bool a_contextDrained)
	{
		const auto quality = ToQuality(a_qualityMode);
		if (!quality) {
			return Failure("XeSS received an invalid quality mode.");
		}
		const auto contextResult = EnsureContext(
			a_outputWidth,
			a_outputHeight,
			*quality,
			a_contextDrained);
		if (!contextResult.Succeeded()) {
			return contextResult;
		}
		if (!EnsureD3D11ConversionResources(
				a_conversionDevice,
				a_renderWidth,
				a_renderHeight,
				a_outputWidth,
				a_outputHeight)) {
			return Failure("XeSS color-conversion resource preflight failed.");
		}
		return Success();
	}

	bool XeSSSuperResolution::EnsureD3D11ConversionResources(
		ID3D11Device* a_device,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight)
	{
		if (!_decodeShader || !_encodeShader) {
			const auto decode = cs::util::CompileShaderToBlob(
				L"Data\\Shaders\\Upscaling\\XeSSDecodeCS.hlsl",
				{},
				"cs_5_0",
				"main");
			const auto encode = cs::util::CompileShaderToBlob(
				L"Data\\Shaders\\Upscaling\\XeSSEncodeCS.hlsl",
				{},
				"cs_5_0",
				"main");
			if (!decode || !encode ||
				FAILED(a_device->CreateComputeShader(
					decode->GetBufferPointer(),
					decode->GetBufferSize(),
					nullptr,
					_decodeShader.put())) ||
				FAILED(a_device->CreateComputeShader(
					encode->GetBufferPointer(),
					encode->GetBufferSize(),
					nullptr,
					_encodeShader.put()))) {
				return false;
			}
			cs::render::annotation::SetName(
				_decodeShader.get(), "XeSS/DecodeGamma22.CS");
			cs::render::annotation::SetName(
				_encodeShader.get(), "XeSS/EncodeGamma22.CS");
		}

		const auto matches = [](const auto& a_texture,
								std::uint32_t a_width,
								std::uint32_t a_height) {
			if (!a_texture) {
				return false;
			}
			D3D11_TEXTURE2D_DESC desc{};
			a_texture->GetDesc(&desc);
			return desc.Width == a_width && desc.Height == a_height;
		};
		if (matches(_linearInput, a_renderWidth, a_renderHeight) &&
			matches(_linearOutput, a_outputWidth, a_outputHeight)) {
			return true;
		}

		const auto create = [&](std::uint32_t a_width,
								std::uint32_t a_height,
								winrt::com_ptr<ID3D11Texture2D>& a_texture,
								winrt::com_ptr<ID3D11ShaderResourceView>& a_srv,
								winrt::com_ptr<ID3D11UnorderedAccessView>& a_uav,
								std::string_view a_name) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = a_width;
			desc.Height = a_height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags =
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			if (FAILED(a_device->CreateTexture2D(
					&desc, nullptr, a_texture.put())) ||
				FAILED(a_device->CreateShaderResourceView(
					a_texture.get(), nullptr, a_srv.put())) ||
				FAILED(a_device->CreateUnorderedAccessView(
					a_texture.get(), nullptr, a_uav.put()))) {
				return false;
			}
			cs::render::annotation::SetName(
				a_texture.get(), std::string(a_name) + ".Texture");
			cs::render::annotation::SetName(
				a_srv.get(), std::string(a_name) + ".SRV");
			cs::render::annotation::SetName(
				a_uav.get(), std::string(a_name) + ".UAV");
			return true;
		};
		_linearInput = nullptr;
		_linearInputSrv = nullptr;
		_linearInputUav = nullptr;
		_linearOutput = nullptr;
		_linearOutputSrv = nullptr;
		_linearOutputUav = nullptr;
		return create(
				   a_renderWidth,
				   a_renderHeight,
				   _linearInput,
				   _linearInputSrv,
				   _linearInputUav,
				   "XeSS/LinearInput") &&
			create(
				a_outputWidth,
				a_outputHeight,
				_linearOutput,
				_linearOutputSrv,
				_linearOutputUav,
				"XeSS/LinearOutput");
	}

	bool XeSSSuperResolution::ConvertD3D11(
		ID3D11DeviceContext* a_context,
		ID3D11Resource*,
		ID3D11ShaderResourceView* a_sourceSrv,
		ID3D11UnorderedAccessView* a_destination,
		ID3D11ComputeShader* a_shader,
		std::uint32_t a_width,
		std::uint32_t a_height)
	{
		if (!a_context || !a_sourceSrv || !a_destination || !a_shader) {
			return false;
		}
		a_context->CSSetShader(a_shader, nullptr, 0);
		a_context->CSSetShaderResources(0, 1, &a_sourceSrv);
		a_context->CSSetUnorderedAccessViews(
			0, 1, &a_destination, nullptr);
		a_context->Dispatch((a_width + 7) / 8, (a_height + 7) / 8, 1);
		ID3D11ShaderResourceView* nullSrv = nullptr;
		ID3D11UnorderedAccessView* nullUav = nullptr;
		a_context->CSSetShaderResources(0, 1, &nullSrv);
		a_context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		a_context->CSSetShader(nullptr, nullptr, 0);
		return true;
	}

	render::temporal::ProviderResult XeSSSuperResolution::Record(
		const render::temporal::SuperResolutionRequest& a_request)
	{
		const bool validD3D11Color = !_d3d12 &&
			a_request.color.transfer ==
				render::temporal::TransferFunction::kGamma22;
		const bool validD3D12Color = _d3d12 &&
			a_request.color.transfer ==
				render::temporal::TransferFunction::kLinear &&
			a_request.color.exposure ==
				render::temporal::ExposureMode::kExplicit &&
			a_request.color.exposureValue == 1.0f;
		if ((!validD3D11Color && !validD3D12Color) ||
			a_request.color.stage !=
				render::temporal::ColorStage::kPostTonemapLut) {
			return Failure("XeSS received an incompatible color contract.");
		}
		const auto quality = ToQuality(a_request.qualityMode);
		if (!quality) {
			return Failure("XeSS received an invalid quality mode.");
		}
		const auto contextResult = EnsureContext(
			a_request.outputWidth,
			a_request.outputHeight,
			*quality,
			false);
		if (!contextResult.Succeeded()) {
			return contextResult;
		}
		const auto velocityResult = _setVelocityScale(
			_context,
			static_cast<float>(a_request.renderWidth),
			static_cast<float>(a_request.renderHeight));
		if (velocityResult != XESS_RESULT_SUCCESS) {
			return Failure("XeSS velocity-scale update failed.", velocityResult);
		}

		if (_d3d12) {
			const auto* recording =
				std::get_if<render::temporal::D3D12RecordingContext>(
					&a_request.recording);
			const auto get = [](const auto& a_view) {
				return std::get_if<render::temporal::D3D12GpuView>(&a_view);
			};
			const auto* color = get(a_request.colorInput);
			const auto* motion = get(a_request.motionVectors);
			const auto* depth = get(a_request.depth);
			const auto* output = get(a_request.privateOutput);
			const auto* reactive = get(a_request.reactiveMask);
			if (!recording || !recording->commandList || !color || !motion ||
				!depth || !output) {
				return Failure("XeSS D3D12 received incomplete recording inputs.");
			}
			const xess_d3d12_execute_params_t execute{
				.pColorTexture = color->resource,
				.pVelocityTexture = motion->resource,
				.pDepthTexture = depth->resource,
				.pResponsivePixelMaskTexture =
					reactive ? reactive->resource : nullptr,
				.pOutputTexture = output->resource,
				// XeSS takes the geometry offset, opposite the engine's sample offset.
				.jitterOffsetX = -a_request.jitterX,
				.jitterOffsetY = -a_request.jitterY,
				.exposureScale = 1.0f,
				.resetHistory = a_request.resetHistory ? 1u : 0u,
				.inputWidth = a_request.renderWidth,
				.inputHeight = a_request.renderHeight
			};
			const auto result = _executeD3D12(
				_context, recording->commandList, &execute);
			return result == XESS_RESULT_SUCCESS
				? Success()
				: Failure("XeSS D3D12 execution failed.", result);
		}

		const auto* recording =
			std::get_if<render::temporal::D3D11RecordingContext>(
				&a_request.recording);
		const auto get = [](const auto& a_view) {
			return std::get_if<render::temporal::D3D11GpuView>(&a_view);
		};
		const auto* color = get(a_request.colorInput);
		const auto* motion = get(a_request.motionVectors);
		const auto* depth = get(a_request.depth);
		const auto* output = get(a_request.privateOutput);
		const auto* reactive = get(a_request.reactiveMask);
		if (!recording || !recording->context || !color || !color->resource ||
			!color->srv || !motion || !depth || !output || !output->uav ||
			!EnsureD3D11ConversionResources(
				_device11.get(),
				a_request.renderWidth,
				a_request.renderHeight,
				a_request.outputWidth,
				a_request.outputHeight) ||
			!ConvertD3D11(
				recording->context,
				color->resource,
				color->srv,
				_linearInputUav.get(),
				_decodeShader.get(),
				a_request.renderWidth,
				a_request.renderHeight)) {
			return Failure("XeSS D3D11 color preparation failed.");
		}
		const xess_d3d11_execute_params_t execute{
			.pColorTexture = _linearInput.get(),
			.pVelocityTexture = motion->resource,
			.pDepthTexture = depth->resource,
			.pResponsivePixelMaskTexture =
				reactive ? reactive->resource : nullptr,
			.pOutputTexture = _linearOutput.get(),
			.jitterOffsetX = -a_request.jitterX,
			.jitterOffsetY = -a_request.jitterY,
			.exposureScale = 1.0f,
			.resetHistory = a_request.resetHistory ? 1u : 0u,
			.inputWidth = a_request.renderWidth,
			.inputHeight = a_request.renderHeight
		};
		const auto result = _executeD3D11(_context, &execute);
		if (result != XESS_RESULT_SUCCESS ||
			!ConvertD3D11(
				recording->context,
				_linearOutput.get(),
				_linearOutputSrv.get(),
				output->uav,
				_encodeShader.get(),
				a_request.outputWidth,
				a_request.outputHeight)) {
			return Failure("XeSS D3D11 execution failed.", result);
		}
		return Success();
	}

	void XeSSSuperResolution::DestroyAfterDrain() noexcept
	{
		if (_context && _destroy) {
			const auto result = _destroy(_context);
			if (result != XESS_RESULT_SUCCESS) {
				L->warn(
					"XeSS context destruction returned {}",
					static_cast<std::int32_t>(result));
			}
		}
		_context = nullptr;
		_contextInitialized = false;
		_device11 = nullptr;
		_device12 = nullptr;
		_linearInput = nullptr;
		_linearInputSrv = nullptr;
		_linearInputUav = nullptr;
		_linearOutput = nullptr;
		_linearOutputSrv = nullptr;
		_linearOutputUav = nullptr;
		_decodeShader = nullptr;
		_encodeShader = nullptr;
		if (_module) {
			FreeLibrary(_module);
		}
		_module = nullptr;
		_createD3D11 = nullptr;
		_initD3D11 = nullptr;
		_executeD3D11 = nullptr;
		_createD3D12 = nullptr;
		_initD3D12 = nullptr;
		_executeD3D12 = nullptr;
		_destroy = nullptr;
		_setVelocityScale = nullptr;
		_getOptimalInputResolution = nullptr;
		_outputWidth = 0;
		_outputHeight = 0;
		_sizeCache.Clear();
	}
}
