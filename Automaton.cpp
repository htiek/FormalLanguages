#include "Automaton.h"
#include "Utilities.h"
#include "Utilities/Unicode.h"
#include "Utilities/JSON.h"
#include <unordered_map>
#include <queue>
#include <functional>
#include <sstream>
#include <set>
#include <algorithm>
#include <iostream>
using namespace std;

namespace Automata {
    namespace  {
        /* Runs a breadth-first search over an automaton, invoking a predicate at each
         * step, and stopping once the predicate returns false (or all states are visited).
         *
         * You can optionally specify an edge filter, which will limit the search to just
         * those edges that pass the predicate.
         */
        void bfs(unordered_set<State*> start,
                 function<void(State*)> callback,
                 function<bool(State*, char32_t, State*)> edgeFilter = [](State*, char32_t, State*) { return true; } ) {
            queue<State*> worklist;

            /* Add all start states. */
            for (auto state: start) {
                worklist.push(state);
            }
            auto visited = start;

            while (!worklist.empty()) {
                auto curr = worklist.front();
                worklist.pop();
                callback(curr);

                /* Expand outward. */
                for (const auto& entry: curr->transitions) {
                    if (!visited.count(entry.second)) {
                        if (edgeFilter(curr, entry.first, entry.second)) {
                            visited.insert(entry.second);
                            worklist.push(entry.second);
                        }
                    }
                }
            }
        }

        /* Returns all start states of an automaton. */
        unordered_set<State*> startStatesOf(const NFA& automaton) {
            unordered_set<State*> result;
            for (const auto& state: automaton.states) {
                if (state->isStart) result.insert(state.get());
            }
            return result;
        }

        /* Convenience function to add a transition to an automaton. */
        void addTransition(State* from, State* to, char32_t ch) {
            from->transitions.insert(make_pair(ch, to));
        }
        void addTransition(shared_ptr<State> from, shared_ptr<State> to, char32_t ch) {
            addTransition(from.get(), to.get(), ch);
        }

        /* Clones the states of an automaton. */
        unordered_map<State*, shared_ptr<State>> cloneStatesOf(const NFA& automaton) {
            unordered_map<State*, shared_ptr<State>> result;

            /* Clone the states. */
            for (const auto& state: automaton.states) {
                /* Copy everything over, and clear out transitions since those
                 * are about to be overwritten.
                 */
                result[state.get()] = make_shared<State>(*state);
                result[state.get()]->transitions.clear();
            }

            /* Run a second pass to rewire things. */
            for (auto& entry: result) {
                for (const auto& transition: entry.first->transitions) {
                    addTransition(entry.second, result[transition.second], transition.first);
                }
            }

            return result;
        }
    }

    /* Copy and move constructors. */
    NFA::NFA(const NFA& rhs) {
        /* Map from old states to new ones. */
        auto map = cloneStatesOf(rhs);

        /* Copy over states. */
        for (const auto& state: rhs.states) {
            states.insert(map[state.get()]);
        }

        /* Copy alphabet. */
        alphabet = rhs.alphabet;
    }

    NFA::NFA(NFA&& rhs) : NFA() {
        swap(states, rhs.states);
        swap(alphabet, rhs.alphabet);
    }

    /* Assignment operator. */
    NFA& NFA::operator= (NFA rhs) {
        swap(states, rhs.states);
        swap(alphabet, rhs.alphabet);
        return *this;
    }

    /* State utilities. */
    State* NFA::newState(const string& name, bool isStart, bool isAccepting) {
        auto result = make_shared<State>();
        result->name = name;
        result->isStart = isStart;
        result->isAccepting = isAccepting;

        states.insert(result);
        return result.get();
    }

    /* Printing. */
    namespace {
        /* Automaton to JSON.
         *
         * The JSON format is
         *
         * { "type": "(DFA or NFA)",
         *   "alphabet": "all alpha chars",
         *   "states": [["unique id", "name", start, accepting],
         *   "transitions": [[from, to, "char"],...] }
         */
        string toString(const Languages::Alphabet& alphabet) {
            string result;
            for (char32_t ch: alphabet) {
                result += toUTF8(ch);
            }
            return result;
        }

