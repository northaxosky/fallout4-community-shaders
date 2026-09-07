#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

struct ID3D11ShaderResourceView;

namespace cs
{
	class Feature;

	enum class FeatureDebugViewKind : std::uint8_t
	{
		kTexturePreview,
		kFullscreen
	};

	struct FeatureDebugTexture
	{
		ID3D11ShaderResourceView* texture = nullptr;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::string caption;
		std::string_view unavailableText = "Texture not allocated.";
	};

	class DebugSnapshotRequest
	{
	public:
		void Refresh() noexcept { _requested.fetch_add(1, std::memory_order_release); }

		[[nodiscard]] std::uint64_t Pending() const noexcept
		{
			const auto request = _requested.load(std::memory_order_acquire);
			return request != _captured.load(std::memory_order_acquire) ? request : 0;
		}

		[[nodiscard]] bool Ready() const noexcept
		{
			return _captured.load(std::memory_order_acquire) != 0;
		}

		void Captured(std::uint64_t a_request) noexcept
		{
			_captured.store(a_request, std::memory_order_release);
		}

		void Invalidate() noexcept { _captured.store(0, std::memory_order_release); }

		void Reset() noexcept
		{
			_requested.store(0, std::memory_order_release);
			Invalidate();
		}

	private:
		std::atomic_uint64_t _requested{};
		std::atomic_uint64_t _captured{};
	};

	using FeatureDebugTextureProvider =
		FeatureDebugTexture (*)(const Feature&);

	struct FeatureDebugView
	{
		std::string_view id;
		std::string_view label;
		FeatureDebugViewKind kind = FeatureDebugViewKind::kTexturePreview;
		FeatureDebugTextureProvider textureProvider = nullptr;
	};

	struct FeatureDebugSelection
	{
		std::string_view feature;
		std::string_view view;
	};
}
