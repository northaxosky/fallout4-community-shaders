#pragma once

#include "Utils/CSBuffer.h"

#include <d3d11_4.h>
#include <winrt/base.h>

namespace cs::features
{
	class RCAS
	{
	public:
		RCAS() = default;
		~RCAS();

		void Initialize();
		bool ApplySharpen(ID3D11ShaderResourceView* inputTexture, ID3D11UnorderedAccessView* outputUAV, float sharpness);

	private:
		winrt::com_ptr<ID3D11ComputeShader> rcasComputeShader;
		cs::buffer::ConstantBuffer* rcasConfigCB = nullptr;
	};
}
