#if !defined(WINGSPAN_FIND_LOOP_INVARIANT_INSTRUCTIONS_H_)
#define WINGSPAN_FIND_LOOP_INVARIANT_INSTRUCTIONS_H_

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"

#include "plugin-registration.h"

namespace ws {
	class LoopInvariantCodeMover : public llvm::PassInfoMixin<LoopInvariantCodeMover> {
	public:

		llvm::PreservedAnalyses run(llvm::Loop& L, 
									llvm::LoopAnalysisManager& LAM, 
									llvm::LoopStandardAnalysisResults& AR, 
									llvm::LPMUpdater& U);

		static inline constexpr llvm::StringRef name() { return "wingspan-licm"; }

	private:
		friend class llvm::PassInfoMixin<LoopInvariantCodeMover>;

		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static bool registerPipelinePass(llvm::StringRef name, llvm::LoopPassManager& lpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*ignored*/);
	};
};


#endif
