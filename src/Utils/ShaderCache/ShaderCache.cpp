#include "Utils/ShaderCache/ShaderCache.h"

#include "Utils/ShaderCache/CacheStorage.h"
#include "Utils/ShaderCache/CompilerIdentity.h"
#include "Utils/ShaderCache/SourceCompile.h"

#include <atomic>
#include <span>
#include <utility>

namespace cs::shader_cache
{
	namespace
	{
		std::atomic<std::uint64_t> g_hit{ 0 };
		std::atomic<std::uint64_t> g_absent{ 0 };
		std::atomic<std::uint64_t> g_stale{ 0 };
		std::atomic<std::uint64_t> g_rejected{ 0 };
		std::atomic<std::uint64_t> g_written{ 0 };

		struct LookupResult
		{
			CacheDisposition          disposition = CacheDisposition::kAbsent;
			std::vector<std::uint8_t> payload;
			std::string               note;
		};

		LookupResult Reject(CacheDisposition a_disposition, std::string a_note)
		{
			return { a_disposition, {}, std::move(a_note) };
		}

		void CountDisposition(CacheDisposition a_disposition) noexcept
		{
			switch (a_disposition) {
			case CacheDisposition::kHit:
				g_hit.fetch_add(1, std::memory_order_relaxed);
				break;
			case CacheDisposition::kAbsent:
				g_absent.fetch_add(1, std::memory_order_relaxed);
				break;
			case CacheDisposition::kStale:
				g_stale.fetch_add(1, std::memory_order_relaxed);
				break;
			case CacheDisposition::kRejected:
				g_rejected.fetch_add(1, std::memory_order_relaxed);
				break;
			default:
				break;
			}
		}

		LookupResult LookUpRecord(
			const std::filesystem::path&  a_recordPath,
			const ShaderRecipe&           a_recipe,
			std::span<const std::uint8_t> a_recipeBytes,
			const sha256::Sha256Result&   a_logicalDigest,
			RevalidationContext*          a_context)
		{
			std::vector<std::uint8_t> bytes;
			const auto readStatus = ReadFileBytes(a_recordPath, kMaxRecordBytes, bytes);
			if (readStatus == FileReadStatus::kMissing)
				return Reject(CacheDisposition::kAbsent, {});
			if (readStatus != FileReadStatus::kOk)
				return Reject(CacheDisposition::kRejected, "record unreadable");

			ShaderCacheRecord record;
			const auto        status = ParseShaderCacheRecord(bytes, record);
			if (status != RecordStatus::kOk)
				return Reject(CacheDisposition::kRejected, DescribeRecordStatus(status));

			// path match alone does not authenticate the request
			if (record.logicalDigest != a_logicalDigest)
				return Reject(CacheDisposition::kRejected, "logical digest mismatch");
			if (record.stage != a_recipe.stage)
				return Reject(CacheDisposition::kRejected, "stage mismatch");
			if (record.profile != a_recipe.profile)
				return Reject(CacheDisposition::kRejected, "profile mismatch");
			if (record.manifest.rootLocator != EncodeLocator(a_recipe.source))
				return Reject(CacheDisposition::kRejected, "source locator mismatch");

			const auto revalidation =
				RevalidateDependencyManifest(record.manifest, a_context);
			if (!revalidation.Valid()) {
				return Reject(
					CacheDisposition::kStale,
					std::string(DescribeRevalidation(revalidation.status)) + " ("
						+ revalidation.detail + ")");
			}

			if (ComputeFullRecipeDigest(a_recipeBytes, record.dependencyDigest)
				!= record.recipeDigest) {
				return Reject(CacheDisposition::kRejected, "recipe digest mismatch");
			}

			LookupResult hit;
			hit.disposition = CacheDisposition::kHit;
			hit.payload     = std::move(record.payload);
			return hit;
		}

		void PublishRecord(
			const std::filesystem::path&  a_recordPath,
			const ShaderRecipe&           a_recipe,
			const sha256::Sha256Result&   a_logicalDigest,
			std::span<const std::uint8_t> a_recipeBytes,
			const SourceCompileOutcome&   a_compiled,
			ShaderCacheOutcome&           a_outcome) noexcept
		{
			try {
				std::vector<std::uint8_t> manifestBytes;
				if (!SerializeDependencyManifest(a_compiled.manifest, manifestBytes)) {
					a_outcome.cacheNote = "manifest cannot be serialized";
					return;
				}

				ShaderCacheRecord record;
				record.logicalDigest    = a_logicalDigest;
				record.dependencyDigest =
					sha256::Sha256Compute(manifestBytes.data(), manifestBytes.size());
				record.recipeDigest =
					ComputeFullRecipeDigest(a_recipeBytes, record.dependencyDigest);
				if (sha256::Sha256IsZero(record.dependencyDigest)
					|| sha256::Sha256IsZero(record.recipeDigest)) {
					a_outcome.cacheNote = "record digest unavailable";
					return;
				}
				record.stage    = a_recipe.stage;
				record.profile  = a_recipe.profile;
				record.manifest = a_compiled.manifest;
				record.payload  = a_compiled.bytecode;

				std::vector<std::uint8_t> recordBytes;
				if (!SerializeShaderCacheRecord(record, recordBytes)) {
					a_outcome.cacheNote = "record exceeds serialization limits";
					return;
				}

				std::string writeError;
				if (!WriteRecordAtomically(a_recordPath, recordBytes, writeError))
					a_outcome.cacheNote = std::move(writeError);
				else
					a_outcome.recordWritten = true;
				if (a_outcome.recordWritten)
					g_written.fetch_add(1, std::memory_order_relaxed);
			} catch (...) {
				a_outcome.cacheNote = "record publication failed";
			}
		}
	}

