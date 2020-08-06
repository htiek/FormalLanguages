#include "FormalLanguages/CFGParser.h"
#include "FormalLanguages/RegexParser.h"
#include "FormalLanguages/Regex.h"
#include "FormalLanguages/RegexScanner.h"
#include "FormalLanguages/Automaton.h"
#include "FormalLanguages/Utilities/Unicode.h"
#include "FormalLanguages/Utilities/CSV.h"
#include "FormalLanguages/Utilities/JSON.h"
#include "FileParser/FileParser.h"
#include "console.h"
#include "simpio.h"
#include "filelib.h"
#include "vector.h"
#include <sstream>
#include <iostream>
#include <random>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <iomanip>
#include <typeindex>
#include <fstream>
#include <map>
#include <chrono>
#include "random.h"
using namespace std;

namespace {
    const size_t kMaxSize      = 15;
    const size_t kTestsPerSize = 1000;

    /* Fuzz-tests two CFGs against one another. Returns whether they seem to match, and if
     * not outputs a string that they disagree on.
     *
     * It's, in general, undecidable whether two CFGs are equal to one another, so there
     * isn't some magic nice procedure we can use to speed this up.
     */
    bool seemEquivalent(const CFG::CFG& one, const CFG::CFG& two, string& out) {
        auto match1 = CFG::matcherFor(one);
        auto match2 = CFG::matcherFor(two);
        auto gen1   = CFG::generatorFor(one);
        auto gen2   = CFG::generatorFor(two);

        for (size_t i = 0; i < kMaxSize; i++) {
            for (size_t trial = 0; trial < kTestsPerSize; trial++) {
                /* L(one) subset L(two)? */
                auto str1 = gen1(i);
                if (str1.first && !match2(str1.second)) {
                    out = str1.second;
                    return false;
                }

                /* L(two) subset L(one)? */
                auto str2 = gen2(i);
                if (str2.first && !match1(str2.second)) {
                    out = str2.second;
                    return false;
                }
            }
        }

        return true;
    }
}

/******************************************************************************
 * Logic to obfuscate a CFG. This algorithm works by computing the intersection
 * of the CFG with several disjoint regular languages that collectively make up
 * Sigma*, then unioning all the grammars together. This dramatically increases
 * the size of the nonterminal space, rendering the grammar impractical to
 * reverse-engineer.
 */
namespace {
    void validate(const CFG::CFG& cfg) {
        set<char32_t> producers;

        for (const auto& prod: cfg.productions) {
            const auto& p = prod.replacement;

            producers.insert(prod.nonterminal);

            /* All symbols must be included. */
            for (auto s: p) {
                if (s.type == CFG::Symbol::Type::TERMINAL) {
                    if (!cfg.alphabet.count(s.ch)) abort();
                } else {
                    if (!cfg.nonterminals.count(s.ch)) abort();
                }
            }
        }

        /* All nonterminals must produce something. */
        if (producers != cfg.nonterminals) abort();
    }

    /* Given a DFA, returns a DFA accepting the complement of its language. */
    Automata::DFA complementOf(Automata::DFA dfa) {
        for (auto state: dfa.states) {
            state->isAccepting = !state->isAccepting;
        }
        return dfa;
    }

    const char32_t kBaseUnicode = 0x1F300;

    /* Produces a CFG that generates exactly the given strings. */
    CFG::CFG cfgForSingletons(const set<string>& strings, const Languages::Alphabet& alphabet) {
        CFG::CFG result;
        result.alphabet = alphabet;
        result.startSymbol = 'S';
        result.nonterminals = { 'S' };

        for (string str: strings) {
            CFG::Production p;
            p.nonterminal = 'S';
            for (char32_t ch: utf8Reader(str)) {
                p.replacement.push_back(CFG::terminal(ch));
            }
            result.productions.push_back(p);
        }
        return result;
    }

    /* Renames all the symbols in a CFG in a decidedly silly way. */
    CFG::CFG sillyRename(const CFG::CFG& cfg) {
        CFG::CFG result;
        result.alphabet = cfg.alphabet;

        /* Map old nonterminal names to new nonterminal names. */
        map<char32_t, char32_t> replacements;
        char32_t next = kBaseUnicode;
        auto nameFor = [&](char32_t ch) {
            if (!replacements.count(ch)) {
                replacements[ch] = next;
                result.nonterminals.insert(next);
                next++;
            }
            return replacements[ch];
        };

        /* Clone productions. */
        for (auto prod: cfg.productions) { // Copy, not ref
            prod.nonterminal = nameFor(prod.nonterminal);
            for (auto& symbol: prod.replacement) {
                if (symbol.type == CFG::Symbol::Type::NONTERMINAL) {
                    symbol.ch = nameFor(symbol.ch);
                }
            }
            result.productions.push_back(prod);
        }

        result.startSymbol = nameFor(cfg.startSymbol);
        return result;
    }

