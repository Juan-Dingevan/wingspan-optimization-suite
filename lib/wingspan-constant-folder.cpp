#include "wingspan-constant-folder.h"
#include "wingspan-find-constant-operations.h"
#include "plugin-registration.h"

#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/ADT/SmallVector.h"

const unsigned int UPPER_BOUND_FOR_FOLDS = 32;

namespace {
	void foldUnaryOperation(llvm::Instruction* instr) {
		// While casting is, technically, an arithmetic operation for llvm
		// we can't really fold them, so we let them be and exit early.
		if (instr->isCast())
			return;

		// Accordin to langref (https://llvm.org/docs/LangRef.html#instruction-reference)
		// There is only one other type of unary instruction, and that is fneg.
		// We check for its opcode for completeness' sake.

		if (instr->getOpcode() == llvm::Instruction::FNeg) {
			auto op = instr->getOperand(0);
			auto opConstant = llvm::dyn_cast<llvm::Constant>(op);
			
			auto opNeg = llvm::ConstantExpr::getNeg(opConstant);
			
			instr->replaceAllUsesWith(opNeg);
			instr->eraseFromParent();
		}

	}

	void foldBinaryOperation(llvm::Instruction* instr) {
		// Every instruction will have the form: 
		//	c = a OP b
		// Where a and b are constants.

		auto opcode = instr->getOpcode();

		// General case.
		if (llvm::ConstantExpr::isDesirableBinOp(opcode) && llvm::ConstantExpr::isSupportedBinOp(opcode)) {
			llvm::errs() << "Folding a bin op\n";

			auto op0 = instr->getOperand(0);
			auto op1 = instr->getOperand(1);

			auto a = llvm::dyn_cast<llvm::Constant>(op0);
			auto b = llvm::dyn_cast<llvm::Constant>(op1);

			auto c = llvm::ConstantExpr::get(opcode, a, b);

			instr->replaceAllUsesWith(c);
			instr->eraseFromParent();
		}
	}

	void fold(llvm::Instruction* instr) {
		if (instr->isUnaryOp())
			foldUnaryOperation(instr);
		else
			foldBinaryOperation(instr);
	}
}

llvm::PreservedAnalyses ws::WingspanConstantFolder::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {	
	// At worst, we do 32 levels of constant folding. This is, of course, a ridiculously
	// high upper bound, which we expect we'll never reach.
	int const max_folds = std::min(f.getInstructionCount(), UPPER_BOUND_FOR_FOLDS);
	int i = 0;

	while (i < max_folds) {
		
		ws::ConstantOperationFinder::Result foldableInstructions = fam.getResult<ws::ConstantOperationFinder>(f);
		
		// If there are no more foldable instructions in f, we break early.
		if (foldableInstructions.size() == 0)
			break;

		for (auto instr : foldableInstructions) {
			fold(instr);
		}
		
		// Invalidate the results of the previous execution of ConstantOperationFinder
		// Otherwise, we'd be operating under false pretenses, such as a certain foldable
		// instruction existing, when it was removed by the previous iteration
		fam.invalidate(f, llvm::PreservedAnalyses::none());

		i++;
	}

	
	
	return llvm::PreservedAnalyses::none();
}

bool ws::WingspanConstantFolder::registerPipelinePass(llvm::StringRef name, llvm::FunctionPassManager& fpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*unused*/) {
	if (name.consume_front("wingspan-constant-folder")) {
		fpm.addPass(WingspanConstantFolder{});
		return true;
	}

	return false;
}