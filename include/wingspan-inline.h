#if !defined(WINGSPAN_INLINE_H_)
#define WINGSPAN_INLINE_H_

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/ADT/ArrayRef.h"

namespace ws {
	class WingspanInliner : public llvm::PassInfoMixin<WingspanInliner> {
	public:
		//The function we MUST override in order to implement the pass.
		llvm::PreservedAnalyses run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		//Just an aux; for easier plugin registration.
		static inline constexpr llvm::StringRef name() { return "wingspan-inline"; }
	private:
		//Constructor
		friend class llvm::PassInfoMixin<WingspanInliner>;

		//Just an aux; for easier plugin registration.
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		//Each pass MUST have an AnalysisKey.
		static inline llvm::AnalysisKey Key;

		//Each pass MUST have a way to be registered.
		static bool registerPipelinePass(llvm::StringRef name, llvm::FunctionPassManager& fpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*ignored*/);


	};
}

#endif