    string escape(const string& input) {
        string result;
        for (char32_t ch: utf8Reader(input)) {
            if (Regex::isSpecialChar(ch)) {
                result += "\\";
            }
            result += toUTF8(ch);
        }
        return result;
    }

    /* Obfuscates a CFG without changing the language. The basic idea is the following:
     *
     * 1. Sample a set X of random strings from the CFG.
     * 2. Intersect the CFG with a DFA that accepts everything except X.
     * 3. Union that grammar with the simple grammar S -> X1 | X2 | ... | Xn.
     * 4. Clean things and convert to (weak) CNF.
     *
     * The effect of step (2) is to mask much of the original structure of
     * the grammar.
     */
    const size_t kNumStrings = 10;
    CFG::CFG obfuscate(CFG::CFG cfg) {
        auto gen = CFG::generatorFor(cfg);

        /* Get some reasonable-length strings. */
        set<string> singletons;
        for (size_t len = 5; ; len++) {
            auto str = gen(len);
            if (str.first) {
                singletons.insert(str.second);
                if (singletons.size() == kNumStrings) break;
            }
        }

        /* Form the regex. */
        string regex = "@ ";
        for (string s: singletons) {
            regex += " | " + escape(s);
        }
        cout << regex << endl;

        /* Form a DFA that accepts everything but this string. */
        auto dfa = complementOf(Automata::minimalDFAFor(Automata::subsetConstruct(Automata::fromRegex(Regex::parse(Regex::scan(regex)), cfg.alphabet))));

        /* Get the intersection of that DFA and the CFG. */
        auto allButSingleton = intersect(cfg, dfa);
        validate(allButSingleton);

        /* Union that grammar with one that only produces the singleton. */
        cfg = unionOf(allButSingleton, cfgForSingletons(singletons, cfg.alphabet));
        validate(cfg);

        cout << "Base NTs: " << cfg.nonterminals.size() << endl;
        cout << "Base Prs: " << cfg.productions.size() << endl;

        cfg = toCNF(cfg);
        validate(cfg);

        cout << "Final NTs: " << cfg.nonterminals.size() << endl;
        cout << "Final PRs: " << cfg.productions.size() << endl;

        cfg = sillyRename(cfg);
        validate(cfg);

        return cfg;
    }

    /* Writes a "classical" JSON data object. The format is the following:
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
    JSON jsonRules(const CFG::CFG& cfg) {
        vector<JSON> rules;
        for (const auto& prod: cfg.productions) {
            const auto& p = prod.replacement;

            vector<JSON> symbols;
            for (const auto& s: p) {
                symbols.push_back(JSON::object({
                    { "type", s.type == CFG::Symbol::Type::TERMINAL? "T": "NT" },
                    { "data", toUTF8(s.ch) }
                }));
            }

            rules.push_back(JSON::object({
                { "name", toUTF8(prod.nonterminal) },
                { "production", symbols }
            }));
        }

        return rules;
    }

    JSON toJSON(const CFG::CFG& cfg) {
        return JSON::object({
            { "start", toUTF8(cfg.startSymbol) },
            { "rules", jsonRules(cfg) }
        });
    }

    string toString(const Languages::Alphabet& alphabet) {
        string result;
        for (char32_t ch: alphabet) {
            result += toUTF8(ch);
        }
        return result;
    }

    void generateObfuscated(const string& partName, const Languages::Alphabet& alphabet) {
        cout << "Processing " << partName << endl;

        auto cfg = parseCFG(CFG::scan(*parseFile("res/Grammars.cfgs").at("[" + partName + "]")), alphabet);

        cout << "Reference grammar: " << endl;
        cout << cfg << endl;

        auto obs = obfuscate(cfg);

        cout << "Obfuscated grammar: " << endl;
        cout << "# NTs: " << obs.nonterminals.size() << endl;
        cout << "# Prs: " << obs.productions.size() << endl;

        string unused;
        if (!seemEquivalent(cfg, obs, unused)) {
            cerr << unused << endl;
            abort();
        }

        JSON result = JSON::object({
            { "alphabet", toString(alphabet) },
            { "cfg",      toJSON(obs) }
        });

        ofstream output(partName);
        output << result;
    }
}

int main() {
    generateObfuscated("Q1.i",    {'a', 'b', 'c'});
    generateObfuscated("Q1.ii",   {'a', '.', '@'});
    generateObfuscated("Q1.iii",  {'a', 'b'});
    generateObfuscated("Q1.iv",   {'n', 'u', 'o', '{', '}', ','});
    generateObfuscated("Q2.ii",   {'1', '+', '='});
    generateObfuscated("Q3.ii",   {'y', 'd'});

    return 0;
}
