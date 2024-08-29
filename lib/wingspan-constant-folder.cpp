#include "wingspan-constant-folder.h"
#include "wingspan-find-constant-operations.h"
#include "plugin-registration.h"

#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/ADT/SmallVector.h"

const unsigned int UPPER_BOUND_FOR_FOLDS = 32;

namespace folds {
	bool isValidIntOperand(llvm::Constant* possibleInt) {
		auto opInt = llvm::dyn_cast<llvm::ConstantInt>(possibleInt);
		
		if (!opInt)
			return false;

		// We don't fold operations with operands larger than 64 bits
		// Because llvm doesn't allow us to (easily) create constants
		// larger than 64 bits.
		if (opInt->getBitWidth() > 64)
			return false;

		return true;

		/*
			Note: I'm 99% sure this function gets inlined, so after
			redundancy elimination, we can use the dyn_cast we do here
			to replace the one in the caller's body :)
		*/
	}

	void foldFNeg(llvm::Instruction* instr, llvm::Constant* opConstant) {
		auto opNeg = llvm::ConstantExpr::getNeg(opConstant);

		instr->replaceAllUsesWith(opNeg);
		instr->eraseFromParent();
	}

	void foldSExt(llvm::Instruction* instr, llvm::Constant* opConstant) {
		// We know, since the opcode is SExt that 
		// we'll be dealing with ints. However, we check that
		// they are int literals and not vectors or any weird
		// int type.
		if (!isValidIntOperand(opConstant))
			return;

		auto opInt = llvm::dyn_cast<llvm::ConstantInt>(opConstant);

		// Source type doesn't matter, we get it as 64 bits
		auto opValue = opInt->getSExtValue();

		// We get the destination type
		auto sext = llvm::dyn_cast<llvm::SExtInst>(instr);
		auto destinationType = sext->getDestTy();

		auto sextedOp = llvm::ConstantInt::get(destinationType, opValue);

		instr->replaceAllUsesWith(sextedOp);
		instr->eraseFromParent();
	}

	void foldZExt(llvm::Instruction* instr, llvm::Constant* opConstant) {
		if (!isValidIntOperand(opConstant))
			return;

		auto opInt = llvm::dyn_cast<llvm::ConstantInt>(opConstant);

		auto opValue = opInt->getZExtValue();

		auto zext = llvm::dyn_cast<llvm::ZExtInst>(instr);
		auto destinationType = zext->getDestTy();

		auto sextedOp = llvm::ConstantInt::get(destinationType, opValue);

		instr->replaceAllUsesWith(sextedOp);
		instr->eraseFromParent();
	}

	void foldTrunc(llvm::Instruction* instr, llvm::Constant* opConstant) {
		if (!isValidIntOperand(opConstant))
			return;

		// If trunc has any size wrapping, we can't fold it, because
		// we may be preventing a runtime error from ocurring.
		// Let the programmer deal with those!
		auto trunc = llvm::dyn_cast<llvm::TruncInst>(instr);
		if (!trunc || trunc->hasNoUnsignedWrap() || trunc->hasNoSignedWrap())
			return;

		auto opInt = llvm::dyn_cast<llvm::ConstantInt>(opConstant);

		auto opValue = opInt->getSExtValue();

		auto destinationType = trunc->getDestTy();

		auto truncatedOp = llvm::ConstantInt::get(destinationType, opValue);

		instr->replaceAllUsesWith(truncatedOp);
		instr->eraseFromParent();
	}

	void foldUDiv(llvm::Instruction* instr, llvm::Constant* a, llvm::Constant* b) {
		// We know that if the opcode is UDiv, 
		// we're dealing with (unsigned) integer operands
		// Let's make sure they are valid and not vectors
		if (!isValidIntOperand(a) || !isValidIntOperand(b))
			return;

		auto aInt = llvm::dyn_cast<llvm::ConstantInt>(a);
		auto bInt = llvm::dyn_cast<llvm::ConstantInt>(b);

		// If b is 0, we change nothing. We don't want
		// the arithmetic exception to pop up here, in
		// compile time, we want the programmer to deal
		// with it.
		if (b->isZeroValue())
			return;

		// We get the (unsigned) ints.
		auto aValue = aInt->getZExtValue();
		auto bValue = bInt->getZExtValue();

		// We get the quotient
		auto cValue = aValue / bValue;

		// We create the llvm constant that represents the quotient
		auto c = llvm::ConstantInt::get(aInt->getType(), cValue);

		instr->replaceAllUsesWith(c);
		instr->eraseFromParent();
	}

