#if !defined(WINGSPAN_PRINT_OPT_ANALYSIS_H_)
#define WINGSPAN_PRINT_OPT_ANALYSIS_H_

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include "plugin-registration.h"

namespace ws {
    class OptimizationAnalysisPrinter : public llvm::PassInfoMixin<OptimizationAnalysisPrinter> {
    public:
        llvm::PreservedAnalyses run(llvm::Module& m, llvm::ModuleAnalysisManager& am);

        static inline constexpr llvm::StringRef name() { return "print<wingspan-optimization-analysis>"; }

    private:
        friend class llvm::PassInfoMixin<OptimizationAnalysisPrinter>;

        //Just an aux; for easier plugin registration.
        friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

        //Each pass MUST have an AnalysisKey.
        static inline llvm::AnalysisKey Key;

        static bool registerPipelinePass(llvm::StringRef name, llvm::ModulePassManager& mpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*ignored*/);
    };
}

#endif