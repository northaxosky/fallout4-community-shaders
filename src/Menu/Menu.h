#pragma once

#include "Host/HostRuntimeModel.h"
#include "Menu/DebugViewSelection.h"

#include <DearModdingUI/Client.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace cs
{
	class Feature;

	class Menu
	{
	public:
		static Menu& Get();

		void Load();
		bool Save();
		void ApplyDebugViewSelections();
		void DrawDebugViewSelector(const Feature& a_feature);

		void DrawHome(dmui::Client& a_client);
		void DrawAdvanced(dmui::Client& a_client);
		void DrawPresets(dmui::Client& a_client);
		void DrawChangelog(dmui::Client& a_client);
		void ObserveHostFrame(dmui::Client& a_client);
		void OnHostDeviceReady() noexcept;

		void RequestClearShaderCache() noexcept;
		void RequestClearRenderDocCaptures() noexcept;
		void ReleaseDebugImages() noexcept;

		static void ShowToast(
			std::string a_text,
			double a_durationSec = 3.0,
			DMUI_StatusSeverity a_severity = DMUI_STATUS_SEVERITY_INFO,
			std::string a_title = {});

	private:
		Menu();

		enum class ClearRequest : std::uint8_t
		{
			kNone,
			kClearCache,
			kClearRenderDocCaptures
		};

		struct DebugImage
		{
			ID3D11ShaderResourceView* source{};
			std::uint32_t width{};
			std::uint32_t height{};
			std::uint64_t generation{};
			std::uint64_t retryAfterFrame{};
			host::ImageImportFailure importFailure{
				host::ImageImportFailure::kNone
			};
			std::optional<DMUI_Result> loggedFailure;
			dmui::ImageResource image;
		};

		void SetDebugViewSelection(const Feature& a_feature, std::string_view a_view);
		void DrawFeatureOverview(dmui::Client& a_client);
		void DrawShaderSettings(dmui::Client& a_client);
		void DrawDebugTexture(dmui::Client& a_client, const Feature& a_feature);
		void StartDialog(
			dmui::Client& a_client,
			const DMUI_DialogDescriptor& a_descriptor,
			dmui::DialogSession::Submit a_submit);
		bool ClearShaderCache(std::string& a_error);
		bool CheckHostResult(
			dmui::Client& a_client,
			bool a_succeeded,
			std::string_view a_operation);

		debug_view::SelectionState _debugViews;
		host::StartupLoadSnapshot _startupLoads;
		std::unordered_map<std::string, DebugImage> _debugImages;
		std::unordered_map<std::string, DMUI_Result> _hostCallFailures;
		dmui::DialogSession _dialog;
		std::atomic<ClearRequest> _clearRequested{ ClearRequest::kNone };
		std::uint64_t _hostFrameSerial{};
		std::uint64_t _debugImageGeneration{};
	};
}
