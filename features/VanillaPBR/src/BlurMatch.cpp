#include "BlurMatch.h"

#include "Render/Annotation.h"
#include "Render/ComputeScope.h"
#include "Render/SharedData.h"
#include "ShaderDefines.h"
#include "Utils/CSBuffer.h"
#include "Utils/CSUtil.h"

#include <d3d11.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace cs::features::vanilla_pbr
{
	namespace
	{
		constexpr const wchar_t* kDetailPath = L"Data\\Shaders\\FO4\\VanillaPBR\\BlurDetailCS.hlsl";
		constexpr const wchar_t* kMatchPath = L"Data\\Shaders\\FO4\\VanillaPBR\\BlurMatchCS.hlsl";
		// 42 eight-mip authored cubes refresh every 84 frames.
		constexpr std::uint32_t kAuthoredJobsPerFrame = 4;
		constexpr std::uint32_t kPartialCount = kBlurMatchTargets * kBlurMatchMaxMips * 6 * kBlurMatchGroupsPerFace;

		using Float4 = std::array<float, 4>;

		template <class T>
		void Upload(ID3D11DeviceContext* a_context, ID3D11Buffer* a_buffer, const T& a_data)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			DX::ThrowIfFailed(a_context->Map(a_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			std::memcpy(mapped.pData, &a_data, sizeof(T));
			a_context->Unmap(a_buffer, 0);
		}

		std::uint32_t FullMask(std::uint32_t a_mips) noexcept
		{
			return (1u << a_mips) - 1u;
		}
	}

	bool BlurMatch::Create(ID3D11Device* a_device, spdlog::logger& a_log) noexcept
	{
		_log = &a_log;
		if (!a_device)
			return false;
		try {
			_detailCS = util::RequireComputeShader("VanillaPBR/BlurDetail.CS", kDetailPath, kBlurMatchShaderDefines);
			_matchCS = util::RequireComputeShader("VanillaPBR/BlurMatch.CS", kMatchPath, kBlurMatchShaderDefines);

			auto detailDesc = buffer::ConstantBufferDesc<BlurDetailCB>();
			DX::ThrowIfFailed(a_device->CreateBuffer(&detailDesc, nullptr, _detailCB.put()));
			render::annotation::SetName(_detailCB.get(), "VanillaPBR/BlurDetail.CB");
			auto matchDesc = buffer::ConstantBufferDesc<BlurMatchCB>();
			DX::ThrowIfFailed(a_device->CreateBuffer(&matchDesc, nullptr, _matchCB.put()));
			render::annotation::SetName(_matchCB.get(), "VanillaPBR/BlurMatch.CB");

			auto partialsDesc = buffer::StructuredBufferDesc<Float4>(kPartialCount);
			DX::ThrowIfFailed(a_device->CreateBuffer(&partialsDesc, nullptr, _partials.put()));
			D3D11_SHADER_RESOURCE_VIEW_DESC partialsView{};
			partialsView.Format = DXGI_FORMAT_UNKNOWN;
			partialsView.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			partialsView.Buffer.NumElements = kPartialCount;
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(_partials.get(), &partialsView, _partialsSRV.put()));
			D3D11_UNORDERED_ACCESS_VIEW_DESC partialsAccess{};
			partialsAccess.Format = DXGI_FORMAT_UNKNOWN;
			partialsAccess.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			partialsAccess.Buffer.NumElements = kPartialCount;
			DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(_partials.get(), &partialsAccess, _partialsUAV.put()));
			render::annotation::SetName(_partials.get(), "VanillaPBR/BlurDetail.Partials");

			D3D11_TEXTURE2D_DESC tableDesc{};
			tableDesc.Width = kBlurMatchTableWidth;
			tableDesc.Height = kBlurMatchMaxSlots;
			tableDesc.MipLevels = 1;
			tableDesc.ArraySize = 1;
			tableDesc.Format = DXGI_FORMAT_R32_FLOAT;
			tableDesc.SampleDesc.Count = 1;
			tableDesc.Usage = D3D11_USAGE_DEFAULT;
			tableDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			const std::vector<float> zeros(kBlurMatchTableWidth * kBlurMatchMaxSlots, 0.0f);
			const D3D11_SUBRESOURCE_DATA tableData{ zeros.data(), kBlurMatchTableWidth * sizeof(float), 0 };
			DX::ThrowIfFailed(a_device->CreateTexture2D(&tableDesc, &tableData, _table.put()));
			DX::ThrowIfFailed(a_device->CreateShaderResourceView(_table.get(), nullptr, _tableSRV.put()));
			DX::ThrowIfFailed(a_device->CreateUnorderedAccessView(_table.get(), nullptr, _tableUAV.put()));
			render::annotation::SetName(_table.get(), "VanillaPBR/LiveLOD.Texture");
		} catch (const std::exception& e) {
			a_log.warn("Live reflection blur matching unavailable: {}", e.what());
			return false;
		}
		_created = true;
		return true;
	}

	void BlurMatch::CaptureAuthored(ID3D11DeviceContext* a_context) noexcept
	{
		if (!_created || !a_context)
			return;
		winrt::com_ptr<ID3D11ShaderResourceView> view;
		a_context->PSGetShaderResources(kAuthoredCubePSSlot, 1, view.put());
		if (!view || view == _authoredView || view == _capturedAuthored)
			return;
		// Other composite families bind buffers here.
		D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
		view->GetDesc(&desc);
		if (desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY)
			_capturedAuthored = std::move(view);
	}

	bool BlurMatch::Adopt(Source& a_source, ID3D11ShaderResourceView* a_view) const
	{
		winrt::com_ptr<ID3D11Resource> resource;
		a_view->GetResource(resource.put());
		const auto texture = resource.try_as<ID3D11Texture2D>();
		if (!texture)
			return false;
		D3D11_TEXTURE2D_DESC textureDesc{};
		texture->GetDesc(&textureDesc);
		D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		a_view->GetDesc(&viewDesc);
		D3D11_SHADER_RESOURCE_VIEW_DESC facesDesc{};
		facesDesc.Format = viewDesc.Format;
		facesDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		facesDesc.Texture2DArray.MipLevels = textureDesc.MipLevels;
		facesDesc.Texture2DArray.ArraySize = textureDesc.ArraySize;
		winrt::com_ptr<ID3D11Device> device;
		a_view->GetDevice(device.put());
		winrt::com_ptr<ID3D11ShaderResourceView> faces;
		if (FAILED(device->CreateShaderResourceView(texture.get(), &facesDesc, faces.put())))
			return false;
		a_source = { std::move(resource), std::move(faces), std::min(textureDesc.MipLevels, kBlurMatchMaxMips), textureDesc.ArraySize / 6 };
		return a_source.mips > 0;
	}

	BlurMatch::LiveStream* BlurMatch::FindLive(ID3D11ShaderResourceView* a_view)
	{
		winrt::com_ptr<ID3D11Resource> resource;
		a_view->GetResource(resource.put());
		std::uint32_t index = 0;
		while (index < _live.size() && _live[index].source.resource != resource)
			++index;
		if (index == _live.size()) {
			// A new stream replaces the one not consumed last.
			index = _liveTarget == kBlurMatchMaxSlots ? 1 : 0;
			_live[index] = {};
			if (!Adopt(_live[index].source, a_view))
				return nullptr;
		}
		_liveTarget = kBlurMatchMaxSlots + index;
		return &_live[index];
	}

	void BlurMatch::Profile(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_faces, const BlurDetailCB& a_jobs, std::uint32_t a_count)
	{
		if (!a_count)
			return;
		Upload(a_context, _detailCB.get(), a_jobs);
		ID3D11Buffer* buffer = _detailCB.get();
		ID3D11UnorderedAccessView* partials = _partialsUAV.get();
		a_context->CSSetConstantBuffers(0, 1, &buffer);
		a_context->CSSetUnorderedAccessViews(0, 1, &partials, nullptr);
		a_context->CSSetShaderResources(0, 1, &a_faces);
		a_context->CSSetShader(_detailCS.get(), nullptr, 0);
		a_context->Dispatch(kBlurMatchGroupsPerSide, kBlurMatchGroupsPerSide, 6 * a_count);
	}

	ID3D11ShaderResourceView* BlurMatch::Update(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_live) noexcept
	{
		if (!_created || !a_context)
			return nullptr;
		try {
			if (auto view = std::exchange(_capturedAuthored, nullptr)) {
				Source source;
				if (Adopt(source, view.get())) {
					_authored = std::move(source);
					_authoredView = std::move(view);
					_slotCount = std::min(_authored.cubes, kBlurMatchMaxSlots);
					_slotMips.fill(0);
					_cursor = 0;
					_authoredSlots.store(_slotCount, std::memory_order_relaxed);
					_log->info("Live reflection blur matching profiles {} authored cubes of {} mips.", _slotCount, _authored.mips);
				}
			}
			auto* live = a_live ? FindLive(a_live) : nullptr;

			const render::annotation::ScopedEvent event("VanillaPBR::BlurMatch");
			const ComputeScope scope(a_context, 3, 1, 1, 1);
			ID3D11SamplerState* sampler = render::GetLinearClampSampler();
			a_context->CSSetSamplers(0, 1, &sampler);

			if (_slotCount) {
				BlurDetailCB jobs;
				const std::uint32_t total = _slotCount * _authored.mips;
				const std::uint32_t count = std::min(kAuthoredJobsPerFrame, total);
				for (std::uint32_t job = 0; job < count; ++job) {
					const std::uint32_t slot = _cursor / _authored.mips;
					const std::uint32_t mip = _cursor % _authored.mips;
					jobs.Jobs[job][0] = slot;
					jobs.Jobs[job][1] = mip;
					jobs.Jobs[job][2] = slot * 6;
					_slotMips[slot] |= 1u << mip;
					_cursor = (_cursor + 1) % total;
				}
				Profile(a_context, _authored.faces.get(), jobs, count);
			}
			// One live mip a frame follows the stream's republication.
			if (live) {
				BlurDetailCB jobs;
				jobs.Jobs[0][0] = _liveTarget;
				jobs.Jobs[0][1] = live->nextMip;
				live->profiledMips |= 1u << live->nextMip;
				live->nextMip = (live->nextMip + 1) % live->source.mips;
				Profile(a_context, live->source.faces.get(), jobs, 1);
			}

			BlurMatchCB match;
			std::uint32_t profiled = 0;
			for (std::uint32_t slot = 0; slot < _slotCount; ++slot) {
				if (_slotMips[slot] != FullMask(_authored.mips))
					continue;
				match.SlotReady[slot / 32] |= 1u << (slot % 32);
				++profiled;
			}
			const bool liveReady = live && live->profiledMips == FullMask(live->source.mips);
			_profiledSlots.store(profiled, std::memory_order_relaxed);
			_liveReady.store(liveReady, std::memory_order_relaxed);
			if (!liveReady || !_slotCount)
				return nullptr;

			match.LiveTarget = _liveTarget;
			match.LiveMips = live->source.mips;
			match.AuthoredMips = _authored.mips;
			match.SlotCount = _slotCount;
			Upload(a_context, _matchCB.get(), match);
			ID3D11Buffer* buffer = _matchCB.get();
			// Output first, so the partials leave u0 before binding as input.
			ID3D11UnorderedAccessView* table = _tableUAV.get();
			ID3D11ShaderResourceView* inputs[]{ _partialsSRV.get(), _authored.faces.get(), live->source.faces.get() };
			a_context->CSSetConstantBuffers(0, 1, &buffer);
			a_context->CSSetUnorderedAccessViews(0, 1, &table, nullptr);
			a_context->CSSetShaderResources(0, 3, inputs);
			a_context->CSSetShader(_matchCS.get(), nullptr, 0);
			a_context->Dispatch(1, 1, 1);
			return _tableSRV.get();
		} catch (const std::exception& e) {
			_created = false;
			_log->error("Live reflection blur matching stopped: {}", e.what());
		} catch (...) {
			_created = false;
			_log->error("Live reflection blur matching stopped.");
		}
		return nullptr;
	}

	void BlurMatch::Release() noexcept
	{
		_capturedAuthored = nullptr;
		_authoredView = nullptr;
		_authored = {};
		_live = {};
		_slotCount = 0;
		_slotMips.fill(0);
		_cursor = 0;
		_liveTarget = 0;
		_authoredSlots.store(0, std::memory_order_relaxed);
		_profiledSlots.store(0, std::memory_order_relaxed);
		_liveReady.store(false, std::memory_order_relaxed);
	}
}
