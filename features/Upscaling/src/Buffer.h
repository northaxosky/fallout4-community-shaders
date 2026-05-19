#pragma once

#include <DirectXMath.h>
#include <d3d11.h>

#include <Windows.Foundation.h>
#include <stdio.h>
#include <winrt/base.h>

namespace cs::features::upscaling
{

// ========================================
// Helper Functions & Utilities
// ========================================

/**
 * @brief Calculate aligned constant buffer size
 * @param buffer_size Raw buffer size in bytes
 * @return Size aligned to 64-byte boundary (required for D3D11 constant buffers)
 *
 * D3D11 requires constant buffers to be aligned to 64-byte boundaries.
 * This function rounds up the size to the nearest multiple of 64.
 */
static constexpr std::uint32_t GetCBufferSize(std::uint32_t buffer_size)
{
	return (buffer_size + (64 - 1)) & ~(64 - 1);
}

// ========================================
// Buffer Description Helpers
// ========================================

/**
 * @brief Create a D3D11 constant buffer description
 * @param size Buffer size in bytes (will be aligned to 64-byte boundary)
 * @param dynamic If true, creates a dynamic buffer for frequent CPU updates
 * @return Configured D3D11_BUFFER_DESC for a constant buffer
 *
 * Dynamic buffers use D3D11_USAGE_DYNAMIC and D3D11_CPU_ACCESS_WRITE for efficient CPU updates.
 * Non-dynamic buffers use D3D11_USAGE_DEFAULT for GPU-only access.
 */
inline D3D11_BUFFER_DESC ConstantBufferDesc(uint32_t size, bool dynamic = false)
{
	D3D11_BUFFER_DESC desc{};
	ZeroMemory(&desc, sizeof(desc));
	desc.Usage = (!dynamic) ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = !dynamic ? 0 : D3D11_CPU_ACCESS_WRITE;
	desc.ByteWidth = GetCBufferSize(size);
	return desc;
}

/**
 * @brief Create a constant buffer description from a type
 * @tparam T Structure type to use for buffer size calculation
 * @param dynamic If true, creates a dynamic buffer for frequent CPU updates
 * @return Configured D3D11_BUFFER_DESC sized for type T
 */
template <typename T>
D3D11_BUFFER_DESC ConstantBufferDesc(bool dynamic = false)
{
	return ConstantBufferDesc(sizeof(T), dynamic);
}

/**
 * @brief Create a structured buffer description
 * @tparam T Structure type for buffer elements
 * @param count Number of elements in the buffer
 * @param uav If true, enables unordered access view (compute shader read/write)
 * @param dynamic If true, creates a dynamic buffer for CPU updates
 * @return Configured D3D11_BUFFER_DESC for a structured buffer
 *
 * Structured buffers store arrays of structures accessible in shaders.
 * UAV support enables compute shader writes. Dynamic buffers allow CPU updates.
 */
template <typename T>
D3D11_BUFFER_DESC StructuredBufferDesc(uint64_t count, bool uav = true, bool dynamic = false)
{
	D3D11_BUFFER_DESC desc{};
	desc.Usage = (uav || !dynamic) ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (uav)
		desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.CPUAccessFlags = !dynamic ? 0 : D3D11_CPU_ACCESS_WRITE;
	desc.StructureByteStride = sizeof(T);
	desc.ByteWidth = (UINT)(sizeof(T) * count);
	return desc;
}

/**
 * @brief Create a structured buffer description (alternative signature)
 * @tparam T Structure type for buffer elements
 * @param a_count Number of elements (default: 1)
 * @param cpu_access If true, allows CPU write access
 * @return Configured D3D11_BUFFER_DESC for a structured buffer
 *
 * CPU-accessible buffers use D3D11_USAGE_DYNAMIC for efficient updates.
 * Non-CPU-accessible buffers support UAV for compute shader writes.
 */
template <typename T>
D3D11_BUFFER_DESC StructuredBufferDesc(UINT a_count = 1, bool cpu_access = true)
{
	D3D11_BUFFER_DESC desc{};
	ZeroMemory(&desc, sizeof(desc));
	desc.Usage = cpu_access ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (!cpu_access)
		desc.BindFlags = desc.BindFlags | D3D11_BIND_UNORDERED_ACCESS;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = sizeof(T);
	desc.ByteWidth = sizeof(T) * a_count;
	return desc;
}

// ========================================
// Constant Buffer Class
// ========================================

/**
 * @class ConstantBuffer
 * @brief RAII wrapper for D3D11 constant buffers
 *
 * Manages constant buffer creation and updates with automatic resource cleanup.
 * Supports both dynamic (CPU-writable) and default (GPU-only) buffers.
 */
class ConstantBuffer
{
public:
	/**
	 * @brief Construct a constant buffer from description
	 * @param a_desc D3D11 buffer description (use ConstantBufferDesc helper)
	 *
	 * Creates the D3D11 buffer resource automatically on construction
	 */
	explicit ConstantBuffer(D3D11_BUFFER_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, resource.put()));
	}

	/**
	 * @brief Get the underlying D3D11 buffer
	 * @return Raw pointer to ID3D11Buffer for binding to pipeline
	 */
	ID3D11Buffer* CB() const { return resource.get(); }

	/**
	 * @brief Update buffer contents from memory
	 * @param src_data Pointer to source data
	 * @param data_size Size of data in bytes
	 *
	 * Dynamic buffers use Map/Unmap for efficient updates.
	 * Default buffers use UpdateSubresource.
	 */
	void Update(void const* src_data, size_t data_size)
	{
		ID3D11DeviceContext* ctx = reinterpret_cast<ID3D11DeviceContext*>(RE::BSGraphics::GetRendererData()->context);
		if (desc.Usage & D3D11_USAGE_DYNAMIC) {
			D3D11_MAPPED_SUBRESOURCE mapped_buffer{};
			ZeroMemory(&mapped_buffer, sizeof(D3D11_MAPPED_SUBRESOURCE));
			DX::ThrowIfFailed(ctx->Map(resource.get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped_buffer));
			memcpy(mapped_buffer.pData, src_data, data_size);
			ctx->Unmap(resource.get(), 0);
		} else
			ctx->UpdateSubresource(resource.get(), 0, nullptr, src_data, 0, 0);
	}

	/**
	 * @brief Update buffer from typed data
	 * @tparam T Type of data structure
	 * @param src_data Reference to data structure
	 */
	template <typename T>
	void Update(T const& src_data)
	{
		Update(&src_data, sizeof(T));
	}

