#ifndef Scanner_Included
#define Scanner_Included

#include <string>
#include <queue>
#include <istream>

namespace Regex {
    /* Enumerated type representing all the tokens we might expect to see. */
    enum class TokenType {
        CHARACTER = 'a',
        EPSILON   = 'e',
        EMPTYSET  = '@',
        LPAREN    = '(',
        RPAREN    = ')',
        STAR      = '*',
        PLUS      = '+',
        QUESTION  = '?',
        SIGMA     = '.',
        UNION     = '|',
        POWER     = '^',
        NUMBER    = 'n',
        SCAN_EOF  = '$'
    };

    /* Type representing a single token. */
    struct Token {
        TokenType   type;
        std::string data;
    };

    std::string to_string(const Token& t);

    /* Scans the input stream, producing a queue of tokens. If the scan fails, an
     * exception is generateed.
     *
     * The last token produced will always be a SCAN_EOF token.
     */
    std::queue<Token> scan(std::istream& input);
    std::queue<Token> scan(const std::string& sourceText);

    /* Used when serializing a regex: is the given character something we need
     * to escape?
     */
    bool isSpecialChar(char32_t ch);
}

#endif
