#include "wingspan-mem2reg.h"
#include "wingspan-constant-folder.h"
#include "wingspan-find-constant-operations.h"
#include "plugin-registration.h"

void ws::RegisterPluginPasses(llvm::PassBuilder& passBuilder) {
    // opt tool registration
    // analysis
    passBuilder.registerAnalysisRegistrationCallback(ws::ConstantOperationFinder::registerAnalysis);
    // transformation
    passBuilder.registerPipelineParsingCallback(ws::WingspanMem2Reg::registerPipelinePass);
    passBuilder.registerPipelineParsingCallback(ws::WingspanConstantFolder::registerPipelinePass);

}

auto llvmGetPassPluginInfo() -> llvm::PassPluginLibraryInfo {
    return {
      LLVM_PLUGIN_API_VERSION,  // APIVersion
      "wingspan",               // PluginName
      LLVM_VERSION_STRING,      // PluginVersion
      &ws::RegisterPluginPasses
    };
}