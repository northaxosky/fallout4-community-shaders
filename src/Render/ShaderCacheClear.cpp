#include "Render/ShaderCacheClear.h"

#include "Render/ShaderInjection.h"
#include "Utils/ShaderCache/CacheStorage.h"

#include <system_error>

namespace cs::engine
{
	ShaderCacheClearResult ClearShaderCache(ShaderCacheScope a_scope)
	{
		ShaderCacheClearResult result;
		if (a_scope == ShaderCacheScope::kMemoryAndDisk) {
			result.root = shader_cache::DefaultCacheRoot();
			std::error_code error;
			result.removed = shader_cache::ClearCacheRecords(result.root, error);
			if (error) {
				result.error = error.message();
				return result;
			}
		}
		InvalidateNativeShaderVariantCompilations();
		PrewarmShaderInjectionVariants();
		result.succeeded = true;
		return result;
	}
}
