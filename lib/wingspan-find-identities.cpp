#include "llvm/IR/Instruction.h"

#include "wingspan-find-identities.h"
#include "plugin-registration.h"
#include "string.h"

#define PRINT_INFO true


namespace {
    // n is a strict power of two iff n = 2^k for some natural number k > 0
    bool isStrictPowerOfTwo(int64_t value) {
        return value > 1 && (value & (value - 1)) == 0;
    }

	bool isAdditionIdentity(llvm::Instruction* instr) {
		// If it's not an addition, we discard the instr.
        if (instr->getOpcode() == llvm::Instruction::Add || instr->getOpcode() == llvm::Instruction::FAdd) {
            // Get the operands of the instruction
            llvm::Value* op1 = instr->getOperand(0);
            llvm::Value* op2 = instr->getOperand(1);

            // Case "X + 0" or "X + 0.0" (op1 is a constant zero or op2 is a constant zero)
            if ((llvm::isa<llvm::Constant>(op1) && llvm::cast<llvm::Constant>(op1)->isNullValue()) ||
                (llvm::isa<llvm::Constant>(op2) && llvm::cast<llvm::Constant>(op2)->isNullValue())) {
                return true;
            }

            // Case "X + X" (op1 and op2 are the same value and neither is a constant)
            // Since we'll use the identity X+X = 2*X = X << 1, we restrict it for ints.
            if (op1 == op2 && !llvm::isa<llvm::Constant>(op1) && instr->getOpcode() == llvm::Instruction::Add) {
                return true;
            }
        }

        // If none of the conditions are met, return false
        return false;
	}

    bool isSubtractionIdentity(llvm::Instruction* instr) {
        // If it's not an subtraction, we discard the instr.
        if (instr->getOpcode() == llvm::Instruction::Sub || instr->getOpcode() == llvm::Instruction::FSub) {
            // Get the operands of the instruction
            llvm::Value* op1 = instr->getOperand(0);
            llvm::Value* op2 = instr->getOperand(1);

            // Case "X - 0" or "X - 0.0" (op1 is a constant zero or op2 is a constant zero)
            if ((llvm::isa<llvm::Constant>(op1) && llvm::cast<llvm::Constant>(op1)->isNullValue()) ||
                (llvm::isa<llvm::Constant>(op2) && llvm::cast<llvm::Constant>(op2)->isNullValue())) {
                return true;
            }

            // Case "X - X" (op1 and op2 are the same value and neither is a constant)
            if (op1 == op2 && !llvm::isa<llvm::Constant>(op1)) {
                return true;
            }
        }

        // If none of the conditions are met, return false
        return false;
    }

    bool isMultiplicationIdentity(llvm::Instruction* instr) {
        // Check if the instruction is an integer multiplication (mul) or floating-point multiplication (fmul)
        if (instr->getOpcode() == llvm::Instruction::Mul || instr->getOpcode() == llvm::Instruction::FMul) {
            // Get the operands of the instruction
            llvm::Value* op1 = instr->getOperand(0);
            llvm::Value* op2 = instr->getOperand(1);

            // Check if either operand is a constant zero
            if ((llvm::isa<llvm::Constant>(op1) && llvm::cast<llvm::Constant>(op1)->isNullValue()) ||
                (llvm::isa<llvm::Constant>(op2) && llvm::cast<llvm::Constant>(op2)->isNullValue())) {
                return true;  // X * 0
            }

            // Check if either operand is a constant one
            if ((llvm::isa<llvm::ConstantInt>(op1) && llvm::cast<llvm::ConstantInt>(op1)->isOne()) ||
                (llvm::isa<llvm::ConstantInt>(op2) && llvm::cast<llvm::ConstantInt>(op2)->isOne())) {
                return true;  // X * 1 (for integer)
            }

            if ((llvm::isa<llvm::ConstantFP>(op1) && llvm::cast<llvm::ConstantFP>(op1)->isExactlyValue(1.0)) ||
                (llvm::isa<llvm::ConstantFP>(op2) && llvm::cast<llvm::ConstantFP>(op2)->isExactlyValue(1.0))) {
                return true;  // X * 1.0 (for floating-point)
            }
        }

        // If none of the conditions are met, return false
        return false;
    }

