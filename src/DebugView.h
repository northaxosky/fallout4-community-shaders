#pragma once

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
