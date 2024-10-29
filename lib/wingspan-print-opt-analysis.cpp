#include <string>
#include <sstream>

#include "wingspan-print-opt-analysis.h"
#include "wingspan-optimization-analysis.h"
#include "plugin-registration.h"

namespace aux {
    std::string toJson(const ws::OptimizationInfo& optInfo) {
        std::stringstream ss;

        ss << "{\n";
        ss << "\t\"name\": \"" << optInfo.name << "\",\n";
        ss << "\t\"functions\": " << optInfo.functions << ",\n";
        ss << "\t\"optimizedFunctions\": " << optInfo.optimizedFunctions << ",\n";
        ss << "\t\"deadFunctions\": " << optInfo.deadFunctions << ",\n";
        ss << "\t\"basicBlocks\": " << optInfo.basicBlocks << ",\n";
        ss << "\t\"instructions\": " << optInfo.instructions << ",\n";
        ss << "\t\"memoryAccesses\": " << optInfo.memoryAccesses << ",\n";
        ss << "\t\"branches\": " << optInfo.branches << ",\n";
        ss << "\t\"conditionalBranches\": " << optInfo.conditionalBranches << ",\n";
        ss << "\t\"functionCalls\": " << optInfo.functionCalls << "\n";
        ss << "}";

        return ss.str();
    }
}

llvm::PreservedAnalyses ws::OptimizationAnalysisPrinter::run(llvm::Module& m, llvm::ModuleAnalysisManager& mam) {
	auto optInfo = mam.getResult<OptimizationAnalyzer>(m);

    llvm::errs() << aux::toJson(optInfo);

	return llvm::PreservedAnalyses::all();
}

bool ws::OptimizationAnalysisPrinter::registerPipelinePass(llvm::StringRef name, llvm::ModulePassManager& mpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*unused*/) {
	if (name.consume_front("print<wingspan-optimization-analysis>")) {
		mpm.addPass(OptimizationAnalysisPrinter{});
		return true;
	}

	return false;
}