        string toString(State* state) {
            ostringstream converter;
            converter << state;
            return converter.str();
        }

        string toString(const shared_ptr<State>& state) {
            return toString(state.get());
        }

        JSON jsonStateFor(State* state) {
            return JSON::array(toString(state), state->name, state->isStart, state->isAccepting);
        }

        JSON jsonStatesFor(const unordered_set<shared_ptr<State>>& states) {
            vector<JSON> result;
            for (const auto& state: states) {
                result.push_back(jsonStateFor(state.get()));
            }
            return result;
        }

        JSON jsonTransitionsFor(const unordered_set<shared_ptr<State>>& states) {
            vector<JSON> result;
            for (const auto& state: states) {
                for (const auto& transition: state->transitions) {
                    string label = (transition.first == EPSILON_TRANSITION? "" : toUTF8(transition.first));
                    result.push_back(JSON::array(toString(state), toString(transition.second), label));
                }
            }
            return result;
        }

        JSON jsonFor(const NFA& automaton, const string& type) {
            return JSON::object({
                { "type", type },
                { "alphabet", toString(automaton.alphabet) },
                { "states", jsonStatesFor(automaton.states) },
                { "transitions", jsonTransitionsFor(automaton.states) }
            });
        }

        /* Serializes an automaton. The output format is
         *
         * $json'$
         *
         * where json' is the JSON encoding of the automaton with all $'s doubled
         * to make the stream self-delimiting.
         */
        void writeAutomaton(ostream& out, const NFA& nfa, const string& type) {
            out << jsonFor(nfa, type);
        }

        /* Decodes an alphabet from a string. */
        Languages::Alphabet readJSONAlphabet(const string& alphabet) {
            istringstream input(alphabet);
            Languages::Alphabet result;

            while (input.peek() != EOF) {
                result.insert(readChar(input));
            }

            return result;
        }

        /* Reconstructs an automaton from a JSON encoded pair of states and transitions. */
        void readJSONStates(NFA& nfa, JSON states, JSON transitions) {
            /* Translation map: name of state to actual state. */
            unordered_map<string, State*> translation;

            /* Read the list of states. */
            for (JSON state: states) {
                if (translation.count(state[0].asString())) {
                    throw runtime_error("Duplicate state.");
                }

                /* Decode information. */
                auto s = make_shared<State>();
                s->name        = state[1].asString();
                s->isStart     = state[2].asBoolean();
                s->isAccepting = state[3].asBoolean();

                /* Stash for later. */
                translation[state[0].asString()] = s.get();
                nfa.states.insert(s);
            }

            /* Read transitions. */
            for (JSON transition: transitions) {
                auto from   = translation.at(transition[0].asString());
                auto to     = translation.at(transition[1].asString());
                char32_t ch = transition[2].asString() == ""? EPSILON_TRANSITION : fromUTF8(transition[2].asString());
                addTransition(from, to, ch);
            }
        }

        /* Reads an automaton from the given stream. */
        void readAutomaton(istream& in, NFA& out, const unordered_set<string>& acceptable) {
            if (istream::sentry(in)) {
                try {
                    JSON json = nullptr;
                    in >> json;
                    if (!in) throw runtime_error("Can't decode JSON.");

                    /* Confirm the type of automaton read matches what's expected. */
                    if (!acceptable.count(json["type"].asString())) {
                        throw runtime_error("Wrong type of automaton.");
                    }

                    NFA result;
                    result.alphabet = readJSONAlphabet(json["alphabet"].asString());
                    readJSONStates(result, json["states"], json["transitions"]);

                    /* Use the newly-generated automaton as our result. */
                    out = std::move(result);
                } catch (const exception& e) {
                    in.setstate(ios::failbit);
                    cerr << e.what() << endl;
                    return;
                }
            }
        }
    }

    ostream& operator<< (ostream& out, const NFA& nfa) {
        writeAutomaton(out, nfa, "NFA");
        return out;
    }
    ostream& operator<< (ostream& out, const DFA& dfa) {
        writeAutomaton(out, dfa, "DFA");
        return out;
    }