private:
	winrt::com_ptr<ID3D11Buffer> resource;  ///< D3D11 buffer resource (automatically released)
	D3D11_BUFFER_DESC desc;                 ///< Buffer description
};

// ========================================
// Structured Buffer Class
// ========================================

/**
 * @class StructuredBuffer
 * @brief RAII wrapper for D3D11 structured buffers
 *
 * Manages structured buffer creation with SRV/UAV support and updates.
 * Structured buffers store arrays of structures accessible in shaders.
 * Supports multiple views for different shader stages or access patterns.
 */
class StructuredBuffer
{
public:
	/**
	 * @brief Construct a structured buffer from description
	 * @param a_desc D3D11 buffer description (use StructuredBufferDesc helper)
	 * @param a_count Number of elements in the buffer
	 *
	 * Creates the D3D11 buffer resource. Call CreateSRV/CreateUAV to create views.
	 */
	StructuredBuffer(D3D11_BUFFER_DESC const& a_desc, UINT a_count) :
		desc(a_desc), count(a_count)
	{
		auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateBuffer(&desc, nullptr, resource.put()));
	}

	/**
	 * @brief Get shader resource view
	 * @param i View index (default: 0)
	 * @return Pointer to SRV for binding to shader stages
	 */
	ID3D11ShaderResourceView* SRV(size_t i = 0) const { return srvs[i].get(); }

	/**
	 * @brief Get unordered access view
	 * @param i View index (default: 0)
	 * @return Pointer to UAV for binding to compute shaders
	 */
	ID3D11UnorderedAccessView* UAV(size_t i = 0) const { return uavs[i].get(); }

	/**
	 * @brief Create a shader resource view for the buffer
	 *
	 * Creates an SRV that allows shaders to read from the buffer.
	 * Can be called multiple times to create views with different settings.
	 */
	virtual void CreateSRV()
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
		srv_desc.Format = DXGI_FORMAT_UNKNOWN;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		srv_desc.Buffer.FirstElement = 0;
		srv_desc.Buffer.NumElements = count;
		winrt::com_ptr<ID3D11ShaderResourceView> srv;
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &srv_desc, srv.put()));
		srvs.push_back(srv);
	}

	/**
	 * @brief Create an unordered access view for the buffer
	 *
	 * Creates a UAV that allows compute shaders to read/write the buffer.
	 * Requires D3D11_BIND_UNORDERED_ACCESS flag in buffer description.
	 */
	virtual void CreateUAV()
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
		uav_desc.Format = DXGI_FORMAT_UNKNOWN;
		uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav_desc.Buffer.Flags = 0;
		uav_desc.Buffer.FirstElement = 0;
		uav_desc.Buffer.NumElements = count;
		winrt::com_ptr<ID3D11UnorderedAccessView> uav;
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &uav_desc, uav.put()));
		uavs.push_back(uav);
	}

	/**
	 * @brief Update buffer contents from memory
	 * @param src_data Pointer to source data
	 * @param data_size Size of data (unused, uses buffer size)
	 *
	 * Uses Map/Unmap with WRITE_DISCARD for efficient updates.
	 * Only works with dynamic buffers.
	 */
	void Update(void const* src_data, [[maybe_unused]] size_t data_size)
	{
		ID3D11DeviceContext* ctx = reinterpret_cast<ID3D11DeviceContext*>(RE::BSGraphics::GetRendererData()->context);
		D3D11_MAPPED_SUBRESOURCE mapped_buffer{};
		ZeroMemory(&mapped_buffer, sizeof(D3D11_MAPPED_SUBRESOURCE));
		DX::ThrowIfFailed(ctx->Map(resource.get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped_buffer));
		memcpy(mapped_buffer.pData, src_data, desc.ByteWidth);
		ctx->Unmap(resource.get(), 0);
	}

	/**
	 * @brief Update buffer from array of typed data
	 * @tparam T Type of data structure
	 * @param src_data Reference to first element
	 * @param count Number of elements to copy
	 */
	template <typename T>
	void UpdateList(T const& src_data, std::int64_t count)
	{
		Update(&src_data, sizeof(T) * count);
	}

	std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> srvs;   ///< Shader resource views (automatically released)
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> uavs;  ///< Unordered access views (automatically released)

