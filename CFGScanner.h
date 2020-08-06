#pragma once

#include <string>
#include <deque>
#include <istream>

namespace CFG {
    /* Enumerated type representing all the tokens we might expect to see. */
    enum class TokenType {
        TERMINAL    = 't',
        NONTERMINAL = 'A',
        ARROW       = '>',
        BAR         = '|',
        EPSILON     = 'e',
        SCAN_EOF    = '$'
    };

    /* Type representing a single token. */
    struct Token {
        TokenType type;
        char32_t  data;
    };

    std::string to_string(const Token& t);

    /* Scans the input stream, producing a queue of tokens. If the scan fails, an
     * exception is generateed.
     *
     * The last token produced will always be a SCAN_EOF token.
     */
    std::deque<Token> scan(std::istream& input);
    std::deque<Token> scan(const std::string& sourceText);
}
