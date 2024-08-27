#if !defined(PLUGIN_REGISTRATION_H_)
#define PLUGIN_REGISTRATION_H_

#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

namespace ws {

	void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

}  // namespace ws


extern "C" LLVM_ATTRIBUTE_WEAK auto llvmGetPassPluginInfo() -> ::llvm::PassPluginLibraryInfo;


#endif // !defined(PLUGIN_REGISTRATION_H_)