private:
	winrt::com_ptr<ID3D11Buffer> resource;  ///< D3D11 buffer resource (automatically released)
	D3D11_BUFFER_DESC desc;                 ///< Buffer description
	UINT count;                             ///< Number of elements
};

// ========================================
// Generic Buffer Class
// ========================================

/**
 * @class Buffer
 * @brief Generic RAII wrapper for D3D11 buffers with flexible view creation
 *
 * Provides low-level buffer management with custom SRV/UAV creation.
 * Use ConstantBuffer or StructuredBuffer for specific use cases.
 */
class Buffer
{
public:
	/**
	 * @brief Construct a buffer from description
	 * @param a_desc D3D11 buffer description
	 * @param a_init Optional initial data (default: nullptr)
	 *
	 * Creates the D3D11 buffer resource with optional initialization data.
	 */
	explicit Buffer(D3D11_BUFFER_DESC const& a_desc, D3D11_SUBRESOURCE_DATA* a_init = nullptr) :
		desc(a_desc)
	{
		auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateBuffer(&desc, a_init, resource.put()));
	}

	/**
	 * @brief Create a custom shader resource view
	 * @param a_desc SRV description defining format and access
	 *
	 * Allows custom SRV configuration (format, element range, etc.)
	 */
	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
	}

	/**
	 * @brief Create a custom unordered access view
	 * @param a_desc UAV description defining format and access
	 *
	 * Allows custom UAV configuration for compute shader access
	 */
	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
	}

	/**
	 * @brief Explicitly release all resources
	 *
	 * Manually destroys all views and buffer resource.
	 * Automatic cleanup happens in destructor via smart pointers.
	 */
	void Reset()
	{
		uav = nullptr;
		srv = nullptr;
		resource = nullptr;
	}

	D3D11_BUFFER_DESC desc;                           ///< Buffer description
	winrt::com_ptr<ID3D11Buffer> resource;            ///< D3D11 buffer resource (automatically released)
	winrt::com_ptr<ID3D11ShaderResourceView> srv;     ///< Shader resource view (automatically released)
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;    ///< Unordered access view (automatically released)
};

