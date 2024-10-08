#if !defined(WINGSPAN_CONSTANTS_H_)
#define WINGSPAN_CONSTANTS_H_

namespace ws {
	namespace constants {
		const bool	AGGRESSIVE_OPTIMIZATIONS_ENABLED = true;
		const bool	FLOATING_POINT_ARITHMETIC_IS_BUGGED = true;

		const int	LOOP_INVARIANT_RECURSION_MAX_DEPTH = 32;
		const int	MAX_NUMBER_OF_LINES_FOR_INLINING = 64;

		const int	MAX_ITERATIONS_FOR_DEAD_CODE_DETECTION = 32;
	}
}

#endif