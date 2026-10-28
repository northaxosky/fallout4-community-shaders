#pragma once

#include "Feature.h"
#include "FeatureCategories.h"

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace DevBenchAPI
{
	struct IDevBenchInterface001;
}

namespace cs::features
{
	class RemoteControl : public Feature
	{
	public:
		static RemoteControl* GetSingleton();

		std::string_view GetName() const override { return "RemoteControl"; }
		std::string_view GetDisplayName() const override { return "Remote Control"; }
		std::string GetCategory() const override { return FeatureCategories::kUtility; }
		std::string GetFeatureSummary() const override
		{
			return "Expose Community Shaders tools to the DevBench host for MCP and REST clients.";
		}

		void Load() override;
		void OnPostPostLoad() override;
		void DrawSettings() override;

		// Muted while disabled, except the bypass that announces the disable itself.
		void EmitEvent(const char* a_topic, const nlohmann::json& a_payload, bool a_bypassMute = false) const;

	private:
		RemoteControl() = default;

		void DrawHostStatus();
		void SubscribeToEvents();
		[[nodiscard]] int ReadHostPort();

		std::atomic<DevBenchAPI::IDevBenchInterface001*> _host{ nullptr };
		unsigned int _hostBuild{};
		std::vector<std::string_view> _registeredTools;

		int _cachedPort{ -1 };
		std::chrono::steady_clock::time_point _portReadAt;
	};
}