    bool isDivisionIdentity(llvm::Instruction* instr) {
        if (instr->getOpcode() == llvm::Instruction::FDiv) {
            llvm::Value* op1 = instr->getOperand(0);
            llvm::Value* op2 = instr->getOperand(1);

            if (llvm::isa<llvm::ConstantFP>(op2)) {
                auto constant = llvm::cast<llvm::ConstantFP>(op2);
                if (!constant->isZero())
                    return true;  // X / K (with K being constant and K != 0)
            }
        }


        if (instr->getOpcode() == llvm::Instruction::SDiv || instr->getOpcode() == llvm::Instruction::UDiv) {
            llvm::Value* op1 = instr->getOperand(0);
            llvm::Value* op2 = instr->getOperand(1);

            if (llvm::isa<llvm::ConstantInt>(op2) && llvm::cast<llvm::ConstantInt>(op2)->isOne()) {
                return true;  // X / 1
            }

            if (op1 == op2 && !llvm::isa<llvm::Constant>(op1)) {
                return true;  // X / X
            }
        }

        return false;
    }

    bool isPowerOfTwoIdentity(llvm::Instruction* instr) {
        // Check if the instruction is a multiplication or UNSIGNED division
        if (instr->getOpcode() == llvm::Instruction::Mul || instr->getOpcode() == llvm::Instruction::UDiv) {
            llvm::Value* op1 = instr->getOperand(0);
            llvm::Value* op2 = instr->getOperand(1);

            llvm::ConstantInt* constantOp = nullptr;

            // Check if one of the operands is a constant integer
            if (llvm::isa<llvm::ConstantInt>(op1)) {
                constantOp = llvm::cast<llvm::ConstantInt>(op1);
            }
            else if (llvm::isa<llvm::ConstantInt>(op2)) {
                constantOp = llvm::cast<llvm::ConstantInt>(op2);
            }

            if (constantOp) {
                int64_t value = constantOp->getSExtValue();

                // Handle cases for multiplication
                if (instr->getOpcode() == llvm::Instruction::Mul) {
                    if (isStrictPowerOfTwo(value)) {
                        return true; // x * 2^K
                    }
                    if (isStrictPowerOfTwo(value + 1)) {
                        return true; // x * (2^K - 1)
                    }
                    if (isStrictPowerOfTwo(value - 1)) {
                        return true; // x * (2^K + 1)
                    }
                }
                // Handle case for unsigned division
                else if (instr->getOpcode() == llvm::Instruction::UDiv) {
                    if (isStrictPowerOfTwo(value) && constantOp == op2) {
                        return true; // x / 2^K
                    }
                }
            }
        }

        // If none of the conditions are met, return false
        return false;
    }

    bool isBooleanIdentity(llvm::Instruction* instr) {
        if (instr->getOpcode() != llvm::Instruction::And && instr->getOpcode() != llvm::Instruction::Or)
            return false;

        llvm::Value* op1 = instr->getOperand(0);
        llvm::Value* op2 = instr->getOperand(1);

        return llvm::isa<llvm::ConstantInt>(op1) || llvm::isa<llvm::ConstantInt>(op2);
    }

}

// Addition:
ws::AdditionIdentityFinder::Result ws::AdditionIdentityFinder::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
    ws::AdditionIdentityFinder::Result additionIdentities;

    for (auto& basicBlock : f)
        for (auto& instr : basicBlock)
            if (isAdditionIdentity(&instr)) {
                additionIdentities.push_back(&instr);

                if (PRINT_INFO)
                    llvm::errs() << instr << " is an Addition Identity\n.";
            }

    return additionIdentities;
}

