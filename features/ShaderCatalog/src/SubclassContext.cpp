#include "SubclassContext.h"

namespace cs::features::catalog::context
{
	thread_local Context g_ctx{};
	thread_local Context g_stickyCtx{};
}
