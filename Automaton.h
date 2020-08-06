/* Types for manipulating automata. */

#pragma once
#include "Languages.h"
#include "Regex.h"
#include <map>
#include <unordered_set>
#include <memory>
#include <string>

namespace Automata {
    /* char32_t value representing an epsilon transition. */
    const char32_t EPSILON_TRANSITION = 0;

    /* Type representing a state in an automaton. */
    struct State {
        bool isAccepting = false;
        bool isStart     = false; // Allow for multiple start states; needed in some algs
        std::string name;

        /* Transitions. This is a multimap so that we can use this as
         * either a DFA or an NFA state.
         *
         * Epsilon transitions are represented, implicitly, as transitions
         * on EPSILON_TRANSITION.
         */
        std::multimap<char32_t, State*> transitions;
    };

    struct NFA {
        virtual ~NFA() = default;

        std::unordered_set<std::shared_ptr<State>> states;
        Languages::Alphabet alphabet;

        /* Utility function to create a state in the NFA. */
        State* newState(const std::string& name, bool isStart = false, bool isAccepting = false);

        /* Support deep-copying, for simplicity. */
        NFA() = default;
        NFA(const NFA& rhs);
        NFA(NFA &&);
        NFA& operator= (NFA rhs);
    };

    /* A DFA is a specific type of NFA. */
    struct DFA: NFA {};


    /* Helper routines for automata. */

    /* Serialize / deserialize */
    std::ostream& operator<< (std::ostream& out, const NFA& nfa);
    std::istream& operator>> (std::istream& in,  NFA& nfa);

    std::ostream& operator<< (std::ostream& out, const DFA& dfa);
    std::istream& operator>> (std::istream& in,  DFA& dfa);

    /* Generate Graphviz data. */
    std::string toDot(const NFA& nfa);

    std::unordered_set<State*> deltaStar(const NFA& automaton, const std::string& input);
    bool accepts(const NFA& automaton, const std::string& input);

    NFA  fromRegex(Regex::Regex regex, const Languages::Alphabet& alphabet);

    DFA  subsetConstruct(const NFA& automaton);

    NFA  reverseOf(const NFA& nfa);
    DFA  minimalDFAFor(const NFA& automaton);

    DFA  xorConstruct(const DFA& lhs, const DFA& rhs);
    bool shortestStringIn(const NFA& lhs, std::string& result);

    bool areEquivalent(const DFA& lhs, const DFA& rhs, std::string& counterexample);
}
