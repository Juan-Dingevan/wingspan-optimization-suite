#include "wingspan-simplify-cfg.h"
#include "wingspan-constants.h"
#include "plugin-registration.h"

#include "llvm/ADT/STLExtras.h"

namespace aux {
	std::pair<llvm::BasicBlock*, llvm::BasicBlock*> getConditionalSuccessors(llvm::BasicBlock* block) {
		auto branchInst = llvm::cast<llvm::BranchInst>(block->getTerminator());
		return { branchInst->getSuccessor(0), branchInst->getSuccessor(1) };
	}
}

namespace detection {
	bool hasUnconditionalBranch(llvm::BasicBlock* bb) {
		llvm::Instruction* terminator = bb->getTerminator();
		if (llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(terminator))
			if (branchInst->isUnconditional())
				return true;

		return false;
	}

	bool hasConditionalBranch(llvm::BasicBlock* bb) {
		llvm::Instruction* terminator = bb->getTerminator();
		if (llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(terminator))
			if (!branchInst->isUnconditional())
				return true;

		return false;
	}

	bool blockCanBeSteppedOver(llvm::BasicBlock* block) {
		auto terminator = block->getTerminator();

		if (&(block->front()) != terminator)
			return false; // Has at least 2 instructions

		if (!llvm::isa<llvm::BranchInst>(terminator))
			return false;

		auto branch = llvm::dyn_cast<llvm::BranchInst>(terminator);

		return branch->isUnconditional();
	}

	bool blocksCanBeMerged(llvm::BasicBlock* first, llvm::BasicBlock* second) {
		/*
			Two blocks can be merged iff the first one has only the second one as successor,
			and the second one has only the first one as predecessor.
		*/
		
		if (auto firstSingleSuccessor = first->getSingleSuccessor()) {
			if (auto secondSinglePredecessor = second->getSinglePredecessor())
				return firstSingleSuccessor == second && secondSinglePredecessor == first;
		}

		return false;
	}
}

namespace simplification {
	void changeBranch(llvm::BasicBlock* from, llvm::BasicBlock* to, llvm::BasicBlock* old = nullptr) {
		llvm::Instruction* terminator = from->getTerminator();

		if (llvm::BranchInst* branchInst = llvm::dyn_cast<llvm::BranchInst>(terminator)) {
			if (branchInst->isUnconditional()) {
				branchInst->setSuccessor(0, to);
			}
			else if (branchInst->getSuccessor(0) == old) {
				branchInst->setSuccessor(0, to);
			}
			else if (branchInst->getSuccessor(1) == old) {
				branchInst->setSuccessor(1, to);
			}
		}
	}

	void stepOver(llvm::BasicBlock* pred, llvm::BasicBlock* steppedOver) {
		auto successor = steppedOver->getSingleSuccessor(); // We know it only has 1 successor, since its terminator is an unconditional branch.
		changeBranch(pred, successor, steppedOver);
		successor->replacePhiUsesWith(steppedOver, pred);
		steppedOver->eraseFromParent();
	}

	void stepOverBlocksWhenPossible(llvm::Function* f) {
		llvm::MapVector<llvm::BasicBlock*, bool> blocksWithConditionalPreviouslyOptimized;
		
		bool changes = true;
		int i = 0;

		while (changes) {
			changes = false;

			for (auto& block : *f) {
				if (detection::hasUnconditionalBranch(&block)) {
					auto successor = block.getSingleSuccessor();
					
					if (detection::blockCanBeSteppedOver(successor)) {
						stepOver(&block, successor);
						changes = true;
						break;
					}
				}
				else if (detection::hasConditionalBranch(&block) && blocksWithConditionalPreviouslyOptimized.count(&block) == 0) {
					auto successors = aux::getConditionalSuccessors(&block);
					
					auto thenCanBeSteppedOver = detection::blockCanBeSteppedOver(successors.first);
					auto elsedCanBeSteppedOver = detection::blockCanBeSteppedOver(successors.second);

					/*
						Note that even if BOTH successors can be stepped over, doing it would result in
						changes to the program's behavior, since it'd ruin PHI nodes. For example:

						%if:
							...
							br %cond, label %then, label %else

						%then:
							br label %end

						%else:
							br label %end

						%end:
							%x = phi([%then, %y], [%else, %z])
							...

						Thus, in these cases we arbitrarily chose to step over then then block, and
						leave the else as is.
					*/
					if (thenCanBeSteppedOver) {
						stepOver(&block, successors.first);
						blocksWithConditionalPreviouslyOptimized[&block] = true;
						changes = true;
						break;
					}
					else if (elsedCanBeSteppedOver) {
						stepOver(&block, successors.second);
						blocksWithConditionalPreviouslyOptimized[&block] = true;
						changes = true;
						break;
					}
				}
			}

			i++;
			if (i > ws::constants::MAX_ITERATIONS_FOR_STEP_OVER_BLOCKS)
				break;
		}
	}

	void merge(llvm::BasicBlock* a, llvm::BasicBlock* b) {
		b->replaceSuccessorsPhiUsesWith(a);
		auto oldTerminator = a->getTerminator();

		for (auto& inst : llvm::make_early_inc_range(*b)) {
			if (auto phi = llvm::dyn_cast<llvm::PHINode>(&inst)) {
				/*
					If two blocks are being merged, we know that it's because
					A is B's only predecessor and B is A's only successor, thus,
					this phi will always have exactly one incoming edge. Since 
					we're moving it from B to A, and A is the incoming block for 
					that edge, we can simply replace all uses with the value.
				*/
				auto value = phi->getIncomingValue(0);
				phi->replaceAllUsesWith(value);
				phi->eraseFromParent();
			}
			else {
				inst.moveBefore(oldTerminator);
			}
		}

		oldTerminator->eraseFromParent();
		b->eraseFromParent();
	}
	
	void eliminateUnnecesaryBranches(llvm::Function* f) {
		bool changes = true;
		int i = 0;

		while (changes) {
			changes = false;

			for (auto& block : *f) {
				if (detection::hasUnconditionalBranch(&block)) {
					auto successor = block.getSingleSuccessor();

					if (detection::blocksCanBeMerged(&block, successor)) {
						llvm::errs() << "Merging: " << block << " and " << *successor << "\n";

						merge(&block, successor);
						changes = true;
						break;
					}
				}
			}

			i++;
			if (i > ws::constants::MAX_ITERATIONS_FOR_STEP_OVER_BLOCKS)
				break;
		}
	}
}

llvm::PreservedAnalyses ws::WingspanCFGSimplifier::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	simplification::stepOverBlocksWhenPossible(&f);
	simplification::eliminateUnnecesaryBranches(&f);

	return llvm::PreservedAnalyses::none();
}

bool ws::WingspanCFGSimplifier::registerPipelinePass(llvm::StringRef name, llvm::FunctionPassManager& fpm, llvm::ArrayRef<llvm::PassBuilder::PipelineElement> /*unused*/) {
	if (name.consume_front("wingspan-simplify-cfg")) {
		fpm.addPass(WingspanCFGSimplifier{});
		return true;
	}

	return false;
}