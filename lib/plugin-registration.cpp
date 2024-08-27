#include "wingspan-mem2reg.h"
#include "plugin-registration.h"

void ws::RegisterPluginPasses(llvm::PassBuilder& passBuilder)
{
    // opt tool registration
    passBuilder.registerPipelineParsingCallback(ws::WingspanMem2Reg::registerPipelinePass);
}

auto llvmGetPassPluginInfo() -> llvm::PassPluginLibraryInfo {
    return {
      LLVM_PLUGIN_API_VERSION,  // APIVersion
      "wingspan",               // PluginName
      LLVM_VERSION_STRING,      // PluginVersion
      &ws::RegisterPluginPasses
    };
}