#pragma once

namespace cs::features::catalog::hooks
{
	struct HookCoverage
	{
		bool vertex = false;
		bool geometry = false;
		bool geometryStreamOutput = false;
		bool pixel = false;
		bool hull = false;
		bool domain = false;
		bool compute = false;
		bool pixelBinding = false;
		bool observer = false;

		bool Complete() const noexcept
		{
			return vertex
				&& geometry
				&& geometryStreamOutput
				&& pixel
				&& hull
				&& domain
				&& compute
				&& pixelBinding
				&& observer;
		}
	};
}
