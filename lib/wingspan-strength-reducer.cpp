#include "wingspan-strength-reducer.h"
#include "wingspan-find-identities.h"
#include "plugin-registration.h"

#include "llvm/IR/IRBuilder.h"

namespace identities {
	void additionReduceStrength(llvm::Instruction* instr) {
		auto op1 = instr->getOperand(0);
		auto op2 = instr->getOperand(1);

		if (op1 == op2) {
			// Handle x + x = x << 1
			llvm::IRBuilder builder(instr); // This will automatically insert shl just before instr

			llvm::Value* one = llvm::ConstantInt::get(op1->getType(), 1);
			llvm::Value* shl = builder.CreateShl(op1, one);

			instr->replaceAllUsesWith(shl);
			instr->eraseFromParent();
		}
		else {
			// The ONLY other posibility is that we have x + 0 or 0 + x
			llvm::Value* x;

			auto op1Costant = llvm::dyn_cast<llvm::Constant>(op1);
			
			if (op1Costant) {
				x = op2; // instr is 0 + x
			}
			else {
				x = op1; // instr is x + 0
			}

			instr->replaceAllUsesWith(x);
			instr->eraseFromParent();
		}
	}

	void subtractionReduceStrength(llvm::Instruction* instr) {
		auto op1 = instr->getOperand(0);
		auto op2 = instr->getOperand(1);

		if (op1 == op2) {
			// Handle x - x = 0
			auto zero = llvm::Constant::getNullValue(instr->getType());
			instr->replaceAllUsesWith(zero);
			instr->eraseFromParent();
		}
		else {
			// The only other case is x - 0 = 0
			instr->replaceAllUsesWith(op1);
			instr->eraseFromParent();
		}
	}

	void multiplicationReduceStrength(llvm::Instruction* instr) {
		// The two identities we handle here are x * 0 (or 0 * x)
		// And x * 1 (or 1 * x). We ALWAYS have one constant operand.
		// So, lets find it.

		auto op1 = instr->getOperand(0);
		auto op2 = instr->getOperand(1);

		llvm::Value* x;
		llvm::Constant* k;

		if (llvm::dyn_cast<llvm::Constant>(op1)) {
			x = op2;
			k = llvm::dyn_cast<llvm::Constant>(op1);
		}
		else {
			x = op1;
			k = llvm::dyn_cast<llvm::Constant>(op2);
		}

		if (k->isNullValue()) {
			// Case x * 0
			instr->replaceAllUsesWith(k);
			instr->eraseFromParent();
		}
		else {
			// Case x * 1
			instr->replaceAllUsesWith(x);
			instr->eraseFromParent();
		}
	}
}

llvm::PreservedAnalyses ws::WingspanStrengthReducer::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	llvm::errs() << "FUNCTION " << f.getName() << ":\n";
	
	auto add = fam.getResult<ws::AdditionIdentityFinder>(f);
	
	for (auto addition : add)
		identities::additionReduceStrength(addition);
	
	auto sub = fam.getResult<ws::SubtractionIdentityFinder>(f);

	for (auto subtraction : sub)
		identities::subtractionReduceStrength(subtraction); // FALTA TESTEAR!

	auto mul = fam.getResult<ws::MultiplicationIdentityFinder>(f);

	for (auto multiplication : mul)
		identities::multiplicationReduceStrength(multiplication);

	auto div = fam.getResult<ws::DivisionIdentityFinder>(f);
	auto pot = fam.getResult<ws::PowersOfTwoIdentityFinder>(f);
	auto log = fam.getResult<ws::BooleanIdentityFinder>(f); // log as in logic, not logarithm :p
	
	llvm::errs() << "\n\n";
	
	auto pa = llvm::PreservedAnalyses::all();
	
	pa.abandon<AdditionIdentityFinder>();
	pa.abandon<SubtractionIdentityFinder>();
	pa.abandon<MultiplicationIdentityFinder>();
	pa.abandon<DivisionIdentityFinder>();
	pa.abandon<PowersOfTwoIdentityFinder>();
	pa.abandon<BooleanIdentityFinder>();
	
	return pa;
}

bool ws::WingspanStrengthReducer::registerPipelinePass(llvm::StringRef name, llvm::FunctionPassManager& fpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*unused*/) {
	if (name.consume_front("wingspan-strength-reducer")) {
		fpm.addPass(WingspanStrengthReducer{});
		return true;
	}

	return false;
}