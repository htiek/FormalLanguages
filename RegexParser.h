#ifndef RegexParser_Included
#define RegexParser_Included

#include "RegexScanner.h"
#include <queue>
#include <memory>
#include "Regex.h"
#include "Utilities/Unicode.h"


std::shared_ptr<Regex::ASTNode> parseRegex(std::queue<Regex::Token>& q);
std::shared_ptr<Regex::ASTNode> parseRegex(std::queue<Regex::Token>&& q);

#endif
