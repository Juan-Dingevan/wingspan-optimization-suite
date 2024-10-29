#include "wingspan-strength-reducer.h"
#include "wingspan-find-identities.h"
#include "plugin-registration.h"
#include "wingspan-constants.h"

#include "llvm/IR/IRBuilder.h"

namespace {
	bool isStrictPowerOfTwo(int64_t value) {
		return value > 1 && (value & (value - 1)) == 0;
	}

	static unsigned int intLog2(unsigned int val) {
		if (val == 0) return UINT_MAX;
		if (val == 1) return 0;
		unsigned int ret = 0;
		while (val > 1) {
			val >>= 1;
			ret++;
		}
		return ret;
	}
}

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
			// For some reason, getNullValue gets stuck on floating point types
			// so, for now, we hotfix it like that. Note that, if the function
			// wasn't bugged, the code would work for floating point as well as
			// integers!
			if (!instr->getType()->isIntegerTy() && ws::constants::FLOATING_POINT_ARITHMETIC_IS_BUGGED)
				return;

			// Handle x - x = 0
			llvm::Value* zero = llvm::Constant::getNullValue(instr->getType());
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

	void divisionReduceStrength(llvm::Instruction* instr) {
		auto op1 = instr->getOperand(0);
		auto op2 = instr->getOperand(1);
		
		// Integer identities
		if (instr->getOpcode() == llvm::Instruction::SDiv || instr->getOpcode() == llvm::Instruction::UDiv) {
			// The only int. division identity is X / 1, which we substitute for X.
			instr->replaceAllUsesWith(op1);
			instr->eraseFromParent();
			
		}
		// FP Identity: X / K == X * 1/K
		else { //Again, something here makes the optimization get stuck. It's very weird.
			if (
				!ws::constants::AGGRESSIVE_OPTIMIZATIONS_ENABLED || 
				ws::constants::FLOATING_POINT_ARITHMETIC_IS_BUGGED
			)
				return;

			// This is an 'aggressive' optimization, since it may cause the optimized code
			// to behave (slightly) differently from the unoptimized code (due to possible 
			// precission loss in computing 1/K). We allow it if and only iff our constant
			// is set to true.
			auto op2Constant = llvm::dyn_cast<llvm::ConstantFP>(op2);
			
			auto k = op2Constant->getValue();
			auto oneOverK = llvm::APFloat(1.0);
			oneOverK.divide(k, llvm::RoundingMode::NearestTiesToEven);
			auto oneOverKConstant = llvm::ConstantFP::get(instr->getType(), oneOverK);
			
			llvm::IRBuilder builder(instr); // This will automatically insert fdiv just before instr
			auto fmul = builder.CreateFMul(op1, oneOverKConstant);
			
			instr->replaceAllUsesWith(fmul);
			instr->eraseFromParent();
		}
	}

	void powersOfTwoReduceStrength(llvm::Instruction* instr) {
		llvm::Value* op1 = instr->getOperand(0);
		llvm::Value* op2 = instr->getOperand(1);

		llvm::ConstantInt* constantOp = nullptr;
		llvm::Value* x = nullptr;

		// Check KNOW at least one of the operands is a constant int
		if (llvm::isa<llvm::ConstantInt>(op1)) {
			constantOp = llvm::cast<llvm::ConstantInt>(op1);
			x = op2;
		}
		else if (llvm::isa<llvm::ConstantInt>(op2)) {
			constantOp = llvm::cast<llvm::ConstantInt>(op2);
			x = op1;
		}

		auto constant = constantOp->getSExtValue();

		if (instr->getOpcode() == llvm::Instruction::Mul) {
			// Case 1: x * 2^K = x << k
			if (isStrictPowerOfTwo(constant)) {
				//get k, create a shift instr. replace all original uses with it.
				auto k = intLog2(constant);
				llvm::Value* kAsConstant = llvm::ConstantInt::get(op1->getType(), k);

				llvm::IRBuilder builder(instr); // This will automatically insert shl just before instr
				llvm::Value* shl = builder.CreateShl(x, kAsConstant);

				instr->replaceAllUsesWith(shl);
				instr->eraseFromParent();

			}
			// Case 2: x * (2^K + 1) = x << k + x
			else if (isStrictPowerOfTwo(constant - 1)) {
				//get k, create a shift instr., create an addition that uses the shift, replace all original uses with it.
				auto k = intLog2(constant - 1);
				llvm::Value * kAsConstant = llvm::ConstantInt::get(op1->getType(), k);
				
				llvm::IRBuilder builder(instr); // This will automatically insert shl just before instr
				llvm::Value* shl = builder.CreateShl(x, kAsConstant);
				llvm::Value* add = builder.CreateAdd(shl, x);

				instr->replaceAllUsesWith(add);
				instr->eraseFromParent();
			} 
			// Case 3 x * (2^K - 1) = x << k - x
			else {
				//get k, create a shift instr., create a subtraction that uses the shift, replace all original uses with it.
				auto k = intLog2(constant + 1);
				llvm::Value* kAsConstant = llvm::ConstantInt::get(op1->getType(), k);

				llvm::IRBuilder builder(instr); // This will automatically insert shl just before instr
				llvm::Value* shl = builder.CreateShl(x, kAsConstant);
				llvm::Value* sub = builder.CreateSub(shl, x);

				instr->replaceAllUsesWith(sub);
				instr->eraseFromParent();
			}
		}
		else { 
			//The ONLY other option is that this is an unsigned division.
			// x / 2^K = x >> k
			auto k = intLog2(constant + 1);
			llvm::Value* kAsConstant = llvm::ConstantInt::get(op1->getType(), k);

			llvm::IRBuilder builder(instr); // This will automatically insert ashr just before instr
			llvm::Value* shr = builder.CreateAShr(x, kAsConstant);

			instr->replaceAllUsesWith(shr);
			instr->eraseFromParent();
		}
	}

	void booleanReduceStrength(llvm::Instruction* instr) {
		llvm::Value* op1 = instr->getOperand(0);
		llvm::Value* op2 = instr->getOperand(1);

		// Case 0: Idempotency: x || x = x && x  = x
		if (op1 == op2) {
			instr->replaceAllUsesWith(op1);
			instr->eraseFromParent();
			return;
		}

		llvm::Constant* constantOp = nullptr;
		llvm::Value* x = nullptr;

		// Check KNOW at least one of the operands is a constant int
		if (llvm::isa<llvm::Constant>(op1)) {
			constantOp = llvm::cast<llvm::Constant>(op1);
			x = op2;
		}
		else if (llvm::isa<llvm::Constant>(op2)) {
			constantOp = llvm::cast<llvm::Constant>(op2);
			x = op1;
		}

		if (instr->getOpcode() == llvm::Instruction::Or) {
			//Case 1: x || true = true (note that 1 == true)
			if (constantOp->isOneValue()) {
				instr->replaceAllUsesWith(constantOp);
				instr->eraseFromParent();
			}
			// If it's an OR identity, the only other possible case is
			// Case 2: x || false = x
			else {
				instr->replaceAllUsesWith(x);
				instr->eraseFromParent();
			}
		}
		// If it's a boolean identity and not an OR, it MUST be an AND identity.
		else {
			//Case 1: x && true = x (note that 1 == true)
			if (constantOp->isOneValue()) {
				instr->replaceAllUsesWith(x);
				instr->eraseFromParent();
			}
			// If it's an OR identity, the only other possible case is
			// Case 2: x && false = false
			else {
				instr->replaceAllUsesWith(constantOp);
				instr->eraseFromParent();
			}
		}
	}

	void branchReduceStrength(llvm::Instruction* instr) {
		llvm::BranchInst* branch = llvm::cast<llvm::BranchInst>(instr);
		llvm::Value* cond = branch->getCondition();
		llvm::ConstantInt* condAsConstInt = llvm::cast<llvm::ConstantInt>(cond);

		if (condAsConstInt->isOne()) {
			llvm::BasicBlock* trueBlock = branch->getSuccessor(0);
			llvm::BranchInst::Create(trueBlock, branch);
		}
		else if (condAsConstInt->isZero()) {
			llvm::BasicBlock* falseBlock = branch->getSuccessor(1);
			llvm::BranchInst::Create(falseBlock, branch);
		}

		branch->eraseFromParent();
	}

	void phiReduceStrength(llvm::Instruction* instr) {
		auto phi = llvm::dyn_cast<llvm::PHINode>(instr);
		auto incomingValue = phi->getIncomingValue(0); // the ONLY incoming value this PHI has.
		instr->replaceAllUsesWith(incomingValue);
		instr->eraseFromParent();
	}
}