	void foldSDiv(llvm::Instruction* instr, llvm::Constant* a, llvm::Constant* b) {
		if (!isValidIntOperand(a) || !isValidIntOperand(b))
			return;
		
		auto aInt = llvm::dyn_cast<llvm::ConstantInt>(a);
		auto bInt = llvm::dyn_cast<llvm::ConstantInt>(b);

		if (b->isZeroValue())
			return;

		auto aValue = aInt->getSExtValue();
		auto bValue = bInt->getSExtValue();

		auto cValue = aValue / bValue;

		auto c = llvm::ConstantInt::getSigned(aInt->getType(), cValue);

		instr->replaceAllUsesWith(c);
		instr->eraseFromParent();
	}

	void foldURem(llvm::Instruction* instr, llvm::Constant* a, llvm::Constant* b) {
		if (!isValidIntOperand(a) || !isValidIntOperand(b))
			return;
		
		auto aInt = llvm::dyn_cast<llvm::ConstantInt>(a);
		auto bInt = llvm::dyn_cast<llvm::ConstantInt>(b);

		if (b->isZeroValue())
			return;

		auto aValue = aInt->getZExtValue();
		auto bValue = bInt->getZExtValue();

		auto cValue = aValue % bValue;

		auto c = llvm::ConstantInt::getSigned(aInt->getType(), cValue);

		instr->replaceAllUsesWith(c);
		instr->eraseFromParent();
	}

	void foldSRem(llvm::Instruction* instr, llvm::Constant* a, llvm::Constant* b) {
		if (!isValidIntOperand(a) || !isValidIntOperand(b))
			return;

		auto aInt = llvm::dyn_cast<llvm::ConstantInt>(a);
		auto bInt = llvm::dyn_cast<llvm::ConstantInt>(b);

		if (b->isZeroValue())
			return;

		auto aValue = aInt->getSExtValue();
		auto bValue = bInt->getSExtValue();

		auto cValue = aValue % bValue;

		auto c = llvm::ConstantInt::getSigned(aInt->getType(), cValue);

		instr->replaceAllUsesWith(c);
		instr->eraseFromParent();
	}

	void foldShl(llvm::Instruction* instr, llvm::Constant* a, llvm::Constant* b) {
		if (!isValidIntOperand(a) || !isValidIntOperand(b))
			return;

		// If shl has any size wrapping, we can't fold it, because
		// we may be preventing a runtime error from ocurring.
		// Let the programmer deal with those!
		auto shl = llvm::dyn_cast<llvm::ShlOperator>(instr);
		if (!shl || shl->hasNoUnsignedWrap() || shl->hasNoSignedWrap())
			return;

		auto aInt = llvm::dyn_cast<llvm::ConstantInt>(a);
		auto bInt = llvm::dyn_cast<llvm::ConstantInt>(b);

		auto n = a->getType()->getIntegerBitWidth();
		auto aValue = aInt->getSExtValue();
		auto bValue = bInt->getSExtValue();

		// The langref says that shifting for k > N bits for type iN
		// will produce undefined results. To me, that's close enough
		// to a runtime error that we should skip folding.

		// In addition, note that b will always be treated as unsigned, 
		// thus if the 'real' b < 0, the 'considered' b will be a huge
		// positive number, and the shift will be valid.

		if (bValue > n)
			return;

		auto cValue = aValue << bValue;

		auto c = llvm::ConstantInt::getSigned(aInt->getType(), cValue);

		instr->replaceAllUsesWith(c);
		instr->eraseFromParent();
	}

