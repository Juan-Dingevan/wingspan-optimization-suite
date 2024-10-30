#include "wingspan-constants.h"
#include "wingspan-should-be-inlined.h"
#include "plugin-registration.h"

namespace aux {
	bool shouldBeInlined(llvm::Function& f) {
		if (f.hasOptNone())
			return false;

		if (f.isDeclaration())
			return false;

		if (f.hasNUses(1))
			return true;

		int numberOfLines = f.getInstructionCount();

		return (1 < numberOfLines) && (numberOfLines <= ws::constants::MAX_NUMBER_OF_LINES_FOR_INLINING);
	}
}

ws::ShouldBeInlinedDecider::Result ws::ShouldBeInlinedDecider::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	ws::ShouldBeInlinedDecider::Result should(aux::shouldBeInlined(f));
	return should;
}

void ws::ShouldBeInlinedDecider::registerAnalysis(llvm::FunctionAnalysisManager& am) {
	am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
	am.registerPass([&] { return ShouldBeInlinedDecider{}; });
}