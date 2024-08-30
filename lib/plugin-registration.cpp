#include "wingspan-mem2reg.h"
#include "wingspan-constant-folder.h"
#include "wingspan-find-constant-operations.h"
#include "wingspan-find-identities.h"
#include "wingspan-strength-reducer.h"
#include "plugin-registration.h"

void ws::RegisterPluginPasses(llvm::PassBuilder& passBuilder) {
    // opt tool registration
    // Analysis:
    // For constant Folding:
    passBuilder.registerAnalysisRegistrationCallback(ws::ConstantOperationFinder::registerAnalysis);
    // For strength reduction
    passBuilder.registerAnalysisRegistrationCallback(ws::AdditionIdentityFinder::registerAnalysis);
    passBuilder.registerAnalysisRegistrationCallback(ws::SubtractionIdentityFinder::registerAnalysis);
    passBuilder.registerAnalysisRegistrationCallback(ws::MultiplicationIdentityFinder::registerAnalysis);
    passBuilder.registerAnalysisRegistrationCallback(ws::DivisionIdentityFinder::registerAnalysis);
    passBuilder.registerAnalysisRegistrationCallback(ws::PowersOfTwoIdentityFinder::registerAnalysis);
    passBuilder.registerAnalysisRegistrationCallback(ws::BooleanIdentityFinder::registerAnalysis);

    // Transformation
    passBuilder.registerPipelineParsingCallback(ws::WingspanMem2Reg::registerPipelinePass);
    passBuilder.registerPipelineParsingCallback(ws::WingspanConstantFolder::registerPipelinePass);
    passBuilder.registerPipelineParsingCallback(ws::WingspanStrengthReducer::registerPipelinePass);
}

auto llvmGetPassPluginInfo() -> llvm::PassPluginLibraryInfo {
    return {
      LLVM_PLUGIN_API_VERSION,  // APIVersion
      "wingspan",               // PluginName
      LLVM_VERSION_STRING,      // PluginVersion
      &ws::RegisterPluginPasses
    };
}