// ========================================
// Texture Classes
// ========================================

/**
 * @class Texture1D
 * @brief RAII wrapper for D3D11 1D textures
 *
 * Manages 1D texture resources with SRV/UAV/RTV support.
 * Used for lookup tables, gradients, and 1D data arrays.
 */
class Texture1D
{
public:
	/**
	 * @brief Construct a 1D texture from description
	 * @param a_desc D3D11 texture description
	 *
	 * Creates the D3D11 1D texture resource.
	 */
	explicit Texture1D(D3D11_TEXTURE1D_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateTexture1D(&desc, nullptr, resource.put()));
	}

	/**
	 * @brief Create a shader resource view
	 * @param a_desc SRV description defining format and mip levels
	 */
	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
	}

	/**
	 * @brief Create an unordered access view
	 * @param a_desc UAV description for compute shader access
	 */
	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
	}

	/**
	 * @brief Create a render target view
	 * @param a_desc RTV description for rendering output
	 */
	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, rtv.put()));
	}

	/**
	 * @brief Explicitly release all resources
	 *
	 * Manually destroys all views and texture resource.
	 * Automatic cleanup happens in destructor via smart pointers.
	 */
	void Reset()
	{
		rtv = nullptr;
		uav = nullptr;
		srv = nullptr;
		resource = nullptr;
	}

	D3D11_TEXTURE1D_DESC desc;                          ///< Texture description
	winrt::com_ptr<ID3D11Texture1D> resource;           ///< D3D11 texture resource (automatically released)
	winrt::com_ptr<ID3D11ShaderResourceView> srv;       ///< Shader resource view (automatically released)
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;      ///< Unordered access view (automatically released)
	winrt::com_ptr<ID3D11RenderTargetView> rtv;         ///< Render target view (automatically released)
};

/**
 * @class Texture2D
 * @brief RAII wrapper for D3D11 2D textures
 *
 * Manages 2D texture resources with full view support (SRV/UAV/RTV/DSV).
 * Used for render targets, depth buffers, color textures, and compute shader outputs.
 * Supports construction from existing textures for wrapping game resources.
 */
