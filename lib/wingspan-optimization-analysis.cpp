#include "wingspan-optimization-analysis.h"
#include "plugin-registration.h"

namespace aux {
	bool isMemoryOperation(llvm::Instruction* instr) {
		switch (instr->getOpcode()) {
		case llvm::Instruction::Alloca:
		case llvm::Instruction::Load:
		case llvm::Instruction::Store:
		case llvm::Instruction::Fence:
		case llvm::Instruction::AtomicCmpXchg:
		case llvm::Instruction::AtomicRMW:
		case llvm::Instruction::GetElementPtr:
			return true;
		default:
			return false;
		}
	}

	bool isBranch(llvm::Instruction* instr) {
		return instr->getOpcode() == llvm::Instruction::Br;
	}

	bool isConditionalBranch(llvm::Instruction* instr) {
		if (!isBranch(instr))
			return false;

		auto br = llvm::dyn_cast<llvm::BranchInst>(instr);

		return br->isConditional();
	}

	bool isFunctionCall(llvm::Instruction* instr) {
		return instr->getOpcode() == llvm::Instruction::Call;
	}
}

ws::OptimizationAnalyzer::Result ws::OptimizationAnalyzer::run(llvm::Module& m, llvm::ModuleAnalysisManager& /*unused*/) {
	auto optInfo = OptimizationInfo();

	optInfo.name = m.getSourceFileName();

	for (auto& function : m) {
		optInfo.functions++;

		if (function.getNumUses() == 0)
			optInfo.deadFunctions++;

		if (function.isDeclaration())
			continue;

		if (function.hasOptNone())
			continue; // If we didn't optimize the function, we don't take stats for it.

		optInfo.optimizedFunctions++;

		for (auto& block : function) {
			optInfo.basicBlocks++;

			for (auto& instr : block) {
				optInfo.instructions++;
				
				if (aux::isMemoryOperation(&instr))
					optInfo.memoryAccesses++;

				if (aux::isBranch(&instr))
					optInfo.branches++;

				if (aux::isConditionalBranch(&instr))
					optInfo.conditionalBranches++;

				if (aux::isFunctionCall(&instr))
					optInfo.functionCalls++;
			}
		}

	}

	return optInfo;
}

void ws::OptimizationAnalyzer::registerAnalysis(llvm::ModuleAnalysisManager& am) {
	am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
	am.registerPass([&] { return OptimizationAnalyzer{}; });
}