    istream& operator>> (istream& in, NFA& nfa) {
        readAutomaton(in, nfa, {"NFA", "DFA"});
        return in;
    }

    istream& operator>> (istream& in, DFA& dfa) {
        readAutomaton(in, dfa, {"DFA"});
        return in;
    }

    /* Produce a .dot string. */
    string toDot(const NFA& nfa) {
        ostringstream builder;
        unordered_map<State*, size_t> stateMap;

        builder << "digraph G {" << endl;
        builder << "start [shape=\"point\"]" << endl;
        /* States */
        for (auto state: nfa.states) {
            stateMap.insert(make_pair(state.get(), stateMap.size()));
            builder << stateMap[state.get()] << "[shape=" << (state->isAccepting? "doubleoctagon" : "octagon")
                                         << " label=\"" << state->name << "\"]" << endl;
        }

        /* Transitions */
        for (auto state: nfa.states) {
            for (auto transition: state->transitions) {
                string label = (transition.first == EPSILON_TRANSITION? "ε" : toUTF8(transition.first));
                builder << stateMap[state.get()] << " -> " << stateMap[transition.second] << " [label =\"" << label << "\"]" << endl;
            }
        }

        /* Start state. */
        for (auto state: startStatesOf(nfa)) {
            builder << "start -> " << stateMap[state] << endl;
        }

        builder << "}";
        return builder.str();
    }

    /* Thompson's algorithm for converting a regular expression into an NFA.
     * This works by replacing each regex with a new automaton with exactly
     * one accepting state, no transitions into the start state, and no
     * transitions out of the accepting state.
     */
    NFA fromRegex(Regex::Regex regex, const Languages::Alphabet& alphabet) {
        /* Confirm compatibility.*/
        if (!Languages::isSubsetOf(Regex::coreAlphabetOf(regex), alphabet)) {
            Utilities::error("Regular expression has wrong alphabet.");
        }

        /* Desugar the regex to make it a "pure" regex. */
        regex = Regex::desugar(regex, alphabet);

        struct ThompsonPair {
            shared_ptr<State> start;
            shared_ptr<State> end;
        };

        /* Builder that knows how to process each type into a ThompsonPair.
         *
         * For simplicity, we will NOT mark any of the states as accepting.
         * That's okay, because we know that only one state at the end will
         * be accepting, and it's the second pair of the Thompson pair.
         */
        struct Builder: public Regex::Calculator<ThompsonPair> {
            NFA& out;
            Builder(NFA& out) : out(out) {}

            shared_ptr<State> newState() {
                auto result = make_shared<State>();
                result->name = "q" + to_string(out.states.size());
                out.states.insert(result);
                return result;
            }

            ThompsonPair handle(Regex::Character* expr) override {
                /*
                 *  [   ] -- ch -->  [[ ]]
                 */
                auto start = newState();
                auto end   = newState();
                addTransition(start, end, expr->ch);
                return { start, end };
            }

            ThompsonPair handle(Regex::Sigma *) override {
                Utilities::error("Impossible.");
            }

            ThompsonPair handle(Regex::Epsilon *) override {
                /*
                 *  [   ] -- eps -->  [[ ]]
                 */
                auto start = newState();
                auto end   = newState();
                addTransition(start, end, EPSILON_TRANSITION);
                return { start, end };
            }

            ThompsonPair handle(Regex::EmptySet *) override {
                /*
                 *  [   ]           [[ ]]
                 */
                return { newState(), newState() };
            }

            ThompsonPair handle(Regex::Union *, ThompsonPair left, ThompsonPair right) override {
                /*
                 *            [   ]  --->  [[ ]]
                 *           /                  \
                 *        [  ]                 [[ ]]
                 *           \                  /
                 *            [   ]  --->  [[ ]]
                 *
                 */
                auto start = newState();
                auto end   = newState();

                /* Epsilons out of start. */
                addTransition(start, left.start,  EPSILON_TRANSITION);
                addTransition(start, right.start, EPSILON_TRANSITION);

                /* Epsilons into end. */
                addTransition(left.end,  end, EPSILON_TRANSITION);
                addTransition(right.end, end, EPSILON_TRANSITION);

                return { start, end };
            }

            ThompsonPair handle(Regex::Concat *, ThompsonPair left, ThompsonPair right) override {
                /*
                 * [   ] ---> [[ ]] -- eps -> [   ] --> [[ ]]
                 */

                /* Epsilons out of start. */
                addTransition(left.end, right.start,  EPSILON_TRANSITION);
                return { left.start, right.end };
            }

            ThompsonPair handle(Regex::Star *, ThompsonPair child) override {
                /*
                 *   +-------------------------------------------------+
                 *   |                                                 v
                 * [   ]  -- eps -->   [   ] ---> [[ ]]  -- eps -->  [[ ]]
                 *                       ^          |
                 *                       |          |
                 *                       +----------+
                 */

                auto start = newState();
                auto end   = newState();

                addTransition(start, child.start, EPSILON_TRANSITION);
                addTransition(child.end, end, EPSILON_TRANSITION);
                addTransition(child.end, child.start, EPSILON_TRANSITION);
                addTransition(start, end, EPSILON_TRANSITION);

                return { start, end };
            }

            ThompsonPair handle(Regex::Plus *, ThompsonPair) override {
                Utilities::error("Impossible.");
            }

            ThompsonPair handle(Regex::Question *, ThompsonPair) override {
                Utilities::error("Impossible.");
            }

            ThompsonPair handle(Regex::Power *, ThompsonPair) override {
                Utilities::error("Impossible.");
            }
        };

        NFA result;
        result.alphabet = alphabet;
        Builder builder(result);
        ThompsonPair final = builder.calculate(regex);

        final.start->isStart = true;
        final.end->isAccepting = true;

        return result;
    }

