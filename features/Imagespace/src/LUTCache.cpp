#include "LUTCache.h"

#include <DirectXTex.h>
#include <filesystem>
#include <string>

#include "Utils/CSUtil.h"
#include "Log.h"

namespace cs::features::imagespace
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace.lutcache"); }

	namespace
	{
		constexpr const char* kLUTDir = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Imagespace\\LUTs\\";
	}

	LUTLoadResult LoadLUTFromFile(std::string_view a_filename)
	{
		LUTLoadResult result;
		if (a_filename.empty()) {
			result.status = LUTLoadStatus::FileMissing;
			return result;
		}

		const std::string path = std::string(kLUTDir) + std::string(a_filename) + ".dds";
		if (!std::filesystem::exists(path)) {
			L->warn("LUT file missing: {}", path);
			result.status = LUTLoadStatus::FileMissing;
			return result;
		}

		auto* device = cs::util::GetD3DDevice();
		if (!device) {
			result.status = LUTLoadStatus::DeviceNotReady;
			return result;
		}

		DirectX::ScratchImage img;
		DirectX::TexMetadata  meta{};
		const std::wstring wpath(path.begin(), path.end());
		if (FAILED(DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, &meta, img))) {
			L->warn("LUT load failed: {}", path);
			result.status = LUTLoadStatus::DDSLoadFailed;
			return result;
		}
		if (meta.dimension != DirectX::TEX_DIMENSION_TEXTURE3D ||
			meta.width != 32 || meta.height != 32 || meta.depth != 32) {
			L->warn("LUT dims mismatch ({}x{}x{} dim={}); expected 32x32x32 Texture3D",
				static_cast<std::uint32_t>(meta.width), static_cast<std::uint32_t>(meta.height),
				static_cast<std::uint32_t>(meta.depth), static_cast<int>(meta.dimension));
			result.status = LUTLoadStatus::DimensionsMismatch;
			return result;
		}

		winrt::com_ptr<ID3D11Resource> resource;
		if (FAILED(DirectX::CreateTexture(device, img.GetImages(), img.GetImageCount(), meta, resource.put()))) {
			L->warn("LUT CreateTexture failed: {}", path);
			result.status = LUTLoadStatus::CreateTextureFailed;
			return result;
		}
		winrt::com_ptr<ID3D11Texture3D> tex;
		if (FAILED(resource->QueryInterface(IID_PPV_ARGS(tex.put())))) {
			L->warn("LUT resource is not Texture3D: {}", path);
			result.status = LUTLoadStatus::CreateTextureFailed;
			return result;
		}

		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = static_cast<DXGI_FORMAT>(meta.format);
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		sd.Texture3D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(tex.get(), &sd, srv.put()))) {
			L->warn("LUT SRV creation failed: {}", path);
			result.status = LUTLoadStatus::SRVCreationFailed;
			return result;
		}

		L->info("LUT cached: {}", path);
		result.srv = std::move(srv);
		result.status = LUTLoadStatus::Ok;
		return result;
	}

	ID3D11ShaderResourceView* LUTCache::GetOrLoad(const std::string& a_filename)
	{
		if (a_filename.empty()) return nullptr;
		if (auto it = entries.find(a_filename); it != entries.end()) {
			return it->second.get();
		}
		auto loaded = LoadLUTFromFile(a_filename);
		if (loaded.status == LUTLoadStatus::DeviceNotReady) {
			// Keep device-not-ready misses retryable.
			return nullptr;
		}
		auto* raw = loaded.srv.get();
		// Cache hard failures until Clear().
		entries.emplace(a_filename, std::move(loaded.srv));
		return raw;
	}

	void LUTCache::Preload(const std::vector<std::string>& a_filenames)
	{
		for (const auto& name : a_filenames) {
			(void)GetOrLoad(name);
		}
	}

	ID3D11ShaderResourceView* LUTCache::TryGet(const std::string& a_filename) const
	{
		if (a_filename.empty()) return nullptr;
		auto it = entries.find(a_filename);
		return (it != entries.end()) ? it->second.get() : nullptr;
	}
}
