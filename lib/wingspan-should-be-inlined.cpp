#include "wingspan-constants.h"
#include "wingspan-should-be-inlined.h"
#include "plugin-registration.h"



ws::ShouldBeInlinedDecider::Result ws::ShouldBeInlinedDecider::run(llvm::Function& f, llvm::FunctionAnalysisManager& fam) {
	/*
		Maybe we'll end up adding more complex behavior here!
	*/
	
	int numberOfLines = 0;

	for (auto &b : f) {
		for (auto& i : b) {
			numberOfLines++;
		}
	}

	bool numberOfLinesInRange = (1 < numberOfLines) && (numberOfLines <= ws::constants::MAX_NUMBER_OF_LINES_FOR_INLINING);
	bool isImplementation = !f.isDeclaration();
	bool hasOptnoneFlag = f.hasOptNone();

	ws::ShouldBeInlinedDecider::Result ii(
		numberOfLinesInRange &&
		isImplementation &&
		!hasOptnoneFlag
	);

	return ii;
}

void ws::ShouldBeInlinedDecider::registerAnalysis(llvm::FunctionAnalysisManager& am) {
	am.registerPass([&] { return llvm::PassInstrumentationAnalysis{}; });
	am.registerPass([&] { return ShouldBeInlinedDecider{}; });
}