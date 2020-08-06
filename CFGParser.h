#pragma once

#include "CFGScanner.h"
#include "CFG.h"
#include "Utilities/JSON.h"

/* Parses a CFG that was read in via the standard scanner interface. */
CFG::CFG parseCFG(std::deque<CFG::Token> tokens, const Languages::Alphabet& alphabet);

/* Parses a CFG stored in the old style. */
CFG::CFG parseCFG(JSON json, const Languages::Alphabet& alphabet);