class Texture2D
{
public:
	/**
	 * @brief Construct a 2D texture from description
	 * @param a_desc D3D11 texture description
	 *
	 * Creates a new D3D11 2D texture resource.
	 */
	explicit Texture2D(D3D11_TEXTURE2D_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateTexture2D(&desc, nullptr, resource.put()));
	}

	/**
	 * @brief Construct from existing texture resource
	 * @param a_resource Existing ID3D11Texture2D (takes ownership)
	 *
	 * Wraps an existing texture (e.g., from swap chain or game engine).
	 * Takes ownership of the provided resource.
	 */
	explicit Texture2D(ID3D11Texture2D* a_resource)
	{
		a_resource->GetDesc(&desc);
		resource.attach(a_resource);
	}

	/**
	 * @brief Create a shader resource view
	 * @param a_desc SRV description defining format and mip levels
	 */
	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
	}

	/**
	 * @brief Create an unordered access view
	 * @param a_desc UAV description for compute shader access
	 */
	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
	}

	/**
	 * @brief Create a render target view
	 * @param a_desc RTV description for rendering output
	 */
	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, rtv.put()));
	}

	/**
	 * @brief Create a depth-stencil view
	 * @param a_desc DSV description for depth buffer usage
	 */
	void CreateDSV(D3D11_DEPTH_STENCIL_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateDepthStencilView(resource.get(), &a_desc, dsv.put()));
	}

	/**
	 * @brief Explicitly release all resources
	 *
	 * Manually destroys all views and texture resource.
	 * Automatic cleanup happens in destructor via smart pointers.
	 * Setting the owning std::unique_ptr to nullptr will call this automatically.
	 */
	void Reset()
	{
		dsv = nullptr;
		rtv = nullptr;
		uav = nullptr;
		srv = nullptr;
		resource = nullptr;
	}

	D3D11_TEXTURE2D_DESC desc;                          ///< Texture description
	winrt::com_ptr<ID3D11Texture2D> resource;           ///< D3D11 texture resource (automatically released)
	winrt::com_ptr<ID3D11ShaderResourceView> srv;       ///< Shader resource view (automatically released)
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;      ///< Unordered access view (automatically released)
	winrt::com_ptr<ID3D11RenderTargetView> rtv;         ///< Render target view (automatically released)
	winrt::com_ptr<ID3D11DepthStencilView> dsv;         ///< Depth-stencil view (automatically released)
};

/**
 * @class Texture3D
 * @brief RAII wrapper for D3D11 3D textures (volume textures)
 *
 * Manages 3D texture resources with SRV/UAV/RTV support.
 * Used for volume data, 3D noise, atmospheric effects, and voxel grids.
 */
class Texture3D
{
public:
	/**
	 * @brief Construct a 3D texture from description
	 * @param a_desc D3D11 texture description
	 *
	 * Creates the D3D11 3D texture resource.
	 */
	explicit Texture3D(D3D11_TEXTURE3D_DESC const& a_desc) :
		desc(a_desc)
	{
		auto device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateTexture3D(&desc, nullptr, resource.put()));
	}

	/**
	 * @brief Create a shader resource view
	 * @param a_desc SRV description defining format and mip levels
	 */
	void CreateSRV(D3D11_SHADER_RESOURCE_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateShaderResourceView(resource.get(), &a_desc, srv.put()));
	}

	/**
	 * @brief Create an unordered access view
	 * @param a_desc UAV description for compute shader access
	 */
	void CreateUAV(D3D11_UNORDERED_ACCESS_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateUnorderedAccessView(resource.get(), &a_desc, uav.put()));
	}

	/**
	 * @brief Create a render target view
	 * @param a_desc RTV description for rendering output
	 */
	void CreateRTV(D3D11_RENDER_TARGET_VIEW_DESC const& a_desc)
	{
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateRenderTargetView(resource.get(), &a_desc, rtv.put()));
	}

	/**
	 * @brief Explicitly release all resources
	 *
	 * Manually destroys all views and texture resource.
	 * Automatic cleanup happens in destructor via smart pointers.
	 */
	void Reset()
	{
		rtv = nullptr;
		uav = nullptr;
		srv = nullptr;
		resource = nullptr;
	}

	D3D11_TEXTURE3D_DESC desc;                          ///< Texture description
	winrt::com_ptr<ID3D11Texture3D> resource;           ///< D3D11 texture resource (automatically released)
	winrt::com_ptr<ID3D11ShaderResourceView> srv;       ///< Shader resource view (automatically released)
	winrt::com_ptr<ID3D11UnorderedAccessView> uav;      ///< Unordered access view (automatically released)
	winrt::com_ptr<ID3D11RenderTargetView> rtv;         ///< Render target view (automatically released)
};

}
