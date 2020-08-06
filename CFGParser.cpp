#include "CFGParser.h"
#include "Utilities/Unicode.h"
using namespace std;

namespace {
    /* Utility error reporting function. */
    [[ noreturn ]] void parseError(const string& message) {
        throw runtime_error(message);
    }

    /* This hand-rolled, recursive descent parser with two tokens of lookahead is
     * based on this CFG:
     *
     * Grammar -> VariableDecl | VariableDecl Grammar
     * VariableDecl -> Nonterminal Arrow ProductionList
     * ProductionList -> Production | Production Bar ProductionList
     * Production -> Epsilon | String
     * String -> empty string | Terminal String | Nonterminal String
     */

    /* Peek, with error-checking. */
    CFG::Token peek(const deque<CFG::Token>& input, size_t index = 0) {
        if (index >= input.size()) parseError("Unexpected end of input found.");
        return input[index];
    }

    /* Dequeue, with error-checking. */
    CFG::Token dequeue(deque<CFG::Token>& input) {
        auto result = peek(input, 0);
        input.pop_front();
        return result;
    }

    /* Production -> Epsilon | String
     * String -> empty string | Terminal String | Nonterminal String
     */
    CFG::Production parseProduction(deque<CFG::Token>& input, const Languages::Alphabet& alphabet,
                                    char32_t nonterminal) {
        /* Could be an empty production. */
        if (peek(input).type == CFG::TokenType::EPSILON) {
            (void) dequeue(input);
            return { nonterminal, {} };
        }

        /* Otherwise, we're reading a string. Keep reading characters until one of the following
         * happens:
         *
         * 1. The lookahead is a bar or EOF. In that case, we are definitely done.
         * 2. The lookahead is of the form NONTERMINAL ARROW. When that happens, we know that
         *    what we're reading is the start of the next grammar rule.
         */
        vector<CFG::Symbol> symbols;
        while (peek(input).type != CFG::TokenType::BAR &&
               peek(input).type != CFG::TokenType::SCAN_EOF &&
               !(peek(input).type == CFG::TokenType::NONTERMINAL && peek(input, 1).type == CFG::TokenType::ARROW)) {
            /* Should be a terminal or a nonterminal. */
            auto token = dequeue(input);
            if (token.type == CFG::TokenType::TERMINAL) {
                /* Validate token. */
                if (!alphabet.count(token.data)) {
                    parseError("Character '" + toUTF8(token.data) + "' is not in alphabet.");
                }
                symbols.push_back({ CFG::Symbol::Type::TERMINAL, token.data });
            } else if (token.type == CFG::TokenType::NONTERMINAL) {
                symbols.push_back({ CFG::Symbol::Type::NONTERMINAL, token.data });
            } else {
                parseError("Unexpected token.");
            }
        }

        return { nonterminal, symbols };
    }

    /* ProductionList -> Production | Production Bar ProductionList */
    vector<CFG::Production> parseProductionList(deque<CFG::Token>& input, const Languages::Alphabet& alphabet,
                                                char32_t nonterminal) {
        vector<CFG::Production> result;

        /* Read at least one production, and possibly more. */
        while (true) {
            result.push_back(parseProduction(input, alphabet, nonterminal));

            /* Bar? Keep going. */
            if (peek(input).type != CFG::TokenType::BAR) break;
            (void) dequeue(input);
        }

        return result;
    }

    /* VariableDecl -> Nonterminal Arrow ProductionList */
    vector<CFG::Production> parseVariableDecl(deque<CFG::Token>& input, const Languages::Alphabet& alphabet) {
        auto nonterminal = dequeue(input);
        if (nonterminal.type != CFG::TokenType::NONTERMINAL) parseError("Expected a nonterminal.");

        if (dequeue(input).type != CFG::TokenType::ARROW) parseError("Expected an arrow.");
        return parseProductionList(input, alphabet, nonterminal.data);
    }

    /* Grammar -> VariableDecl | VariableDecl Grammar */
    CFG::CFG parseGrammar(deque<CFG::Token>& input, const Languages::Alphabet& alphabet) {
        CFG::CFG result;
        result.alphabet = alphabet;

        /* Read a list of variable declarations. */
        do {
            auto variable = parseVariableDecl(input, alphabet);
            if (variable.empty()) abort(); // Not possible

            /* Integrate this into the grammar. */
            char32_t nonterminal = variable[0].nonterminal;
            result.nonterminals.insert(nonterminal);
            if (result.startSymbol == char32_t(0)) result.startSymbol = nonterminal;

            for (const auto& entry: variable) {
                result.productions.push_back(entry);
            }

        } while (input.front().type != CFG::TokenType::SCAN_EOF);

        if (result.nonterminals.empty()) parseError("No productions found.");
        return result;
    }

    /* JSON decoder. This is used to import data from the old CFG tool and to
     * decode CFGs that use more nonterminals than the scanner can handle.
     */
    void parseJSONRule(CFG::CFG& result, const Languages::Alphabet& alphabet, JSON rule) {
        CFG::Production p;
        p.nonterminal = fromUTF8(rule["name"].asString());

        for (JSON symbol: rule["production"]) {
            CFG::Symbol s;
            s.ch = fromUTF8(symbol["data"].asString());

            if (symbol["type"].asString() == "T") {
                s.type = CFG::Symbol::Type::TERMINAL;
                if (!alphabet.count(s.ch)) parseError("Illegal terminal: " + symbol["data"].asString());
            } else if (symbol["type"].asString() == "NT") {
                s.type = CFG::Symbol::Type::NONTERMINAL;
                result.nonterminals.insert(s.ch);
            } else parseError("Unknown type: " + symbol["type"].asString());

            p.replacement.push_back(s);
        }

        result.nonterminals.insert(p.nonterminal);
        result.productions.push_back(p);
    }
}

/* Read from token stream. */
CFG::CFG parseCFG(deque<CFG::Token> input, const Languages::Alphabet& alphabet) {
    return parseGrammar(input, alphabet);
}

/* Parse a "classical" JSON data object. The format is the following:
 *
 * {"start": "start symbol",
 *  "rules": [ rule* ]}
 *
 * Here, a rule has this form:
 *
 *   { "name": "left hand side of the production",
 *     "production": [ symbol* ] }
 *
 * Each symbol is then
 *
 *   { "type": "T for terminal, NT for nonterminal",
 *     "data": "the actual character" }
 */
CFG::CFG parseCFG(JSON data, const Languages::Alphabet& alphabet) {
    /* Copy basic data. */
    CFG::CFG result;
    result.startSymbol = fromUTF8(data["start"].asString());
    result.alphabet = alphabet;

    /* Process each rule to get the nonterminals / productions. */
    for (JSON rule: data["rules"]) {
        parseJSONRule(result, alphabet, rule);
    }

    return result;
}
