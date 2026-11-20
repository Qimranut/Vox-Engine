#pragma once

namespace scheduler
{
	class ISystem
	{
	public:

		virtual ~ISystem() = default;
		virtual void update(double) = 0;
		virtual const char* getName() const = 0;
	};
}