void ws::AdditionIdentityFinder::registerAnalysis(llvm::FunctionAnalysisManager& am) {
    am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
    am.registerPass([&] { return AdditionIdentityFinder{}; });
}

// ---------------------------------------------------------------------------
// Subtraction:
ws::SubtractionIdentityFinder::Result ws::SubtractionIdentityFinder::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
    ws::SubtractionIdentityFinder::Result subtractionIdentities;

    for (auto& basicBlock : f)
        for (auto& instr : basicBlock)
            if (isSubtractionIdentity(&instr)) {
                subtractionIdentities.push_back(&instr);

                if (PRINT_INFO)
                    llvm::errs() << instr << " is a Subtraction Identity\n.";
            }

    return subtractionIdentities;
}

void ws::SubtractionIdentityFinder::registerAnalysis(llvm::FunctionAnalysisManager& am) {
    am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
    am.registerPass([&] { return SubtractionIdentityFinder{}; });
}

// ---------------------------------------------------------------------------
// Multiplication:
ws::MultiplicationIdentityFinder::Result ws::MultiplicationIdentityFinder::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
    ws::MultiplicationIdentityFinder::Result multiplicationIdentities;

    for (auto& basicBlock : f)
        for (auto& instr : basicBlock)
            if (isMultiplicationIdentity(&instr)) {
                multiplicationIdentities.push_back(&instr);

                if (PRINT_INFO)
                    llvm::errs() << instr << " is a Multiplication Identity\n.";
            }

    return multiplicationIdentities;
}

void ws::MultiplicationIdentityFinder::registerAnalysis(llvm::FunctionAnalysisManager& am) {
    am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
    am.registerPass([&] { return MultiplicationIdentityFinder{}; });
}

// ---------------------------------------------------------------------------
// Division:
ws::DivisionIdentityFinder::Result ws::DivisionIdentityFinder::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
    ws::DivisionIdentityFinder::Result divisionIdentities;

    for (auto& basicBlock : f)
        for (auto& instr : basicBlock)
            if (isDivisionIdentity(&instr)) {
                divisionIdentities.push_back(&instr);

                if (PRINT_INFO)
                    llvm::errs() << instr << " is a Division Identity\n.";
            }

    return divisionIdentities;
}

void ws::DivisionIdentityFinder::registerAnalysis(llvm::FunctionAnalysisManager& am) {
    am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
    am.registerPass([&] { return DivisionIdentityFinder{}; });
}

// ---------------------------------------------------------------------------
// PowersOfTwo:
ws::PowersOfTwoIdentityFinder::Result ws::PowersOfTwoIdentityFinder::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
    ws::PowersOfTwoIdentityFinder::Result powersOfTwoIdentities;

    for (auto& basicBlock : f)
        for (auto& instr : basicBlock)
            if (isPowerOfTwoIdentity(&instr)) {
                powersOfTwoIdentities.push_back(&instr);

                if (PRINT_INFO)
                    llvm::errs() << instr << " is a Powers Of Two Identity\n.";
            }

    return powersOfTwoIdentities;
}

void ws::PowersOfTwoIdentityFinder::registerAnalysis(llvm::FunctionAnalysisManager& am) {
    am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
    am.registerPass([&] { return PowersOfTwoIdentityFinder{}; });
}

// ---------------------------------------------------------------------------
// Boolean:
ws::BooleanIdentityFinder::Result ws::BooleanIdentityFinder::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
    ws::BooleanIdentityFinder::Result booleanIdentities;

    for (auto& basicBlock : f)
        for (auto& instr : basicBlock)
            if (isBooleanIdentity(&instr)) {
                booleanIdentities.push_back(&instr);

                if (PRINT_INFO)
                    llvm::errs() << instr << " is a Boolean Identity\n.";
            }

    return booleanIdentities;
}

void ws::BooleanIdentityFinder::registerAnalysis(llvm::FunctionAnalysisManager& am) {
    am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
    am.registerPass([&] { return BooleanIdentityFinder{}; });
}