#pragma once

#include "Languages.h"
#include "Automaton.h"
#include <string>
#include <memory>
#include <vector>
#include <set>
#include <ostream>
#include <functional>
#include <map>

namespace CFG {
    /* A symbol in a production. */
    struct Symbol {
        enum class Type {
            TERMINAL,
            NONTERMINAL
        };

        Type type;
        char32_t ch;
    };

    /* Convenience helpers. */
    inline Symbol terminal(char32_t ch) {
        return { Symbol::Type::TERMINAL, ch };
    }
    inline Symbol nonterminal(char32_t ch) {
        return { Symbol::Type::NONTERMINAL, ch };
    }

    /* Type representing a production rule. */
    struct Production {
        char32_t nonterminal;
        std::vector<Symbol> replacement;
    };

    /* Type representing a CFG. This is a "pure data" version of CFG; operations on
     * CFGs that need to do more elaborate processing might bundle it with some
     * other information.
     */
    struct CFG {
        Languages::Alphabet alphabet;
        std::set<char32_t> nonterminals;

        char32_t startSymbol = char32_t(0);
        std::vector<Production> productions;
    };

    /* Type representing a derivation of a string from a grammar. Each item in the
     * vector is a pair of the form (production, position).
     */
    using Derivation = std::vector<std::pair<Production, std::size_t>>;

    /* Input is a string, output is a boolean for whether we match. */
    using Matcher = std::function<bool(const std::string&)>;

    /* Input is a string, output is a derivation. */
    using Deriver = std::function<Derivation (const std::string&)>;

    /* Input is a length, output is a pair of "can we make it?" and a string. */
    using Generator = std::function<std::pair<bool, std::string>(std::size_t)>;

    /* We support three different matchers. */
    enum class MatcherType {
        EARLEY_LR0, // General purpose, time-optimized. Use as default.
        EARLEY,     // General purpose, fast for unambiguous grammars, slower as it gets more ambiguous
        CYK,        // Only works on (weak) CNF; somewhat slow.
    };

    Matcher   matcherFor(const CFG& cfg, MatcherType type = MatcherType::EARLEY_LR0);
    Deriver   deriverFor(const CFG& cfg);    // Earley
    Generator generatorFor(const CFG& cfg);  // McKenzie

    /* * * * * CFG Utility Functions * * * * */

    /* Converts a grammar to Chomsky normal form. The nonterminals in the resulting
     * grammar may have names that bear no resemblance to the original grammar's
     * nonterminal names.
     */
    CFG toCNF(const CFG& cfg);

    /* Converts a grammar to "weak CNF." This is like CNF except that unit
     * productions are permitted as long as they don't form a cycle. This helps
     * keep the size of the resulting grammar smaller than regular CNF.
     */
    CFG toWeakCNF(const CFG& cfg);

    /* * * * * Language Transforms * * * * */

    /* Returns a new CFG whose language is the intersection of the languages of the
     * input CFG and DFA. (CFLs aren't closed under intersection, but they are closed
     * under intersection with regular languages.) The inputs must have the same
     * alphabets for this construction to work.
     *
     * The nonterminals in the resulting CFG will be chosen Arbitrarily and
     * Capriciously.
     */
    CFG intersect(const CFG& lhs, const Automata::DFA& rhs);

    /* Returns a new CFG whose language is the union of the languages of the
     * input CFGs. The inputs must have the same alphabets for this construction
     * to work.
     *
     * The nonterminals in the resulting CFG will be chosen Arbitrarily and
     * Capriciously.
     */
    CFG unionOf(const CFG& lhs, const CFG& rhs);


    /* * * * * C++ Utility Functions * * * * */
    bool operator== (const Symbol& lhs, const Symbol& rhs);
    bool operator<  (const Production& lhs, const Production& rhs);
    bool operator<  (const Symbol& lhs, const Symbol& rhs);

    std::ostream& operator<< (std::ostream& out, const Symbol& symbol);
    std::ostream& operator<< (std::ostream& out, const Production& production);
    std::ostream& operator<< (std::ostream& out, const Derivation& derivation);
    std::ostream& operator<< (std::ostream& out, const CFG& cfg);
}

/* Hash support. */
namespace std {
    template <> struct hash<CFG::Symbol> {
        std::size_t operator()(const CFG::Symbol& s) const {
            return s.ch * 997 + std::size_t(s.type);
        }
    };
}