	void foldAshr(llvm::Instruction* instr, llvm::Constant* a, llvm::Constant* b) {
		if (!isValidIntOperand(a) || !isValidIntOperand(b))
			return;

		auto aInt = llvm::dyn_cast<llvm::ConstantInt>(a);
		auto bInt = llvm::dyn_cast<llvm::ConstantInt>(b);

		auto n = a->getType()->getIntegerBitWidth();
		auto aValue = aInt->getSExtValue();
		auto bValue = bInt->getSExtValue();

		if (bValue > n)
			return;

		auto cValue = aValue >> bValue;

		auto c = llvm::ConstantInt::getSigned(aInt->getType(), cValue);

		instr->replaceAllUsesWith(c);
		instr->eraseFromParent();
	}
}

namespace {
	void foldUnaryOperation(llvm::Instruction* instr) {
		auto op = instr->getOperand(0);
		auto opConstant = llvm::dyn_cast<llvm::Constant>(op);

		// Accordin to langref (https://llvm.org/docs/LangRef.html#instruction-reference)
		// There is only one type of unary arithmetic instruction, and that is fneg.
		if (instr->getOpcode() == llvm::Instruction::FNeg) {
			folds::foldFNeg(instr, opConstant);
		}
		// But we're not just interested in folding arithmetic. Some cast
		// instructions, such as sign- or zero-extensions can be folded.
		else if (instr->getOpcode() == llvm::Instruction::SExt) {
			folds::foldSExt(instr, opConstant);
		}
		else if (instr->getOpcode() == llvm::Instruction::ZExt) {
			folds::foldZExt(instr, opConstant);
		}
		else if (instr->getOpcode() == llvm::Instruction::Trunc) {
			folds::foldTrunc(instr, opConstant);
		}

	}

	void foldBinaryOperation(llvm::Instruction* instr) {
		// Every instruction will have the form: 
		//	c = a OP b
		// Where a and b are constants.

		auto opcode = instr->getOpcode();

		auto op0 = instr->getOperand(0);
		auto op1 = instr->getOperand(1);

		auto a = llvm::dyn_cast<llvm::Constant>(op0);
		auto b = llvm::dyn_cast<llvm::Constant>(op1);

		// With this, we cover the general case.
		// It's mostly int operations and xor.
		// We have the HUGE advantage of not having to deal
		// With weird types (i8, i1, vectors, etc) manually
		if (llvm::ConstantExpr::isDesirableBinOp(opcode) && llvm::ConstantExpr::isSupportedBinOp(opcode)) {
			auto c = llvm::ConstantExpr::get(opcode, a, b);

			instr->replaceAllUsesWith(c);
			instr->eraseFromParent();

			return;
		}
		
		// And here, we cover specific cases we deemed to be useful.
		switch (instr->getOpcode()) {
		case llvm::Instruction::UDiv:
			folds::foldUDiv(instr, a, b);
			break;
		case llvm::Instruction::SDiv:
			folds::foldSDiv(instr, a, b);
			break;
		case llvm::Instruction::URem:
			folds::foldURem(instr, a, b);
			break;
		case llvm::Instruction::SRem:
			folds::foldSRem(instr, a, b);
			break;
		case llvm::Instruction::Shl:
			folds::foldShl(instr, a, b);
			break;
		case llvm::Instruction::AShr:
			folds::foldAshr(instr, a, b);
			break;
		}

	}

	void fold(llvm::Instruction* instr) {
		if (instr->isBinaryOp())
			foldBinaryOperation(instr);
		else
			foldUnaryOperation(instr);
	}
}

llvm::PreservedAnalyses ws::WingspanConstantFolder::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {	
	// At worst, we do 32 levels of constant folding. This is, of course, a ridiculously
	// high upper bound, which we expect we'll never reach.
	int const max_folds = std::min(f.getInstructionCount(), UPPER_BOUND_FOR_FOLDS);
	int i = 0;

	while (i < max_folds) {
		//llvm::errs() << "Iteration " << i << "\n";

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