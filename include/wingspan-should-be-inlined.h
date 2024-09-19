#if !defined(WINGSPAN_SHOULD_BE_INLINED_DECIDER_H_)
#define WINGSPAN_SHOULD_BE_INLINED_DECIDER_H_

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"

#include "plugin-registration.h"

namespace ws {
	class InlineInfo {
	public:
		InlineInfo(bool should) : functionShouldBeInlined(should) {}

		bool shouldBeInlined() const {
			return functionShouldBeInlined;
		}

	private:
		bool functionShouldBeInlined;
	};

	class ShouldBeInlinedDecider : public llvm::AnalysisInfoMixin<ShouldBeInlinedDecider> {
	public:
		using Result = InlineInfo;

		Result run(llvm::Function& f, llvm::FunctionAnalysisManager& fam);

		static inline constexpr llvm::StringRef name() { return "should-be-inlined"; }

	private:
		friend class llvm::AnalysisInfoMixin<ShouldBeInlinedDecider>;
		friend void RegisterPluginPasses(llvm::PassBuilder& passBuilder);

		static inline llvm::AnalysisKey Key;

		static void registerAnalysis(llvm::FunctionAnalysisManager& am);

	};
}


#endif