    namespace {
        /* Computes the epsilon-closure of a given state (all the states reachable from that
         * state via epsilon transitions.
         */
        unordered_set<State*> epsilonClosureOf(unordered_set<State*> states) {
            unordered_set<State*> result;
            bfs(states,
                [&](State* s) { result.insert(s); },
                [](State*, char32_t ch, State*) { return ch == EPSILON_TRANSITION; });
            return result;
        }
        unordered_set<State*> epsilonClosureOf(State* state) {
            return epsilonClosureOf(unordered_set<State*>{ state });
        }
    }

    /* Computes δ*(w) for an automaton D and string w. */
    unordered_set<State*> deltaStar(const NFA& automaton, const string& str) {
        unordered_set<State*> curr = epsilonClosureOf(startStatesOf(automaton));

        for (istringstream input(str); input.peek() != EOF; ) {
            /* Read the next character. */
            char32_t ch = readChar(input);
            if (!automaton.alphabet.count(ch)) {
                Utilities::error("Character not in alphabet: " + toUTF8(ch));
            }

            /* Follow all transitions labeled with this character, taking their
             * epsilon closures.
             */
            unordered_set<State*> next;
            for (State* state: curr) {
                auto range = state->transitions.equal_range(ch);
                for (auto itr = range.first; itr != range.second; ++itr) {
                    auto dest = epsilonClosureOf(itr->second);
                    next.insert(dest.begin(), dest.end());
                }
            }

            curr = next;
        }

        return curr;
    }

    /* w in L(D)   <->   F n delta*_D(w) != empty */
    bool accepts(const NFA& automaton, const string& str) {
        for (State* state: deltaStar(automaton, str)) {
            if (state->isAccepting) return true;
        }
        return false;
    }

    namespace {
        /* Converts an std::unordered_set to a std::set. */
        template <typename T>
        set<T> toSet(const std::unordered_set<T>& s) {
            return { s.begin(), s.end() };
        }

