#pragma once

#include "BlurMatchConstants.h"

#include <array>
#include <atomic>
#include <cstdint>

#include <winrt/base.h>

struct ID3D11Buffer;
struct ID3D11ComputeShader;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Resource;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D11UnorderedAccessView;

namespace spdlog
{
	class logger;
}

namespace cs::features::vanilla_pbr
{
	// FO4: matches the live cube's mip to the blur baked into each authored cube.
	class BlurMatch
	{
	public:
		// Render thread from here on; false leaves matching off.
		bool Create(ID3D11Device* a_device, spdlog::logger& a_log) noexcept;
		void CaptureAuthored(ID3D11DeviceContext* a_context) noexcept;
		// Profiles this frame's share; returns the matched LOD table or null.
		ID3D11ShaderResourceView* Update(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_live) noexcept;
		// Drops the adopted cubes, so a disabled feature pins no game textures.
		void Release() noexcept;

		std::uint32_t AuthoredSlots() const noexcept { return _authoredSlots.load(std::memory_order_relaxed); }
		std::uint32_t ProfiledSlots() const noexcept { return _profiledSlots.load(std::memory_order_relaxed); }
		bool LiveReady() const noexcept { return _liveReady.load(std::memory_order_relaxed); }

	private:
		struct Source
		{
			winrt::com_ptr<ID3D11Resource> resource;
			// The cube's faces as a 2D array, so sampling clamps at face edges.
			winrt::com_ptr<ID3D11ShaderResourceView> faces;
			std::uint32_t mips = 0;
			std::uint32_t cubes = 0;
		};

		struct LiveStream
		{
			Source source;
			std::uint32_t profiledMips = 0;
			std::uint32_t nextMip = 0;
		};

		bool Adopt(Source& a_source, ID3D11ShaderResourceView* a_view) const;
		LiveStream* FindLive(ID3D11ShaderResourceView* a_view);
		void Profile(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_faces, const BlurDetailCB& a_jobs, std::uint32_t a_count);

		spdlog::logger* _log = nullptr;
		bool _created = false;
		winrt::com_ptr<ID3D11ComputeShader> _detailCS;
		winrt::com_ptr<ID3D11ComputeShader> _matchCS;
		winrt::com_ptr<ID3D11Buffer> _detailCB;
		winrt::com_ptr<ID3D11Buffer> _matchCB;
		winrt::com_ptr<ID3D11Buffer> _partials;
		winrt::com_ptr<ID3D11ShaderResourceView> _partialsSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> _partialsUAV;
		winrt::com_ptr<ID3D11Texture2D> _table;
		winrt::com_ptr<ID3D11ShaderResourceView> _tableSRV;
		winrt::com_ptr<ID3D11UnorderedAccessView> _tableUAV;

		winrt::com_ptr<ID3D11ShaderResourceView> _capturedAuthored;
		winrt::com_ptr<ID3D11ShaderResourceView> _authoredView;
		Source _authored;
		std::uint32_t _slotCount = 0;
		std::array<std::uint32_t, kBlurMatchMaxSlots> _slotMips{};
		std::uint32_t _cursor = 0;
		std::array<LiveStream, 2> _live;
		std::uint32_t _liveTarget = 0;

		std::atomic_uint32_t _authoredSlots{ 0 };
		std::atomic_uint32_t _profiledSlots{ 0 };
		std::atomic_bool _liveReady{ false };
	};
}
