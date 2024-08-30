#include "llvm/IR/Instruction.h"

#include "wingspan-find-constant-operations.h"
#include "plugin-registration.h"

namespace {
	bool operationHasOnlyConstantOperands(llvm::Instruction& instr) {
		// For this pass, we work some ops. only
		if (!instr.isBinaryOp() && !instr.isUnaryOp() && !instr.isCast()) {
			return false;
		}

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
			if (operationHasOnlyConstantOperands(instr))
				operationsWithOnlyConstantOperands.push_back(&instr);

	return operationsWithOnlyConstantOperands;
}

void ws::ConstantOperationFinder::registerAnalysis(llvm::FunctionAnalysisManager& am) {
	am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
	am.registerPass([&] { return ConstantOperationFinder{}; });
}