        /* Creates a new DFA state for the set of NFA states. */
        void makeDFAStateFor(const set<State*>& nfaStates,
                             DFA& dfa,
                             map<set<State*>, State*>& translation) {
            auto dfaState = make_shared<State>();

            /* This state is accepting if any of the NFA states are. */
            dfaState->isAccepting = any_of(nfaStates.begin(), nfaStates.end(), [](State* s) {
                return s->isAccepting;
            });

            /* Name is the set of states it's made of. */
            dfaState->name = "{";
            for (auto itr = nfaStates.begin(); itr != nfaStates.end(); ++itr) {
                dfaState->name += (*itr)->name + (next(itr) == nfaStates.end()? "" : ", ");
            }
            dfaState->name += "}";

            dfa.states.insert(dfaState);
            translation[nfaStates] = dfaState.get();
        }
    }

    /* Uses the subset construction to produce a DFA with the same language
     * as the input automaton.
     */
    DFA subsetConstruct(const NFA& nfa) {
        /* Alphabet stays the same. */
        DFA result;
        result.alphabet = nfa.alphabet;

        /* Table mapping from sets of NFA states to DFA states. */
        map<set<State*>, State*> translation;

        /* Worklist of what to process. */
        queue<set<State*>> worklist;

        /* Seed with the start state. */
        auto initial = toSet(epsilonClosureOf(startStatesOf(nfa)));
        worklist.push(initial);
        makeDFAStateFor(initial, result, translation);
        translation[initial]->isStart = true;

        /* Search outward! */
        while (!worklist.empty()) {
            auto curr = worklist.front();
            worklist.pop();

            /* Expand outward. */
            for (char32_t ch: nfa.alphabet) {
                set<State*> successor;

                /* For each state, find all successors for this character. */
                for (const auto& state: curr) {
                    auto range = state->transitions.equal_range(ch);
                    for (auto itr = range.first; itr != range.second; ++itr) {
                        /* Add in the epsilon closure of the destination.
                         *
                         * TODO: For efficiency, compute epsilon closures only once rather
                         * than recomputing from scratch each time?
                         */
                        auto destinations = epsilonClosureOf(itr->second);
                        successor.insert(destinations.begin(), destinations.end());
                    }
                }

                /* If we don't already know this configuration, add it to the worklist. */
                if (!translation.count(successor)) {
                    makeDFAStateFor(successor, result, translation);
                    worklist.push(successor);
                }

                /* Take the current DFA state and wire its transition on this character
                 * to point to the state corresponding to this NFA set of states.
                 */
                addTransition(translation[curr], translation[successor], ch);
            }
        }

        return result;
    }

    /* Given an automaton, constructs the reverse of that automaton. */
    NFA reverseOf(const NFA& nfa) {
        /* Copy the automaton. */
        NFA result = nfa;

        /* Record and remove all transitions. */
        struct Transition {
            State* from;
            State* to;
            char32_t ch;
        };
        vector<Transition> transitions;

        for (const auto& state: result.states) {
            for (const auto& transition: state->transitions) {
                transitions.push_back({ state.get(), transition.second, transition.first });
            }
            state->transitions.clear();
        }

        /* Insert them in reverse. */
        for (const auto& transition: transitions) {
            addTransition(transition.to, transition.from, transition.ch);
        }

        /* Change which states are accepting / starting. */
        for (const auto& state: result.states) {
            swap(state->isStart, state->isAccepting);
        }

        return result;
    }

    /* Given any automaton, returns a minimal DFA equivalent to it. */
    DFA minimalDFAFor(const NFA& nfa) {
        /* We use Brzozowki's algorithm, which works as follows:
         *
         * minimal-dfa = S(R(S(R(automatom))))
         *
         * Here, R is reversal (turn all edges around) and S is the subset construction.
         *
         * I know, right? This is really surprising!
         *
         * TODO: For efficiency's sake, it might be a good idea to prune the automaton
         * just before doing a reverse step. This will remove some states that otherwise
         * would be factored into the subset construction.
         */
        auto result = subsetConstruct(reverseOf(subsetConstruct(reverseOf(nfa))));

        /* Just to be nice, rename all the states in some nice fashion. */
        size_t next = 0;
        bfs(startStatesOf(result), [&](State* s) {
            s->name = "q" + to_string(next++);
        });

        return result;
    }

