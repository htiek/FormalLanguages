#pragma once

#include "CFGScanner.h"
#include "CFG.h"
#include "Utilities/JSON.h"

namespace CFG {
    /* Parses a CFG that was read in via the standard scanner interface. */
    CFG parse(std::deque<Token> tokens, const Languages::Alphabet& alphabet);

    /* Parses a CFG stored in the old style. */
    CFG parse(JSON json, const Languages::Alphabet& alphabet);
}
