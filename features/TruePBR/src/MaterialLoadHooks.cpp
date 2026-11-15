#include "MaterialLoadHooks.h"

#include "Format/BgsmV100.h"
#include "LoadNotices.h"
#include "Log.h"
#include "LogThrottle.h"
#include "MaterialRegistry.h"
#include "Render/HookRegistry.h"
#include "ShaderDataSwap.h"

#include "RE/B/BSMaterialDB.h"
#include "RE/B/BSShaderData.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <utility>

namespace cs::features::true_pbr
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.truepbr");

		// LoadBinary has no name of its own; Create is its only caller.
		thread_local const char* t_resourceName = nullptr;

		struct LoadContext
		{
			RE::BSShaderData& data;
			RE::BSStreamer::ScrapStreamer& stream;
			RE::BSShaderManager::ShaderEnum family;
			const char* resourceName;
			void (*runOriginal)(LoadContext&);

			void RunOriginal() { runOriginal(*this); }
		};

		struct VersionHandler
		{
			std::uint32_t version;
			void (*handle)(LoadContext&);
		};

		void Reject(const std::string& a_name, const std::string& a_reason)
		{
			Notices().Reject(a_name, a_reason);
			CS_LOG_ONCE_BY_KEY_GLOBAL(L, spdlog::level::warn, a_name,
				"TruePBR material '{}' rendered as its vanilla part: {}", a_name, a_reason);
		}

		// Part 2 follows Part 1 in the same stream, at the cursor.
		void HandleVersion100(LoadContext& a_context)
		{
			a_context.RunOriginal();

			const std::string rawName = a_context.resourceName;
			if (a_context.family != RE::BSShaderManager::ShaderEnum::kLighting) {
				CS_LOG_ONCE_BY_KEY_GLOBAL(L, spdlog::level::warn, rawName,
					"TruePBR material '{}' is not a lighting material and is left as its vanilla part.", rawName);
				return;
			}

			const auto name = CanonicalName(rawName);
			if (!name) {
				Reject(rawName, "the path has no materials\\ component");
				return;
			}

			const auto& stream = a_context.stream;
			const auto* begin = reinterpret_cast<const std::uint8_t*>(stream.buffer);
			const auto* cursor = reinterpret_cast<const std::uint8_t*>(stream.cursor);
			if (!begin || cursor < begin || cursor > begin + stream.length) {
				Reject(*name, "the stream cursor is outside the file");
				return;
			}

			auto decoded = DecodePart2({ cursor, static_cast<std::size_t>(begin + stream.length - cursor) });
			if (!decoded) {
				Reject(*name, decoded.error());
				return;
			}
			for (const auto& tag : decoded->unknownTags)
				L->debug("TruePBR material '{}' has unknown block '{}', skipped.", *name, tag);

			auto& data = a_context.data;
			if (!data.textureSet) {
				Reject(*name, "the vanilla part has no texture set");
				return;
			}
			if (data.skinTint || data.facegen || data.hair) {
				Reject(*name, "the vanilla part is skin tint, facegen or hair, which this version does not swap");
				return;
			}

			auto ingested = Records().Ingest(*name, std::move(decoded->material));
			if (!ingested) {
				Reject(*name, ingested.error().errors.front());
				return;
			}
			const auto& record = *ingested->record;
			if (ingested->outcome == IngestOutcome::kPublished) {
				for (const auto& warning : ingested->warnings)
					L->warn("TruePBR material '{}': {}", *name, warning);
				L->debug("TruePBR material '{}' is record {}.", *name, record.id);
			} else if (ingested->outcome == IngestOutcome::kChangedOnDisk) {
				CS_LOG_ONCE_BY_KEY_GLOBAL(L, spdlog::level::warn, *name,
					"TruePBR material '{}' changed on disk; restart to apply.", *name);
			}
			ShaderDataSwap(data, record);
		}

		constexpr std::array kHandlers{
			VersionHandler{ kBgsmVersion, &HandleVersion100 }
		};

		const VersionHandler* FindHandler(std::uint32_t a_version)
		{
			const auto found = std::ranges::find(kHandlers, a_version, &VersionHandler::version);
			return found == kHandlers.end() ? nullptr : &*found;
		}

		struct DBTraits_Create
		{
			static void thunk(RE::BSTSmartPointer<RE::BSResource::Stream>& a_source, const char* a_name,
				RE::BSMaterialDB::DBTraits::ArgsType& a_args, RE::BSShaderData*& a_out)
			{
				const auto* outer = std::exchange(t_resourceName, a_name);
				func(a_source, a_name, a_args, a_out);
				t_resourceName = outer;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShaderData_LoadBinary
		{
			static void thunk(RE::BSShaderData* a_data, RE::BSStreamer::ScrapStreamer& a_stream, RE::BSShaderManager::ShaderEnum a_family)
			{
				// Read before the original, which advances the cursor.
				const auto version = a_stream.buffer && t_resourceName ?
				                         ReadBgsmVersion({ reinterpret_cast<const std::uint8_t*>(a_stream.buffer), a_stream.length }) :
				                         std::nullopt;
				const auto* handler = version ? FindHandler(*version) : nullptr;
				if (!handler) {
					func(a_data, a_stream, a_family);
					return;
				}
				LoadContext context{ *a_data, a_stream, a_family, t_resourceName,
					[](LoadContext& a_self) { func(&a_self.data, a_self.stream, a_self.family); } };
				handler->handle(context);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	bool InstallMaterialLoadHooks()
	{
		static bool create = false;
		static bool load = false;
		return engine::EnsureDetourInstalled<DBTraits_Create>(create, RE::ID::BSMaterialDB::DBTraits::Create, "BSMaterialDB::DBTraits::Create") &&
		       engine::EnsureDetourInstalled<BSShaderData_LoadBinary>(load, RE::ID::BSShaderData::LoadBinary, "BSShaderData::LoadBinary");
	}
}
