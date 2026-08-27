#include "RCAS.h"

#include "Log.h"
#include "Render/Engine.h"
#include "Utils/CSUtil.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.upscaling.rcas");

		struct RCASConfig
		{
			float sharpness;
			float3 pad;
		};
	}

	RCAS::~RCAS()
	{
		delete rcasConfigCB;
		rcasConfigCB = nullptr;
	}

	void RCAS::Initialize()
	{
		if (rcasConfigCB)
			return;

		L->info("Creating resources");
		std::vector<std::pair<const char*, const char*>> defines;
		rcasComputeShader.attach((ID3D11ComputeShader*)cs::util::CompileShader(
			L"Data\\Shaders\\Upscaling\\RCAS\\RCAS.hlsl", defines, "cs_5_0"));
		rcasConfigCB = new cs::buffer::ConstantBuffer(cs::buffer::ConstantBufferDesc<RCASConfig>());
	}

	bool RCAS::ApplySharpen(ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV, float sharpness)
	{
		auto* context = cs::engine::GetImmediateContext();
		auto* graphicsState = cs::engine::GetGraphicsState();
		if (!context || !graphicsState || !rcasConfigCB || !inputSRV || !outputUAV)
			return false;

		if (!rcasComputeShader) {
			L->warn("Compute shader not compiled");
			return false;
		}

		uint32_t screenWidth = graphicsState->screenWidth;
		uint32_t screenHeight = graphicsState->screenHeight;

		RCASConfig config{};
		config.sharpness = sharpness;

		rcasConfigCB->Update(config);
		auto bufferArray = rcasConfigCB->CB();

		context->CSSetShader(rcasComputeShader.get(), nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &bufferArray);

		ID3D11ShaderResourceView* srvs[] = { inputSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[] = { outputUAV };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		uint32_t dispatchX = (screenWidth + 7) / 8;
		uint32_t dispatchY = (screenHeight + 7) / 8;
		context->Dispatch(dispatchX, dispatchY, 1);

		ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSRVs);

		ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		context->CSSetShader(nullptr, nullptr, 0);
		return true;
	}
}
