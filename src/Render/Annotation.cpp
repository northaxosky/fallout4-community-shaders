#include "Render/Annotation.h"

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>

#include <winrt/base.h>

namespace cs::render::annotation
{
	namespace
	{
		constexpr std::size_t kWideNameCapacity = 256;
		winrt::com_ptr<ID3DUserDefinedAnnotation> annotation;

		std::wstring_view ToWide(
			std::string_view a_name,
			std::array<wchar_t, kWideNameCapacity>& a_storage) noexcept
		{
			if (a_name.empty())
				return {};

			auto sourceLength = static_cast<int>(
				(std::min)(a_name.size(), a_storage.size() - 1));
			while (sourceLength > 0) {
				const int converted = MultiByteToWideChar(
					CP_UTF8,
					MB_ERR_INVALID_CHARS,
					a_name.data(),
					sourceLength,
					a_storage.data(),
					static_cast<int>(a_storage.size() - 1));
				if (converted > 0) {
					a_storage[static_cast<std::size_t>(converted)] = L'\0';
					return {
						a_storage.data(),
						static_cast<std::size_t>(converted)
					};
				}
				--sourceLength;
			}
			return {};
		}
	}

	void Initialize(ID3D11DeviceContext* a_context) noexcept
	{
		if (a_context && !annotation) {
			std::ignore = a_context->QueryInterface(
				IID_PPV_ARGS(annotation.put()));
		}
	}

	ScopedEvent::ScopedEvent(std::string_view a_name) noexcept
	{
		if (!annotation || !annotation->GetStatus())
			return;

		std::array<wchar_t, kWideNameCapacity> wideName{};
		if (const auto name = ToWide(a_name, wideName); !name.empty()) {
			if (annotation->BeginEvent(name.data()) >= 0)
				_d3d11 = annotation.get();
		}
	}

	ScopedEvent::ScopedEvent(
		ID3D12GraphicsCommandList* a_commandList,
		std::string_view a_name) noexcept
	{
		if (!a_commandList || a_name.empty())
			return;
		a_commandList->BeginEvent(
			0,
			a_name.data(),
			static_cast<UINT>(std::min<std::size_t>(
				a_name.size(),
				(std::numeric_limits<UINT>::max)())));
		_d3d12 = a_commandList;
	}

	ScopedEvent::~ScopedEvent() noexcept
	{
		if (_d3d11)
			_d3d11->EndEvent();
		if (_d3d12)
			_d3d12->EndEvent();
	}

	void SetMarker(std::string_view a_name) noexcept
	{
		if (!annotation || !annotation->GetStatus())
			return;

		std::array<wchar_t, kWideNameCapacity> wideName{};
		if (const auto name = ToWide(a_name, wideName); !name.empty())
			annotation->SetMarker(name.data());
	}

	void SetMarker(
		ID3D12GraphicsCommandList* a_commandList,
		std::string_view a_name) noexcept
	{
		if (!a_commandList || a_name.empty())
			return;
		a_commandList->SetMarker(
			0,
			a_name.data(),
			static_cast<UINT>(std::min<std::size_t>(
				a_name.size(),
				(std::numeric_limits<UINT>::max)())));
	}

	void SetName(ID3D11DeviceChild* a_object, std::string_view a_name) noexcept
	{
		if (!a_object || a_name.empty())
			return;
		std::ignore = a_object->SetPrivateData(
			WKPDID_D3DDebugObjectName,
			static_cast<UINT>(std::min<std::size_t>(
				a_name.size(),
				(std::numeric_limits<UINT>::max)())),
			a_name.data());
	}

	void SetName(ID3D12Object* a_object, std::string_view a_name) noexcept
	{
		if (!a_object || a_name.empty())
			return;
		std::array<wchar_t, kWideNameCapacity> wideName{};
		if (const auto name = ToWide(a_name, wideName); !name.empty())
			std::ignore = a_object->SetName(name.data());
	}
}
