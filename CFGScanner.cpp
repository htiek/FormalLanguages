#include "CFGScanner.h"
#include "Utilities/Unicode.h"
#include "StrUtils/StrUtils.h"
#include <unordered_map>
#include <sstream>
using namespace std;

namespace CFG {
    namespace {
        const unordered_map<string, TokenType> kTokens = {
            { "->",               TokenType::ARROW   },
            { "=>",               TokenType::ARROW   },
            { "\\to",             TokenType::ARROW   },
            { "\\rightarrow",     TokenType::ARROW   },
            { "\\Rightarrow",     TokenType::ARROW   },
            { "→",                TokenType::ARROW   },
            { "⇒",                TokenType::ARROW   },
            { "::=",              TokenType::ARROW   },
            { "|",                TokenType::BAR     },
            { "ϵ",                TokenType::EPSILON },
            { "ε",                TokenType::EPSILON },
            { "_",                TokenType::EPSILON },
        };

        /* Replacements for <cctype>, given that we're working with
         * Unicode characters.
         */
        bool isUpper(char32_t ch) {
            return (ch >= 'A' && ch <= 'Z');
        }
        bool isASCII(char32_t ch) {
            return 0 <= static_cast<int>(ch) && ch <= 127;
        }
        bool isSpace(char32_t ch) {
            return isASCII(ch) && isspace(static_cast<int>(ch));
        }

        /* Whether something is a terminal. */
        bool isNonterminal(char32_t ch) {
            return isUpper(ch);
        }

        /* Returns whether any token begins with the specified pattern. */
        bool someTokenStartsWith(const string& soFar) {
            for (const auto& token: kTokens) {
                if (Utilities::startsWith(token.first, soFar)) return true;
            }
            return false;
        }

        /* Scans until either (1) a complete symbol is found or (2) the input is
         * consumed. In the first case, great! We return what token we got. In the
         * second case, we report an error, because we don't know what to do.
         */
        void scanSymbol(deque<Token>& result, istream& input) {
            /* Grab the first character, which we know exists. */
            string token = toUTF8(readChar(input));

            /* Keep extending this while one of our special characters starts with it. Stop
             * once we hit the end or when the next character is a space.
             */
            bool match = kTokens.count(token);
            while (someTokenStartsWith(token) && input.peek() != EOF && !isSpace(peekChar(input))) {
                token += toUTF8(readChar(input));
                match |= kTokens.count(token);
            }

            /* One of two things is true at this point:
             *
             * 1. We have read something that matched at some point. In that case, use
             *    maximal munch to figure out what that something is, then return it.
             * 2. Nothing matched. That's okay! Because in CFG land we don't have multiletter
             *    expressions, we just want the first character that we read. Put the rest
             *    back into the stream and figure out what those tokens mean later.
             */
            if (match) {
                while (!kTokens.count(token)) {
                    if (!input.unget()) throw runtime_error("Can't rewind stream.");
                    token.pop_back();
                }
                result.push_back({ kTokens.at(token), static_cast<char32_t>(kTokens.at(token)) });
            } else {
                /* Rewind all the way. */
                while (!token.empty()) {
                    if (!input.unget()) throw runtime_error("Can't rewind stream.");
                    token.pop_back();
                }

                /* Just read one character. */
                char32_t ch = readChar(input);
                result.push_back({ isNonterminal(ch)? TokenType::NONTERMINAL : TokenType::TERMINAL, ch });
            }
        }
    }

    deque<Token> scan(istream& input) {
        deque<Token> result;
        while (input.peek() != EOF) {
            /* Grab the next character to see what to do with it. */
            char32_t next = peekChar(input);

            /* Skip whitespace. */
            if (isSpace(next)) {
                (void) readChar(input);
            } else {
                scanSymbol(result, input);
            }
        }

        /* Tack on two EOF markers, since our scanner needs two tokens of lookahead. */
        result.push_back({ TokenType::SCAN_EOF, '$' });
        result.push_back({ TokenType::SCAN_EOF, '$' });
        return result;
    }

    deque<Token> scan(const string& sourceText) {
        istringstream extractor(sourceText);
        return scan(extractor);
    }

    string to_string(const Token& t) {
        return toUTF8(t.data);
    }
}