llvm::PreservedAnalyses ws::WingspanStrengthReducer::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	auto add = fam.getResult<ws::AdditionIdentityFinder>(f);
	
	for (auto addition : add)
		identities::additionReduceStrength(addition);
	
	auto sub = fam.getResult<ws::SubtractionIdentityFinder>(f);

	for (auto subtraction : sub)
		identities::subtractionReduceStrength(subtraction);

	auto mul = fam.getResult<ws::MultiplicationIdentityFinder>(f);

	for (auto multiplication : mul)
		identities::multiplicationReduceStrength(multiplication);

	auto div = fam.getResult<ws::DivisionIdentityFinder>(f);

	for (auto division : div)
		identities::divisionReduceStrength(division);

	auto pot = fam.getResult<ws::PowersOfTwoIdentityFinder>(f);

	for (auto power : pot)
		identities::powersOfTwoReduceStrength(power);

	//auto log = fam.getResult<ws::BooleanIdentityFinder>(f); // log as in logic, not logarithm :p

	//for (auto boolean : log)
		//identities::booleanReduceStrength(boolean);

	auto brs = fam.getResult<ws::BranchIdentityFinder>(f);

	for (auto branch : brs)
		identities::branchReduceStrength(branch);
	
	auto phi = fam.getResult<ws::PhiIdentityFinder>(f);

	for (auto phiNode : phi)
		identities::phiReduceStrength(phiNode);

	auto pa = llvm::PreservedAnalyses::all();
	
	pa.abandon<AdditionIdentityFinder>();
	pa.abandon<SubtractionIdentityFinder>();
	pa.abandon<MultiplicationIdentityFinder>();
	pa.abandon<DivisionIdentityFinder>();
	pa.abandon<PowersOfTwoIdentityFinder>();
	pa.abandon<BooleanIdentityFinder>();
	pa.abandon<BranchIdentityFinder>();
	pa.abandon<PhiIdentityFinder>();
	
	return pa;
}

bool ws::WingspanStrengthReducer::registerPipelinePass(llvm::StringRef name, llvm::FunctionPassManager& fpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*unused*/) {
	if (name.consume_front("wingspan-strength-reducer")) {
		fpm.addPass(WingspanStrengthReducer{});
		return true;
	}

	return false;
}