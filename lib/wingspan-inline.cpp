#include "wingspan-inline.h"
#include "wingspan-should-be-inlined.h"
#include "plugin-registration.h"
#include "wingspan-constants.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"

#define INFO true

namespace aux {
	void removeLastInstruction(llvm::BasicBlock* block) {
		if (!block->empty()) {
			llvm::Instruction* lastInstr = block->getTerminator();
			lastInstr->eraseFromParent();
		}
	}

	std::pair<llvm::BasicBlock*, llvm::BasicBlock*> splitBeforeAndAfterInstr(llvm::Instruction* instr) {
		llvm::BasicBlock* originalBlock = instr->getParent();

		llvm::BasicBlock* secondHalf = llvm::BasicBlock::Create(
			originalBlock->getContext(),
			"", 
			originalBlock->getParent(), 
			originalBlock->getNextNode()
		);

		// Create a temporary terminator so we can insert instructions into secondHalf
		auto tempTerminator = llvm::BranchInst::Create(originalBlock->getNextNode(), secondHalf);

		llvm::BasicBlock::iterator it(instr);
		++it;

		while (it != originalBlock->end()) {
			llvm::Instruction& currentInstr = *it++;
			currentInstr.moveBefore(secondHalf->getTerminator());
		}

		//Remove the temporary terminator
		removeLastInstruction(secondHalf);

		llvm::BranchInst::Create(secondHalf, originalBlock);

		return std::make_pair(originalBlock, secondHalf);
	}

	void changeBranch(llvm::BasicBlock* blockBranchIsIn, llvm::BasicBlock* to) {
		llvm::Instruction* terminator = blockBranchIsIn->getTerminator();

		if (llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(terminator))
			if (branchInst->isUnconditional())
				branchInst->setSuccessor(0, to);
	}
}

namespace inlining {
	llvm::SmallVector<llvm::CallInst*> getAllCallInsts(llvm::Function* f) {
		llvm::SmallVector<llvm::CallInst*> calls;

		for (auto& b : *f) {
			for (auto& instr : b) {
				if (auto callInst = llvm::dyn_cast<llvm::CallInst>(&instr)) {
					calls.push_back(callInst);
				}
			}
		}

		return calls;
	}

	llvm::SmallVector<llvm::CallInst*> getCallInstsToInline(llvm::SmallVector<llvm::CallInst*> candidates, llvm::FunctionAnalysisManager& fam) {
		llvm::SmallVector<llvm::CallInst*> toBeInlined;

		for (auto candidate : candidates) {
			auto g = candidate->getCalledFunction();
			
			/*
				Some function calls (indirect or otherwise invalid)
				will return null. Obviously those candidates are NOT
				to be considered.
			*/
			if (g) {
				ws::ShouldBeInlinedDecider::Result result = fam.getResult<ws::ShouldBeInlinedDecider>(*g);
				if (result.shouldBeInlined()) {
					toBeInlined.push_back(candidate);
				}
			}
		}

		return toBeInlined;
	}
	
	void makeInstructionUseActualParameters(llvm::Instruction* instr, llvm::ValueToValueMapTy& formalToActualMap) {
		for (int i = 0; i < instr->getNumOperands(); i++) {
			auto op = instr->getOperand(i);
			if (formalToActualMap[op]) {
				instr->setOperand(i, formalToActualMap[op]);
			}
		}

		if (auto phi = llvm::dyn_cast<llvm::PHINode>(instr)) {
			for (int i = 0; i < phi->getNumIncomingValues(); i++) {
				auto formalBlock = phi->getIncomingBlock(i);
				auto actualBlockNullable = formalToActualMap[formalBlock];
				
				if (actualBlockNullable) {
					auto actualBlock = llvm::dyn_cast<llvm::BasicBlock>(actualBlockNullable);
					phi->setIncomingBlock(i, actualBlock);
				}
			}
		}
	}

	void inlineCall(llvm::CallInst* call) {
		auto f = call->getParent()->getParent();
		auto g = call->getCalledFunction();

		auto halves = aux::splitBeforeAndAfterInstr(call);
		auto firstHalf = halves.first;
		auto secondHalf = halves.second;

		llvm::ValueToValueMapTy formalToActualMap;

		// Map the formal arguments of g() to the actual arguments used in the call
		for (unsigned i = 0; i < call->arg_size(); ++i) {
			formalToActualMap[g->getArg(i)] = call->getArgOperand(i);
		}

		llvm::SmallVector<llvm::BasicBlock*> clonedBlocks;

		for (auto &blockInG : *g) {
			auto blockInF = llvm::CloneBasicBlock(&blockInG, formalToActualMap, "", f);
			
			blockInF->moveBefore(secondHalf);

			formalToActualMap[&blockInG] = blockInF;
			
			clonedBlocks.push_back(blockInF);
		}

		// If g() isn't null, add a PHI node to secondHalf which 
		// we'll use to determine the value that g() would've returned.
		llvm::PHINode* phi = nullptr;
		bool needsPhi = !call->getType()->isVoidTy();
		if (needsPhi) {
			phi = llvm::PHINode::Create(call->getType(), 0, "", secondHalf->getFirstNonPHI());
		}

		llvm::SmallVector<llvm::Instruction*> toDelete;

		// Adjust cloned instructions.
		for (auto block : clonedBlocks) {
			for (auto& instr : *block) {
				llvm::errs() << "\t" << instr << "\n";
				// Replace all uses of formal parameters for uses of actual parameters.
				makeInstructionUseActualParameters(&instr, formalToActualMap);

				// If its a return, we replace it by a branch to secondHalf, and
				// add the value that would've been returned to the PHI node.
				if (auto ret = llvm::dyn_cast<llvm::ReturnInst>(&instr)) {
					toDelete.push_back(ret); // We cant delete while iterating, over the blocks, so we do it after.
					auto br = llvm::BranchInst::Create(secondHalf, ret); // Add a branch from block to secondHalf.
					
					if(needsPhi) {
						auto returnValue = ret->getReturnValue();
						phi->addIncoming(returnValue, block);
					}
				}
			}
		}

		for (auto instr : toDelete) {
			instr->eraseFromParent();
		}

		// Adjust the CFG so that firstHalf jumps over to the first block
		// we pulled from g()
		aux::changeBranch(firstHalf, clonedBlocks.front());

		call->replaceAllUsesWith(phi);
		call->eraseFromParent();
	}
}

llvm::PreservedAnalyses ws::WingspanInliner::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	auto allCalls = inlining::getAllCallInsts(&f);
	auto toBeInlined = inlining::getCallInstsToInline(allCalls, fam);

	if (INFO) {
		llvm::errs() << "Running on Function " << f.getName() << ".\n";
		llvm::errs() << "Found the following call instructions: \n";
		
		for (auto call : allCalls)
			llvm::errs() << "\t" << *call << "\n";

		llvm::errs() << "\n";
		
		llvm::errs() << "From which the following were selected to be inlined: \n";
		
		for (auto call : toBeInlined)
			llvm::errs() << "\t" << *call << "\n";

		llvm::errs() << "\n";
		llvm::errs() << "Proceeding to inline.\n";
	}

	for (auto instr : toBeInlined) {
		inlining::inlineCall(instr);
	}

	return llvm::PreservedAnalyses::all();
}

bool ws::WingspanInliner::registerPipelinePass(llvm::StringRef name, llvm::FunctionPassManager& fpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*unused*/) {
	if (name.consume_front("wingspan-inline")) {
		fpm.addPass(WingspanInliner{});
		return true;
	}

	return false;
}