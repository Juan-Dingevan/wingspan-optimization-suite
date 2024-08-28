#if !defined(WINGSPAN_CONSTANT_ADDITION_FINDER_H_)
#define WINGSPAN_CONSTANT_ADDITION_FINDER_H_

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"

#include "plugin-registration.h"

namespace ws {
	class ConstantOperationFinder : public llvm::AnalysisInfoMixin<ConstantOperationFinder> {
	public:
		using Result = llvm::SmallVector<llvm::Instruction*, 0>;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "constant-operation-finder"; }

	private:
		friend class llvm::AnalysisInfoMixin<ConstantOperationFinder>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};
}


#endif