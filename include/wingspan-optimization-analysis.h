#if !defined(WINGSPAN_OPTIMIZATION_ANALYSIS_H_)
#define WINGSPAN_OPTIMIZATION_ANALYSIS_H_

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "plugin-registration.h"

namespace ws {
	class OptimizationInfo {
		public:
			std::string name;
			int functions;
			int optimizedFunctions;
			int deadFunctions;
			int basicBlocks;
			int instructions;
			int memoryAccesses;
			int branches;
			int conditionalBranches;
			int functionCalls;

			OptimizationInfo() :
				name(""),
				functions(0),
				deadFunctions(0),
				optimizedFunctions(0),
				basicBlocks(0),
				instructions(0),
				memoryAccesses(0),
				conditionalBranches(0),
				branches(0),
				functionCalls(0)
			{}
	};

	class OptimizationAnalyzer : public llvm::AnalysisInfoMixin<OptimizationAnalyzer> {
	public:
		using Result = OptimizationInfo;

		Result run(llvm::Module& m, llvm::ModuleAnalysisManager& /*unused*/);

		static inline constexpr llvm::StringRef name() { return "optimization-analysis"; }

	private:
		friend class llvm::AnalysisInfoMixin<OptimizationAnalyzer>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::ModuleAnalysisManager& am);
	};
};

#endif