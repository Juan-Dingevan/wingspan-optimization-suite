#include "llvm/IR/Instruction.h"

#include "wingspan-find-constant-operations.h"
#include "plugin-registration.h"

namespace {
	bool canBeFolded(llvm::Instruction* instr) {
		switch (instr->getOpcode()) {
		case llvm::Instruction::UDiv:
		case llvm::Instruction::SDiv:
		case llvm::Instruction::URem:
		case llvm::Instruction::SRem:
		case llvm::Instruction::Shl:
		case llvm::Instruction::AShr:
		case llvm::Instruction::FNeg:
		case llvm::Instruction::SExt:
		case llvm::Instruction::ZExt:
		case llvm::Instruction::Trunc:
		case llvm::Instruction::ICmp:
			return true;
		default:
			return false;
		}
	}

	bool operationHasOnlyConstantOperands(llvm::Instruction& instr) {
		for (auto& op : instr.operands()) {
			if (!(llvm::isa<llvm::Constant>(op))) {
				return false;
			}
		}

		return true;
	}
}

ws::ConstantOperationFinder::Result ws::ConstantOperationFinder::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	ws::ConstantOperationFinder::Result operationsWithOnlyConstantOperands;

	for (auto& basicBlock : f)
		for (auto& instr : basicBlock)
			if (canBeFolded(&instr) && operationHasOnlyConstantOperands(instr))
				operationsWithOnlyConstantOperands.push_back(&instr);

	return operationsWithOnlyConstantOperands;
}

void ws::ConstantOperationFinder::registerAnalysis(llvm::FunctionAnalysisManager& am) {
	am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
	am.registerPass([&] { return ConstantOperationFinder{}; });
}