	const char* DescribeDisposition(CacheDisposition a_disposition) noexcept
	{
		switch (a_disposition) {
		case CacheDisposition::kHit:
			return "hit";
		case CacheDisposition::kNoCompilerIdentity:
			return "no compiler identity";
		case CacheDisposition::kAbsent:
			return "absent";
		case CacheDisposition::kRejected:
			return "rejected";
		case CacheDisposition::kStale:
			return "stale";
		case CacheDisposition::kBypassed:
			return "bypassed";
		}
		return "unknown";
	}

	void ResetShaderCacheCounters() noexcept
	{
		g_hit.store(0, std::memory_order_relaxed);
		g_absent.store(0, std::memory_order_relaxed);
		g_stale.store(0, std::memory_order_relaxed);
		g_rejected.store(0, std::memory_order_relaxed);
		g_written.store(0, std::memory_order_relaxed);
	}

	ShaderCacheCounters GetShaderCacheCounters() noexcept
	{
		return {
			g_hit.load(std::memory_order_relaxed),
			g_absent.load(std::memory_order_relaxed),
			g_stale.load(std::memory_order_relaxed),
			g_rejected.load(std::memory_order_relaxed),
			g_written.load(std::memory_order_relaxed)
		};
	}

	std::string DescribeCacheOutcome(const ShaderCacheOutcome& a_outcome)
	{
		std::string description =
			DescribeDisposition(a_outcome.disposition);
		if (!a_outcome.cacheNote.empty())
			description += " (" + a_outcome.cacheNote + ")";
		return description;
	}

	ShaderCacheOutcome LoadOrCompileShader(
		const ShaderRecipe&       a_recipe,
		const ShaderCacheOptions& a_options,
		CacheMode                 a_mode)
	{
		ShaderCacheOutcome outcome;
		try {
			const auto& identity = GetD3DCompilerIdentity();
			if (!identity.established) {
				outcome.disposition = CacheDisposition::kNoCompilerIdentity;
				outcome.cacheNote   = "d3dcompiler identity unavailable";
			}

			std::vector<std::uint8_t> recipeBytes;
			sha256::Sha256Result      logicalDigest{};
			bool                      cacheAvailable = identity.established;
			if (cacheAvailable) {
				recipeBytes   = EncodeShaderRecipe(a_recipe, identity);
				logicalDigest = ComputeLogicalDigest(recipeBytes);
				if (sha256::Sha256IsZero(logicalDigest)) {
					cacheAvailable = false;
					outcome.disposition = CacheDisposition::kBypassed;
					outcome.cacheNote = "recipe digest unavailable";
				}
			}
			if (cacheAvailable) {
				outcome.recordPath = BuildRecordPath(
					a_options.cacheRoot.empty() ? DefaultCacheRoot() : a_options.cacheRoot,
					a_recipe.stage,
					logicalDigest);

				if (a_mode == CacheMode::kReadWrite) {
					LookupResult lookup;
					try {
						lookup = LookUpRecord(
							outcome.recordPath,
							a_recipe,
							recipeBytes,
							logicalDigest,
							a_options.revalidation);
					} catch (...) {
						lookup = Reject(
							CacheDisposition::kRejected,
							"record validation failed");
					}
					outcome.disposition = lookup.disposition;
					outcome.cacheNote   = std::move(lookup.note);
					CountDisposition(outcome.disposition);
					if (lookup.disposition == CacheDisposition::kHit) {
						outcome.succeeded = true;
						outcome.origin    = CompileOrigin::kCacheHit;
						outcome.bytecode  = std::move(lookup.payload);
						return outcome;
					}
				} else {
					outcome.disposition = CacheDisposition::kBypassed;
				}
			}

			auto compiled = CompileSourceWithManifest(a_recipe);
			if (!compiled.succeeded) {
				outcome.error = std::move(compiled.error);
				return outcome;
			}

			outcome.succeeded = true;
			outcome.origin    = CompileOrigin::kFreshCompile;

			// publication failure must not discard compiled bytecode
			if (cacheAvailable) {
				PublishRecord(
					outcome.recordPath,
					a_recipe,
					logicalDigest,
					recipeBytes,
					compiled,
					outcome);
			}

			outcome.bytecode = std::move(compiled.bytecode);
			return outcome;
		} catch (const std::exception& exception) {
			outcome.succeeded = false;
			outcome.bytecode.clear();
			outcome.error = exception.what();
			return outcome;
		} catch (...) {
			outcome.succeeded = false;
			outcome.bytecode.clear();
			outcome.error = "unexpected shader cache failure";
			return outcome;
		}
	}
}
