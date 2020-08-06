#include "RegexScanner.h"
#include "Utilities/Unicode.h"
#include "StrUtils/StrUtils.h"
#include <unordered_map>
#include <sstream>
using namespace std;

namespace Regex {
    namespace {
        const unordered_map<string, TokenType> kTokens = {
            { "_", TokenType::EPSILON  },
            { "ϵ", TokenType::EPSILON  },
            { "ε", TokenType::EPSILON  },
            { "*", TokenType::STAR     },
            { "+", TokenType::PLUS     },
            { "⁺", TokenType::PLUS     },
            { "@", TokenType::EMPTYSET },
            { "∅", TokenType::EMPTYSET },
            { "Ø", TokenType::EMPTYSET },
            { "|", TokenType::UNION    },
            { "∪", TokenType::UNION    },
            { ".", TokenType::SIGMA    },
            { "Σ", TokenType::SIGMA    },
            { "∑", TokenType::SIGMA    },
            { "?", TokenType::QUESTION },
            { "(", TokenType::LPAREN   },
            { ")", TokenType::RPAREN   },
            { "^", TokenType::POWER    }
        };

        /* Map from superscript numerals to their numeric equivalents. */
        const unordered_map<string, string> kSuperscripts = {
            { "⁰", "0" },
            { "¹", "1" },
            { "²", "2" },
            { "³", "3" },
            { "⁴", "4" },
            { "⁵", "5" },
            { "⁶", "6" },
            { "⁷", "7" },
            { "⁸", "8" },
            { "⁹", "9" },
        };

        /* Replacements for <cctype>, given that we're working with
         * Unicode characters.
         */
        bool isASCII(char32_t ch) {
            return 0 <= static_cast<int>(ch) && ch <= 127;
        }
        bool isSpace(char32_t ch) {
            return isASCII(ch) && isspace(static_cast<int>(ch));
        }
        bool isDigit(char32_t ch) {
            return isASCII(ch) && isdigit(static_cast<int>(ch));
        }
        bool isSuperscriptDigit(char32_t ch) {
            return kSuperscripts.count(toUTF8(ch));
        }
        bool isEscape(char32_t ch) {
            return ch == '\\';
        }

        /* Maximum number of repeats permitted; anything above this is excessive. :-) */
        const size_t kMaxRepeats = 20;

        /* Given a numeric value represented as a string, checks whether it's
         * something we can use as an integer and, if so, returns its numeric
         * value.
         */
        int checkNumericValue(const string& sequence) {
            istringstream converter(sequence);
            size_t value;

            if (!(converter >> value) || value > kMaxRepeats) {
                throw runtime_error("Number too large: " + sequence);
            }

            return value;
        }

        void scanDigitSequence(queue<Token>& result, istream& input) {
            string sequence;
            while (input.peek() != EOF && isDigit(peekChar(input))) {
                sequence += readChar(input);
            }

            int value = checkNumericValue(sequence);
            result.push({ TokenType::NUMBER, std::to_string(value) });
        }

        void scanSuperscriptDigitSequence(queue<Token>& result, istream& input) {
            string sequence;
            while (input.peek() != EOF && isSuperscriptDigit(peekChar(input))) {
                sequence += kSuperscripts.at(toUTF8(readChar(input)));
            }

            int value = checkNumericValue(sequence);

            /* Treat this as though we implicitly raised something to a power. */
            result.push({ TokenType::POWER,  "^"});
            result.push({ TokenType::NUMBER, std::to_string(value) });
        }

        void scanCharacter(queue<Token>& result, istream& input) {
            string read = toUTF8(readChar(input));

            /* This is either a special character or just a regular
             * ordinary character.
             */
            auto itr = kTokens.find(read);
            if (itr != kTokens.end()) {
                result.push({ itr->second, read });
            } else {
                result.push({ TokenType::CHARACTER, read });
            }
        }

        void scanEscape(queue<Token>& result, istream& input) {
            /* Grab and skip the next character. */
            (void) readChar(input);

            /* The next character is the one we're looking for. */
            if (input.peek() == EOF) {
                throw runtime_error("Saw escape character at end of input.");
            }

            result.push({ TokenType::CHARACTER, toUTF8(readChar(input)) });
        }
    }

    queue<Token> scan(istream& input) {
        queue<Token> result;
        while (input.peek() != EOF) {
            /* Grab the next character to see what to do with it. */
            char32_t next = peekChar(input);

            /* Skip whitespace. */
            if (isSpace(next)) {
                (void) readChar(input);
            }
            /* If this is an escape, read it as such. */
            else if (isEscape(next)) {
                scanEscape(result, input);
            }
            /* If this is a series of superscript digits, scan the sequence as
             * a repeat count.
             */
            else if (isSuperscriptDigit(next)) {
                scanSuperscriptDigitSequence(result, input);
            }
            /* If this is a digit sequence, scan it as such. */
            else if (isDigit(next)) {
                scanDigitSequence(result, input);
            }
            /* Otherwise, scan it as though it's a collection of symbols. */
            else {
                scanCharacter(result, input);
            }
        }

        /* Tack on an EOF marker. */
        result.push({ TokenType::SCAN_EOF, "(EOF)" });
        return result;
    }

    queue<Token> scan(const string& sourceText) {
        istringstream extractor(sourceText);
        return scan(extractor);
    }

    string to_string(const Token& t) {
        return t.data;
    }

    bool isSpecialChar(char32_t ch) {
        /* It's a special character if it's in our table, it's a digit, or it's a superscript
         * digit.
         */
        return isDigit(ch) || kSuperscripts.count(toUTF8(ch)) || kTokens.count(toUTF8(ch));
    }
}
