#ifndef RegexParser_Included
#define RegexParser_Included

#include "RegexScanner.h"
#include <queue>
#include <memory>
#include "Regex.h"
#include "Utilities/Unicode.h"


namespace Regex {
    std::shared_ptr<ASTNode> parse(std::queue<Token>& q);
    std::shared_ptr<ASTNode> parse(std::queue<Token>&& q);
}

#endif