    namespace {
        void makePairState(DFA& dfa,
                           map<pair<State*, State*>, State*>& translation,
                           State* first, State* second) {
            auto state = make_shared<State>();
            state->name = "(" + first->name + ", " + second->name + ")";
            state->isStart     = first->isStart && second->isStart;
            state->isAccepting = first->isAccepting != second->isAccepting;
            dfa.states.insert(state);
            translation[make_pair(first, second)] = state.get();
        }
    }

    /* Given two automata, returns their XOR automata, which accepts everything accepted
     * by only one of the two automata.
     */
    DFA xorConstruct(const DFA& one, const DFA& two) {
        /* Alphabets must match; if not, we're in trouble. */
        if (one.alphabet != two.alphabet) {
            Utilities::error("Alphabet mismatch in XOR construction.");
        }

        DFA result;
        result.alphabet = one.alphabet; // == rhs.alphabet

        /* Run a BFS to explore all pairs of states. */
        map<pair<State*, State*>, State*> translation;
        queue<pair<State*, State*>> worklist;

        /* Find all pairs of start states. */
        for (const auto& first: one.states) {
            if (first->isStart) {
                for (const auto& second: two.states) {
                    if (second->isStart) {
                        makePairState(result, translation, first.get(), second.get());
                        worklist.push(make_pair(first.get(), second.get()));
                    }
                }
            }
        }

        /* Run the search. */
        while (!worklist.empty()) {
            auto curr = worklist.front();
            worklist.pop();

            /* Find all successors. */
            for (char32_t ch: result.alphabet) {
                /* There should be exactly one transition for each character. */
                auto r1 = curr.first->transitions.lower_bound(ch);
                auto r2 = curr.second->transitions.lower_bound(ch);

                if (r1 == curr.first->transitions.end() || r1->first != ch) {
                    Utilities::error("Internal error: Missing transition?");
                }
                if (r2 == curr.second->transitions.end() || r2->first != ch) {
                    Utilities::error("Internal error: Missing transition?");
                }

                /* Here's where we need to go. Have we seen it before? */
                auto dest = make_pair(r1->second, r2->second);
                if (!translation.count(dest)) {
                    makePairState(result, translation, dest.first, dest.second);
                    worklist.push(dest);
                }

                /* Insert the transition. */
                addTransition(translation[curr], translation[make_pair(r1->second, r2->second)], ch);
            }
        }

        return result;
    }

    /* Finds the shortest string accepted by the automaton, or reports that
     * the automaton doesn't accept anything.
     */
    bool shortestStringIn(const NFA& nfa, string& result) {
        NFA dfa = subsetConstruct(nfa);
        queue<State*> worklist;

        /* Predecessor map. */
        unordered_map<State*, pair<char32_t, State*>> predecessors;

        /* Add all start states. */
        for (auto state: startStatesOf(dfa)) {
            worklist.push(state);
            predecessors[state] = make_pair(-1, nullptr);
        }

        /* Run the BFS. */
        while (!worklist.empty()) {
            auto curr = worklist.front();
            worklist.pop();

            /* Found an accepting state? Then we're done! */
            if (curr->isAccepting) {
                /* Track backwards until we hit a start state. */
                result = "";
                while (!curr->isStart) {
                    auto prev =  predecessors[curr];
                    result    += toUTF8(prev.first);
                    curr      =  prev.second;
                }
                reverse(result.begin(), result.end());
                return true;
            }

            /* Expand outward. */
            for (const auto& entry: curr->transitions) {
                if (!predecessors.count(entry.second)) {
                    predecessors[entry.second] = make_pair(entry.first, curr);
                    worklist.push(entry.second);
                }
            }
        }

        /* Oops, didn't find anything. */
        return false;
    }

    /* Checks for equivalence, giving a counterexample if the automata aren't
     * equivalent.
     */
    bool areEquivalent(const DFA& lhs, const DFA& rhs, string& counterexample) {
        return !shortestStringIn(xorConstruct(lhs, rhs), counterexample);
    }
}
