#include "RemoteControl.h"

#include "RemoteControlTools.h"

#include "Host/HostClient.h"
#include "Log.h"
#include "Render/Annotation.h"
#include "Render/FrameBoundary.h"

#include <DearModdingUI/Client.h>
#include <DevBenchAPI.h>

#ifdef NVPERF_ENABLED
#	include "Features/RemoteControl/NvPerf.h"
#endif

#include <filesystem>
#include <format>
#include <fstream>

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.remotecontrol");

		std::filesystem::path RuntimeJsonPath()
		{
			wchar_t base[MAX_PATH];
			const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
			if (length == 0 || length >= MAX_PATH)
				return {};
			return std::filesystem::path(base) / L"devbench" / L"fo4" / L"runtime.json";
		}
	}

	RemoteControl* RemoteControl::GetSingleton()
	{
		static RemoteControl singleton;
		return &singleton;
	}

	void RemoteControl::Load()
	{
#ifdef NVPERF_ENABLED
		// Main::Swap renders and presents, so both Present edges sit after it.
		if (!render::RegisterPostSwap(*this, [] {
				NvPerf::OnPresentBegin();
				NvPerf::OnPresentEnd();
			}))
			L->warn("Nsight Perf frame boundary callback was not registered");
		render::annotation::SetRangeSink({ &NvPerf::RangesActive, &NvPerf::PushRange, &NvPerf::PopRange });
#endif
	}

	void RemoteControl::OnPostPostLoad()
	{
		auto* host = DevBenchAPI::GetDevBenchInterface001();
		if (!host) {
			L->info("DevBench host not present; Remote Control tools are not registered");
			return;
		}
		_hostBuild = host->GetBuildNumber();
		_registeredTools = remote_control::RegisterTools(*host);
		SubscribeToEvents();
		_host.store(host, std::memory_order_release);
		L->info("DevBench build {} present; registered {} tools", _hostBuild, _registeredTools.size());
	}

	void RemoteControl::SubscribeToEvents()
	{
		remote_control::SubscribeShaderCacheEvents();
		remote_control::SubscribeCaptureEvents();
		remote_control::SubscribeNvPerfEvents();
	}

	void RemoteControl::EmitEvent(const char* a_topic, const nlohmann::json& a_payload, bool a_bypassMute) const
	{
		auto* host = _host.load(std::memory_order_acquire);
		if (!host || (!a_bypassMute && !IsEnabled()))
			return;
		const auto text = a_payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
		host->EmitEvent(a_topic, text.c_str());
	}

	int RemoteControl::ReadHostPort()
	{
		const auto now = std::chrono::steady_clock::now();
		// DevBench may bind after the page first opens.
		if (_cachedPort >= 0 && now - _portReadAt < std::chrono::seconds(2))
			return _cachedPort;
		_portReadAt = now;
		_cachedPort = 0;
		if (const auto path = RuntimeJsonPath(); !path.empty()) {
			std::ifstream input(path);
			const auto runtime = nlohmann::json::parse(input, nullptr, false);
			if (runtime.is_object())
				_cachedPort = runtime.value("port", 0);
		}
		return _cachedPort;
	}

	void RemoteControl::DrawSettings()
	{
		DrawHostStatus();
		// The Nsight Perf panel works without the DevBench host.
		remote_control::DrawNvPerfPanel();
	}

	void RemoteControl::DrawHostStatus()
	{
		auto& client = host::HostClient::Get().Client();
		const auto draw = [&](std::string_view a_text, dmui::TextTone a_tone) {
			if (!dmui::DrawStyledText(client, a_text, { .tone = a_tone, .wrapped = true }))
				L->warn("DearModdingUI draw failed: {}", DMUI_ResultToString(client.LastResult()));
		};

		if (!IsEnabled())
			draw("Remote Control is disabled; every tool answers with an error.", dmui::TextTone::kStatusDisable);
		if (!_host.load(std::memory_order_acquire)) {
			draw("DevBench host not detected. Install the DevBench plugin and restart; the tools register automatically.",
				dmui::TextTone::kStatusWarning);
			return;
		}

		draw(std::format("DevBench host present (build {})", _hostBuild), dmui::TextTone::kStatusSuccess);
		if (const auto port = ReadHostPort(); port > 0)
			draw(std::format("Host bound on port {}", port), dmui::TextTone::kInherit);
		else
			draw("Port unknown; DevBench writes it to %LOCALAPPDATA%\\devbench\\fo4\\runtime.json once it binds.", dmui::TextTone::kMuted);

		draw("Registered tools:", dmui::TextTone::kInherit);
		for (const auto tool : _registeredTools) {
			if (!client.DrawBulletText(std::string(tool).c_str()))
				L->warn("DearModdingUI draw failed: {}", DMUI_ResultToString(client.LastResult()));
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister()
			{
				cs::FeatureManager::Get().Register(RemoteControl::GetSingleton());
			}
		};
		static AutoRegister autoRegister;
	}
}
