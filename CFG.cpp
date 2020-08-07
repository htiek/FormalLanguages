#include "CFG.h"
#include "Utilities/Unicode.h"
#include <vector>
#include <set>
#include <queue>
#include <sstream>
#include <iostream>
#include <cctype>
#include <map>
#include <functional>
#include <algorithm>
#include <random>
#include <unordered_map>
#include <unordered_set>
using namespace std;

namespace CFG {
    namespace {
        /* Generic fixed-point iteration routine. Many algorithms on CFGs make use of
         * fixed-point iteration on the nonterminals, and this factors that logic out into
         * a separate routine.
         */
        template <typename Result>
        map<char32_t, Result> fixedPointIterate(const CFG& cfg,
                                                function<bool (const Production&, map<char32_t, Result>&)> update) {
            /* Initialize the map. */
            map<char32_t, Result> result;
            for (char32_t nonterminal: cfg.nonterminals) {
                result[nonterminal] = Result();
            }

            /* Repeat until convergence. */
            bool changed;
            do {
                changed = false;
                for (const auto& production: cfg.productions) {
                    changed |= update(production, result);
                }
            } while (changed);


            return result;
        }
    }

    bool operator== (const Symbol& lhs, const Symbol& rhs) {
        return lhs.ch == rhs.ch && lhs.type == rhs.type;
    }

    ostream& operator<< (ostream& out, const Symbol& symbol) {
        ostringstream builder;
        if (symbol.type == Symbol::Type::TERMINAL) builder << toUTF8(symbol.ch);
        else builder << "<" << toUTF8(symbol.ch) << ">";
        return out << builder.str();
    }

    ostream& operator<< (ostream& out, const Production& prod) {
        ostringstream builder;
        builder << "<" << toUTF8(prod.nonterminal) << "> -> ";
        if (prod.replacement.empty()) {
            builder << "ε";
        } else {
            for (auto symbol: prod.replacement) {
                builder << symbol;
            }
        }
        return out << builder.str();
    }

    ostream& operator<< (ostream& out, const Derivation& d) {
        ostringstream builder;
        for (size_t i = 0; i < d.size(); i++) {
            builder << "(" << d[i].first << " @" << d[i].second << ")";
            if (i + 1 != d.size()) builder << ", ";
        }
        return out << builder.str();
    }

    bool operator< (const Symbol& lhs, const Symbol& rhs) {
        if (lhs.type != rhs.type) return int(lhs.type) < int(rhs.type);
        return lhs.ch < rhs.ch;
    }

    bool operator< (const Production& lhs, const Production& rhs) {
        if (lhs.nonterminal != rhs.nonterminal) return lhs.nonterminal < rhs.nonterminal;
        return lhs.replacement < rhs.replacement;
    }

    namespace {
        /* Nullablity information is a map from nullable nonterminals to the production
         * they should use as their first step toward null.
         */
        using Nulls = std::map<char32_t, const Production*>;

        Nulls nullablesOf(const CFG& cfg) {
           Nulls result;

           bool changed;
           do {
               changed = false;

               /* Loop over all productions and see if any of them give us a new way to null. */
               for (const auto& p: cfg.productions) {
                   /* Already nullable? Skip. */
                   if (result.count(p.nonterminal)) continue;

                   /* See if all terms in the production are nullable. */
                   if (all_of(p.replacement.begin(), p.replacement.end(), [&](Symbol s) {
                       return s.type == Symbol::Type::NONTERMINAL && result.count(s.ch);
                   })) {
                       result[p.nonterminal] = &p;
                       changed = true;
                   }
               }

           } while (changed);

           return result;
        }

        string toString(const set<char32_t>& s) {
            ostringstream result;
            result << "{ ";
            for (char32_t ch: s) {
                result << toUTF8(ch) << " ";
            }
            result << "}";
            return result.str();
        }
    }

    ostream& operator<< (ostream& out, const CFG& cfg) {
        out << "Start:        " << toUTF8(cfg.startSymbol) << endl;
        out << "Alphabet:     " << toString(cfg.alphabet) << endl;
        out << "Nonterminals: " << toString(cfg.nonterminals) << endl;
        out << "Productions:" << endl;
        for (const auto& p: cfg.productions) {
            out << "  " << p << endl;
        }
        return out;
    }

    /**************************************************************************
     **************************************************************************
     ***                   Earley Parser Implementation                     ***
     **************************************************************************
     **************************************************************************

     This is the logic for the matcher and deriver. It uses Earley's algorithm,
     a simple general context-free grammar parser that runs in worst-case cubic
     time as a function of the string length. The core Earley algorithm is
     fairly accessible; much of the complexity here is due to the logistics of
     handling epsilon productions.

     The hardest part here is the deriver, which reconstructs a derivation for
     a string in the language. The comments at that section explain how that's
     done. It's essentially a double backtracking search and was way harder to
     write than I thought it was going to be. :-)

     *************************************************************************/

    namespace {
        struct EarleyItem {
            const Production* production; // Use pointers for identity semantics when comparing
            size_t dotPos;  // Where the dot is
            size_t itemPos; // Where this item starts
        };

        /* State of the Earley parse. */
        struct EarleyState {
            /* Earley items per slot. */
            vector<set<EarleyItem>> items;

            /* Nullable nonterminals; used for quick lookup during prediction. */
            Nulls nullable;

            /* Productions by nonterminal; used for quick lookup during prediction. */
            map<char32_t, vector<const Production*>> grammar;
        };

        /* For debugging. */
        ostream& operator<< (ostream& out, const EarleyItem& item) {
            out << "<" << toUTF8(item.production->nonterminal) << "> -> ";
            for (size_t i = 0; i < item.production->replacement.size(); i++) {
                if (item.dotPos == i) cout << ".";
                out << toUTF8(item.production->replacement[i].ch);
            }

            if (item.dotPos == item.production->replacement.size()) {
                out << ".";
            }

            return out << " @" << item.itemPos;
        }

        /* So we can stash them in a std::set. */
        bool operator< (const EarleyItem& lhs, const EarleyItem& rhs) {
            if (lhs.production != rhs.production) {
                /* Legal; will always be drawn from the same vector. */
                return lhs.production < rhs.production;
            } else if (lhs.dotPos != rhs.dotPos) {
                return lhs.dotPos < rhs.dotPos;
            } else {
                return lhs.itemPos < rhs.itemPos;
            }
        }

        /* cctype replacements for char32_t's. */
        bool isASCII(char32_t ch) {
            return 0 <= static_cast<int>(ch) && ch <= 127;
        }
        bool isSpace(char32_t ch) {
            return isASCII(ch) && isspace(static_cast<int>(ch));
        }

        /* Given a string in UTF-8 formatting, breaks it apart into individual characters,
         * represented as strings with UTF-8 formatting.
         */
        vector<char32_t> utf8Decode(const string& input, const Languages::Alphabet& alphabet) {
            vector<char32_t> result;
            for (char32_t ch: utf8Reader(input)) {
                if (isSpace(ch)) continue;
                if (!alphabet.count(ch)) throw runtime_error("Invalid character: " + toUTF8(ch));
                result.push_back(ch);
            }
            return result;
        }

        /* Utility for Earley: is item's dot at end? */
        bool dotAtEnd(const EarleyItem& item) {
            return item.dotPos == item.production->replacement.size();
        }

        /* Utility for Earley: item after the dot. */
        Symbol afterDot(const EarleyItem& item) {
            if (dotAtEnd(item)) abort(); // Logic error!
            return item.production->replacement[item.dotPos];
        }

        /* Utility for Earley: Shifts the dot over. This produces a new item rather than
         * editing the existing one.
         */
        EarleyItem advanceDot(const EarleyItem& item) {
            if (dotAtEnd(item)) abort(); // Logic error!
            auto result = item;
            result.dotPos++;
            return result;
        }

        EarleyItem retreatDot(const EarleyItem& item) {
            if (item.dotPos == 0) abort(); // Logic error!
            auto result = item;
            result.dotPos--;
            return result;
        }

        const bool kParserVerbose = false;

        /* Adds an item to an Earley parser. */
        bool addItem(EarleyState& state, size_t index, const EarleyItem& item) {
            if (index < 0 || index > state.items.size() || item.itemPos > index) abort(); // Logic error!
            return state.items[index].insert(item).second;
        }

        /* "Scan" step of the Earley algorithm. */
        void scan(EarleyState& state, size_t index, char32_t ch) {
            if (kParserVerbose) cout << "SCAN " << index << endl;
            /* See if anything here needs to have the dot shifted. */
            for (const auto& item: state.items[index]) {
                /* Item after the dot matches this character. */
                if (kParserVerbose) cout << "Considering " << item << endl;
                if (!dotAtEnd(item) && afterDot(item) == Symbol{Symbol::Type::TERMINAL, ch}) {
                    if (kParserVerbose) cout << "    Will scan." << endl;
                    /* Shift the dot over. */
                    auto next = advanceDot(item);
                    if (kParserVerbose) cout << "    Result: " << next << endl;
                    addItem(state, index + 1, next);
                }
            }

            if (kParserVerbose) cout << endl;
        }

        /* "Complete" step of the Earley algorithm. Returns whether anything
         * changed in the course of running this process.
         */
        bool complete(EarleyState& state, size_t index) {
            bool result = false;
            if (kParserVerbose) cout << "COMPLETE " << index << endl;

            /* Create a worklist of items to process. */
            queue<EarleyItem> worklist;
            for (const auto& item: state.items[index]) {
                if (kParserVerbose) cout << "  Considering " << item << endl;
                if (dotAtEnd(item)) { // At end
                    if (kParserVerbose) cout << "    Adding to worklist." << endl;
                    worklist.push(item);
                }
            }

            /* Keep processing items until none are left. */
            while (!worklist.empty()) {
                auto item = worklist.front();
                worklist.pop();

                if (kParserVerbose) cout << "  Processing " << item << endl;

                /* Track back to where this item came from and push all dots before
                 * this nonterminal forward.
                 */
                for (const auto& pred: state.items[item.itemPos]) {
                    if (kParserVerbose) cout << "    Possible predecessor: " << pred << endl;
                    /* Dot not at end, and is before this nonterminal. */
                    if (!dotAtEnd(pred) && afterDot(pred) == Symbol{Symbol::Type::NONTERMINAL, item.production->nonterminal}) {
                        if (kParserVerbose) cout << "      Will complete." << endl;
                        /* Shift the dot forward. */
                        auto next = advanceDot(pred);
                        if (kParserVerbose) cout << "      Completes to " << next << endl;

                        /* If we haven't already seen this, and if it's at the end, we need to
                         * process it too.
                         */
                        if (addItem(state, index, next)) {
                            result = true;
                            if (kParserVerbose) cout << "      This is new - do we need to process?" << endl;
                            if (dotAtEnd(next)) {
                                if (kParserVerbose) cout << "        Yes!" << endl;
                                worklist.push(next);
                            }
                            else if (kParserVerbose) cout << "      No" << endl;
                        }
                    }
                }
            }

            if (kParserVerbose) cout << endl;
            return result;
        }

        /* "Predict" step of Earley parser. */
        bool predict(EarleyState& state, size_t index) {
            bool result = false;
            if (kParserVerbose) cout << "PREDICT " << index << endl;

            /* Worklist; we may discover many things. */
            queue<EarleyItem> worklist;

            /* Seed queue with all items with dots before nonterminals. */
            for (const auto& item: state.items[index]) {
                if (kParserVerbose) cout << "  Considering " << item << endl;
                if (!dotAtEnd(item) && afterDot(item).type == Symbol::Type::NONTERMINAL) {
                    if (kParserVerbose) cout << "    Adding to worklist." << endl;
                    worklist.push(item);
                }
            }

            /* Keep processing until done. */
            while (!worklist.empty()) {
                auto item = worklist.front();
                worklist.pop();

                if (dotAtEnd(item) || afterDot(item).type == Symbol::Type::TERMINAL) abort(); // Logic error!

                if (kParserVerbose) cout << "  Processing " << item << endl;

                /* Find all productions we can use here. */
                char32_t nonterminal = afterDot(item).ch;
                for (const auto* prod: state.grammar[nonterminal]) {
                    if (kParserVerbose) cout << "    Considering production " << prod << endl;
                    /* If we haven't yet seen this, we may need to put it into the queue
                     * too for nonterminals at the front.
                     */
                    if (kParserVerbose) cout << "      Match!" << endl;
                    EarleyItem predicted = { prod, 0, index };
                    if (kParserVerbose) cout << "      Produced " << predicted << endl;
                    if (addItem(state, index, predicted)) {
                        result = true;

                        if (kParserVerbose) cout << "      This is new. Process it?" << endl;
                        /* Nonterminal at the front? Add it in. */
                        if (!dotAtEnd(predicted) && afterDot(predicted).type == Symbol::Type::NONTERMINAL) {
                            if (kParserVerbose) cout << "        Yes!" << endl;
                            worklist.push(predicted);
                        }
                        else if (kParserVerbose) cout << "        No" << endl;
                    }
                }
            }
            if (kParserVerbose) cout << endl;

            return result;
        }

        void completeAndPredict(EarleyState& state, size_t index) {
            /* Run predictor and completer until convergence. */
            bool completeUpdate;
            bool predictUpdate;
            do {
                completeUpdate = complete(state, index);
                predictUpdate  = predict(state, index);
            } while (completeUpdate || predictUpdate);
        }

        /* For debugging. */
        void printItems(const EarleyState& state, size_t index) {
            cout << "Items at index " << index << ": " << endl;
            for (const auto& item: state.items.at(index)) {
                cout << "  " << item << endl;
            }
        }

        /* Earley parsing algorithm. We run in a loop of scan/complete/predict, starting
         * at predict, and see if we find the proper Earley item in the last column.
         *
         * We use a slightly different indexing system than the traditional Earley position.
         * Position 0 is just before the first character, and position i+1 is just after the
         * ith character.
         */
        EarleyState earley(char32_t start,
                           const Nulls& nullable,
                           const map<char32_t, vector<const Production*>>& grammar,
                           const vector<char32_t>& input) {
            EarleyState state;
            state.nullable = nullable;
            state.grammar  = grammar;
            state.items.resize(input.size() + 1);

            /* Seed with the initial Earley items. */
            if (kParserVerbose) cout << "SEEDING" << endl;
            for (const auto* prod: state.grammar[start]) {
                if (kParserVerbose) cout << "Including this production." << endl;
                addItem(state, 0, { prod, 0, 0 });
            }

            /* Run the Earley parser. */
            for (size_t i = 0; i <= input.size(); i++) {
                if (kParserVerbose) printItems(state, i);
                completeAndPredict(state, i);
                if (i != input.size()) scan(state, i, input[i]);
            }
            if (kParserVerbose) cout << endl;

            return state;
        }

        /* Given the result of an Earley parse, returns some final item from that
         * parse (or null if one doesn't exist).
         */
        const EarleyItem* acceptingItem(const EarleyState& state, char32_t start) {
            /* See if we match. */
            for (const auto& item: state.items.back()) {
                if (kParserVerbose) cout << "Final item: " << item << endl;
                if (item.production->nonterminal == start &&     // Start symbol
                    item.itemPos == 0 &&                                   // occurs at the beginning
                    item.dotPos == item.production->replacement.size()) {  // is complete
                    if (kParserVerbose) cout << "  Success!" << endl;
                    return &item;
                }
            }
            return nullptr;
        }

        /* Acceptance works by running Earley and seeing if we have any matching items. */
        bool accepts(char32_t start,
                     const Nulls& nullable,
                     const map<char32_t, vector<const Production*>>& grammar,
                     const vector<char32_t>& input) {
            return acceptingItem(earley(start, nullable, grammar, input), start) != nullptr;
        }

        /* Given a nonterminal and a position, creates a sequence of Earley items corresponding
         * to that nonterminal getting replaced by epsilon.
         */
        vector<EarleyItem> nullingSequenceFor(const EarleyState& state,
                                              char32_t nonterminal,
                                              size_t position) {
            if (!state.nullable.count(nonterminal)) abort(); // Logic error!

            /* Seed things with an initial Earley item. */
            vector<EarleyItem> result = { { state.nullable.at(nonterminal), 0, position } };

            /* Keep appending. */
            while (!dotAtEnd(result[0])) {
                auto next = nullingSequenceFor(state, afterDot(result[0]).ch, position);
                result.insert(result.end(), next.begin(), next.end());

                result[0] = advanceDot(result[0]);
            }

            return result;
        }

        const bool kDeriverVerbose = false;

        /* Given the result of an Earley parse, returns a derivation of the input string.
         *
         * The basic idea behind this algorithm is not that complicated. We begin with
         * some Earley item we want to come up with a derivation for, say,
         *
         *    S -> alpha X . beta @n #k
         *
         * Here, the "#k" refers to the fact that the item is in slot k, meaning that
         * this Earley item spans the range [n, k).
         *
         * The basic idea is the following. If X is a terminal, then we know the item
         *
         *    S -> alpha .X beta @n #k-1
         *
         * exists, so we recursively get a derivation for it. Otherwise, X is a nonterminal,
         * so we search for an item of the form
         *
         *    S -> alpha .X beta @n #i
         *
         * where there's also an item
         *
         *    X -> gamma. @i #k
         *
         * and get derivations for these items. Putting those together gives us our derivation.
         *
         * The problem is that grammars can have loops in them, and we need to be extra vigilant
         * about this. In particular, while S -> alpha .X beta @n #i can't loop (we enforce that
         * i < k), the rule X -> gamma. @i #k *can* cause a loop if i = n. This means that, as
         * we're running this algorithm, we need to make sure that we don't get caught in a loop.
         *
         * The specific way we'll do this is the following. We'll name the two items here the
         * "left" and "right" items. The "left" item is the (earlier) item that never can trigger
         * a loop, since we monotonically move to the left. The "right" item can trigger a loop.
         * To ensure that it doesn't, we'll keep track of all the nonterminals that have appeared
         * on the left side of the right production *at the specific position spanned by the current
         * Earley item*. We then ignore all productions that would lead to loops, doing so via a
         * backtracking search.
         *
         * This can also manifest as failures in the LEFT side as well. Those failures occur if we
         * picked an item for the left that works, but requires a self-loop.
         *
         * More generally, this algorithm can fail if the item we're working with could potentially
         * derive the string in question, but only via at least one self-loop.
         *
         * There's one more issue we need to handle, and that's nullable nonterminals. Specifically,
         * we may find that we need a pair
         *
         *     S -> alpha . X beta @n #i
         *     X -> gamma.         @i #k
         *
         * where the left item is at index k. But that can easily trigger loops in the left item, which
         * we want to avoid. To address this, we'll use a shortcut. In the event that we have
         *
         *     S -> alpha X . beta @n #k
         *
         * and X is nullable, then the item
         *
         *     S -> alpha . X beta @n #k
         *
         * may also exist in this spot. If it does, we'll do a backtracking search to see whether this
         * leads us to our goal.
         */
        pair<vector<EarleyItem>, bool>
        derivationOfRec(const EarleyState& state,
                        const EarleyItem& item, size_t position,
                        const set<char32_t>& used,               // Nonterminals that were used in the current range
                        size_t indent = 2) {
            if (kDeriverVerbose) {
                cout << string(indent, ' ') << "Deriving " << item << " #" << position << " with these restrictions:" << endl;
                cout << string(indent, ' ') << "{ ";
                for (char32_t ch: used) {
                    cout << toUTF8(ch) << " ";
                }
                cout << "}" << endl;
            }

            /* Base case: If the dot is at the front of the production, nothing more
             * is required.
             */
            if (item.dotPos == 0) return {{}, true};

            auto prev = retreatDot(item);

            /* Recursive case 1: Dot before a terminal means we shift it back. */
            if (afterDot(prev).type == Symbol::Type::TERMINAL) {
                if (kDeriverVerbose) cout << string(indent, ' ') << "Terminal shift." << endl;

                /* Position has changed. Reset the 'used' set because we didn't hit any loops
                 * at this interval.
                 */
                return derivationOfRec(state, prev, position - 1, { }, indent + 4);
            }

            /* Recursive case 2: Dot before a nullable nonterminal. If we can, shift the
             * dot back one position.
             */
            auto nonterminal = afterDot(prev).ch;
            if (state.nullable.count(nonterminal) && state.items[position].count(prev)) {
                if (kDeriverVerbose) cout << string(indent, ' ') << "Nullable shift." << endl;

                /* Position has NOT changed; do not reset 'used.' */
                auto result = derivationOfRec(state, prev, position, used, indent + 4);
                if (result.second) {
                    auto nulling = nullingSequenceFor(state, nonterminal, position);
                    result.first.insert(result.first.end(), nulling.begin(), nulling.end());
                    return result;
                }

                if (kDeriverVerbose) cout << string(indent, ' ') << "Nullable shift failed." << endl;
            }

            /* Recursive case 3: Dot before a nonterminal. Given that we have
             *
             *     S -> alpha X . beta @n #k
             *
             * we want to search for a pair
             *
             *     S -> alpha . X beta @n #i
             *     X -> gamma.         @i #k
             *
             * We need to be careful here not to allow for loops; there are several
             * cases where X -> gamma. @i #k could result in a loop here!
             *
             * We search all item slots *before* this one because epsilons are handled
             * in the above step.
             */
            for (size_t i = 0; i < position; i++) {
                /* See if the retreated dot item is here. */
                if (kDeriverVerbose) cout << string(indent, ' ') << "Index " << i << endl;

                if (state.items[i].count(prev)) {
                    if (kDeriverVerbose) cout << string(indent, ' ') << "Found " << prev << " in slot " << i << endl;
                    if (kDeriverVerbose) cout << string(indent, ' ') << "Searching for production <" << toUTF8(nonterminal) << "> -> alpha. @" << i << " #" << position << endl;
                    /* See if we can find a completed item for the nonterminal in the
                     * current slot.
                     */
                    for (const auto& next: state.items[position]) {
                        if (!dotAtEnd(next) || next.production->nonterminal != nonterminal || next.itemPos != i) {
                            if (kDeriverVerbose) cout << string(indent, ' ') << "  Wrong position; needed " << i << endl;
                            continue;
                        }
                        if (kDeriverVerbose) cout << string(indent, ' ') << "Considering " << next << " #" << position << endl;
                        if (next.itemPos == item.itemPos && used.count(next.production->nonterminal)) {
                            if (kDeriverVerbose) cout << string(indent, ' ') << "  Already used this nonterminal here." << endl;
                            continue;
                        }

                        if (kDeriverVerbose) cout << string(indent, ' ') << "Splitting into " << prev << " #" << i << " and " << next << " #" << position << endl;

                        /* We now have our pair. See whether we can derive the right. */
                        pair<vector<EarleyItem>, bool> rhs;

                        /* If this item has the same range as the current one, blacklist the
                         * nonterminal involved.
                         */
                        if (next.itemPos == item.itemPos) {
                            auto nextUsed = used;
                            nextUsed.insert(next.production->nonterminal);
                            rhs = derivationOfRec(state, next, position, nextUsed, indent + 4);
                        }
                        /* Otherwise, we've made progress, so we can reset the used set. */
                        else {
                            rhs = derivationOfRec(state, next, position, {}, indent + 4);
                        }

                        /* If this failed, move on. */
                        if (!rhs.second) {
                            if (kDeriverVerbose) cout << string(indent, ' ') << "Loop detected; fail." << endl;
                            continue;
                        }

                        /* Right side worked, which is great! Now get the left side. */
                        auto lhs = derivationOfRec(state, prev, i, {}, indent + 4);

                        /* This could also fail if we picked an LHS item that requires us
                         * to use a self-loop. So if that happens, skip it and move on.
                         */
                        if (!lhs.second) {
                            if (kDeriverVerbose) cout << string(indent, ' ') << "Loop detected; fail." << endl;
                            continue;
                        }

                        vector<EarleyItem> result;
                        result.insert(result.end(), lhs.first.begin(), lhs.first.end());
                        result.push_back(next);
                        result.insert(result.end(), rhs.first.begin(), rhs.first.end());

                        return { result, true };
                    }
                }
            }

            if (kDeriverVerbose) cout << string(indent, ' ') << "FAILURE; backtracking..." << endl;
            return { {}, false };
        }

        Derivation derivationOf(char32_t start,
                                const Nulls& nullable,
                                const map<char32_t, vector<const Production*>>& grammar,
                                const vector<char32_t>& input) {
            auto state = earley(start, nullable, grammar, input);

            /* Try all possible derivations from the end and see if any of them work. */
            for (const auto& item: state.items.back()) {
                if (kDeriverVerbose) cout << "Inspecting item " << item << endl;
                if (dotAtEnd(item) && item.itemPos == 0 && item.production->nonterminal == start) {
                    /* See if we can find a derivation here. */
                    auto derivation = derivationOfRec(state, item, input.size(), { item.production->nonterminal });
                    if (!derivation.second) continue;

                    /* Fencepost issue; this first one isn't added in. */
                    derivation.first.insert(derivation.first.begin(), item);

                    /* Translate from a list of Earley items into what we actually want. */
                    Derivation result;
                    for (const auto& item: derivation.first) {
                        result.push_back({ *item.production, item.itemPos });
                    }
                    return result;
                }
            }

            return {};
        }

        /* Given a CFG, converts it to a format usable in Earley parsing. */
        map<char32_t, vector<const Production*>> toEarleyGrammar(const CFG& cfg) {
            map<char32_t, vector<const Production*>> result;
            for (const auto& prod: cfg.productions) {
                result[prod.nonterminal].push_back(&prod);
            }
            return result;
        }

        Matcher earleyMatcherFor(const CFG& cfg) {
            /* Clone the grammar locally so that internal pointers stay valid. */
            auto grammarRef = make_shared<CFG>(cfg);
            auto nullable   = nullablesOf(*grammarRef);
            auto grammar    = toEarleyGrammar(*grammarRef);

            return [=](const string& input) {
                return accepts(grammarRef->startSymbol, nullable, grammar, utf8Decode(input, grammarRef->alphabet));
            };
        }
    }

    Deriver deriverFor(const CFG& cfg) {
        /* Clone the grammar locally so that internal pointers stay valid. */
        auto grammarRef = make_shared<CFG>(cfg);
        auto nullable   = nullablesOf(*grammarRef);
        auto grammar    = toEarleyGrammar(*grammarRef);

        return [=](const string& input) {
            return derivationOf(grammarRef->startSymbol, nullable, grammar, utf8Decode(input, grammarRef->alphabet));
        };
    }

    /**************************************************************************
     **************************************************************************
     ***               McKenzie Generator Implementation                    ***
     **************************************************************************
     **************************************************************************

     The logic below is for the algorithm to generate random strings from a CFG
     using McKenzie's algorithm.

     Routines to sample strings from a CFG. I am indebted to former CS103 TA and
     all-around superstar Kevin Gibbons for sourcing the paper "Generating
     Strings at Random from a Context-Free Grammar" by Bruce McKenzie, for
     spotting typos in the paper and correcting them, and for working out that
     it's necessary to eliminate epsilon productions and self-loops in the
     grammar before first running this algorithm.

     *************************************************************************/

    namespace {
        /* Type representing the 5D (!) Tail table.
         *
         * Nonterminal x Length x Production x Start -> [ numbers ]
         */
        using TailTable = map<char32_t, map<size_t, map<const Production*, map<size_t, vector<size_t>>>>>;

        /* Representation of a grammar, optimized for fast lookups of productions for a nonterminal. */
        using McKenzieGrammar = map<char32_t, vector<Production>>;

        struct McKenzieGenerator {
            McKenzieGenerator(const CFG& cfg);
            pair<bool, string> operator()(size_t) const;

            /* Cleaned grammar; used for generation. It's stored this way because
             * we need the ability to index productions by number.
             */
            McKenzieGrammar grammar;

            /* Start symbol. */
            char32_t start;

            /* Stored tail table (see below). This is mutable because it can be populated on demand. */
            mutable TailTable tail;

            /* Grammar can generate empty string. Is that the case? */
            bool hasEpsilon;
        };

        namespace {
            /* The random number generator. */
            mt19937 theGenerator;

            /* Given a production, produces all nonempty productions that can be formed by
             * taking subsets of the nullable nonterminals.
             */
            void generateSubsetsOf(const Production& p,
                                   const Nulls& nullable,
                                   set<Production>& result) {
                std::function<void(Production&, size_t)> rec =
                [&](Production& soFar, size_t index) {
                    /* If at end, add unless we're empty. */
                    if (index == p.replacement.size()) {
                        if (!soFar.replacement.empty()) result.insert(soFar);
                        return;
                    }
                    /* Include next character. */
                    soFar.replacement.push_back(p.replacement[index]);
                    rec(soFar, index+1);
                    soFar.replacement.pop_back();

                    /* If next is nullable, skip it. */
                    if (p.replacement[index].type == Symbol::Type::NONTERMINAL &&
                        nullable.count(p.replacement[index].ch)) {
                        rec(soFar, index + 1);
                    }
                };

                Production builder;
                builder.nonterminal = p.nonterminal;
                rec(builder, 0);
            }

            /* Given a CFG, returns the epsilon normal form of that CFG. This is formed by
             * replacing all epsilon productions with new productions by "dropping out"
             * nonterminals that are nullable in all (nonempty) combinations.
             *
             * TODO: This is misleading because it doesn't add epsilon back in at the end
             * if the start symbol is nullable. Be careful!
             */
            CFG epsilonNormalFormOf(const CFG& cfg, const Nulls& nullable) {
                set<Production> newProds;

                for (const auto& prod: cfg.productions) {
                    generateSubsetsOf(prod, nullable, newProds);
                }

                auto result = cfg;
                result.productions.assign(newProds.begin(), newProds.end());
                return result;
            }

            CFG epsilonNormalFormOf(const CFG& cfg) {
                return epsilonNormalFormOf(cfg, nullablesOf(cfg));
            }

            /* Given a CFG in epsilon normal form, returns a new CFG such that if there any
             * unit productions, they form a DAG.
             *
             * A unit production is one of the form A -> B. The basic idea is to replace all
             * productions of the form A -> B by finding productions of the form B -> alpha
             * and then directly writing A -> alpha, replacing the A -> B production.
             *
             * The challenge here is that we can have chains of nonterminals, such as
             * A -> B -> C, where A needs to absorb items out of C.
             *
             * Making this more complicated, we also have to worry about chains of
             * the form A -> B -> A, in which we get a cycle from a nonterminal back to
             * itself. In that case, A needs to take on B's productions, and B needs to take
             * on A's productions as well. (This includes a special case of A -> A, nonterminals
             * that immediately produce themselves.)
             *
             * The most general version of this problem goes like this: we have a graph where
             * each node is a nonterminal and there's an edge from A to B if A -> B is a unit
             * production. We need to duplicate productions while also respecting cycles.
             *
             * Our approach is to use a strongly-connected-components algorithm to find SCC's of
             * nonterminals that can produce one another. We'll then process them in reverse
             * topological order (deepest first, topmost last). We'll then pick one representative
             * of each of the nonterminals in an SCC and combine all the productions together
             * under that representative.
             *
             * The resulting graph may still have unit productions in it, but those units will
             * form a DAG, which is all we need.
             */
            using Graph = map<char32_t, set<char32_t>>;

            /* Returns whether something is a unit production. */
            bool isNonterminalUnit(const Production& p) {
                return p.replacement.size() == 1 && p.replacement[0].type == Symbol::Type::NONTERMINAL;
            }
            bool isTerminalUnit(const Production& p) {
                return p.replacement.size() == 1 && p.replacement[0].type == Symbol::Type::TERMINAL;
            }

            /* Constructs the graph where A -> B is an edge if A -> B is a production. */
            Graph unitGraphOf(const CFG& cfg) {
                Graph result;

                /* Ensure a node for each nonterminal. */
                for (char32_t nonterminal: cfg.nonterminals) {
                    (void) result[nonterminal];
                }

                for (const auto& p: cfg.productions) {
                    if (isNonterminalUnit(p)) {
                        /* Insert both nodes and an edge one way. */
                        result[p.nonterminal].insert(p.replacement[0].ch);
                    }
                }
                return result;
            }

            /* Runs a DFS, recording the finishing times in the output vector. */
            void dfs(char32_t nonterminal, const Graph& graph, vector<char32_t>& order, set<char32_t>& visited) {
                if (!visited.insert(nonterminal).second) return;

                for (char32_t next: graph.at(nonterminal)) {
                    dfs(next, graph, order, visited);
                }

                order.push_back(nonterminal);
            }

            /* Given a graph, reverses that graph. */
            Graph reverseOf(const Graph& input) {
                Graph result;
                for (const auto& src: input) {
                    /* Clone nodes. */
                    (void) result[src.first];

                    for (char32_t dst: src.second) {
                        result[dst].insert(src.first);
                    }
                }
                return result;
            }

            /* Returns the SCCs of a given graph in reverse topological order. This uses Kosaraju's
             * SCC algorithm.
             */
            vector<vector<char32_t>> sccsOf(const Graph& graph) {
                /* Work with the reverse of the graph so that we find SCCs in reverse topological order. */
                Graph rev = reverseOf(graph);

                /* Run the inital DFS. */
                vector<char32_t> order;
                set<char32_t> visited;
                for (const auto& entry: rev) {
                    dfs(entry.first, rev, order, visited);
                }

                /* Run the reverse DFS's. */
                vector<vector<char32_t>> result;

                reverse(order.begin(), order.end());
                visited.clear();
                for (char32_t nonterminal: order) {
                    vector<char32_t> scc;
                    dfs(nonterminal, graph, scc, visited);

                    /* SCC can be empty if the node was already visited. */
                    if (!scc.empty()) result.push_back(std::move(scc));
                }

                return result;
            }


            CFG unitNormalForm(const CFG& cfg) {
                /* Map from each nonterminal to its representative. */
                map<char32_t, char32_t> reps;

                /* Loop over SCCs, combining productions together. */
                for (const auto& scc: sccsOf(unitGraphOf(cfg))) {
                    /* The first becomes the representative for all of these. */
                    auto rep = scc.front();
                    for (char32_t nonterminal: scc) {
                        reps[nonterminal] = rep;
                    }
                }

                /* Rebuild the productions list. */
                set<Production> productions;
                for (const auto& p: cfg.productions) {
                    /* Replace all nonterminals with their representatives. */
                    auto newP = p;
                    newP.nonterminal = reps.at(newP.nonterminal);
                    for (auto& s: newP.replacement) {
                        if (s.type == Symbol::Type::NONTERMINAL) {
                            s.ch = reps.at(s.ch);
                        }
                    }

                    /* If this is a unit, include it if it's not a loop. */
                    if (!isNonterminalUnit(newP) || newP.replacement[0].ch != newP.nonterminal) {
                        productions.insert(newP);
                    }
                }

                /* Form the new grammar. */
                auto result = cfg;

                /* Nonterminals are the reps. */
                result.nonterminals.clear();
                for (const auto& entry: reps) {
                    result.nonterminals.insert(entry.second);
                }

                /* Change start symbol to rep. */
                result.startSymbol = reps.at(result.startSymbol);

                /* Use new productions. */
                result.productions.assign(productions.begin(), productions.end());

                return result;
            }

            /* Given a CFG, returns its "useful subset." This consists of the subset of
             * the grammar that is (1) reachable from the start symbol and (2) includes
             * only nonterminals that can produce something.
             *
             * This can be thought of as two fixed-point equations. The first one works
             * by identifying nonterminals that can produce nonterminal-free strings
             * and then propagating that information upward. Anything not reached this
             * way can be removed.
             *
             * The second identifies nonterminals reachable from the start symbol. The
             * start symbol is reachable from itself, as is everything transitively
             * derived from there.
             *
             * At this point, everything that remains must be productive and reachable. Why?
             * Everything is definitely reachable. So suppose something is not productive.
             * At the end of the productivity search, it was productive, so this means that
             * something must have been removed that makes it not productive. But the only
             * things removed were ones that weren't reachable, and everything in a production
             * that stems from this nonterminal is reachable, so nothing could be removed.
             *
             * This procedure may produce a CFG with no productions and no nonterminals.
             * If that happens, the language is empty.
             */

            CFG removeNonproductive(const CFG& cfg) {
                bool changed;
                set<char32_t> productive;

                /* Fixed-point iteration. */
                do {
                    changed = false;
                    for (const Production& p: cfg.productions) {
                        /* Skip things we already know are productive. */
                        if (productive.count(p.nonterminal)) continue;

                        /* If production has only productive nonterminals, it's productive. */
                        if (all_of(p.replacement.begin(), p.replacement.end(), [&](const Symbol& s) {
                            return s.type == Symbol::Type::TERMINAL || productive.count(s.ch);
                        })) {
                            productive.insert(p.nonterminal);
                            changed = true;
                        }
                    }

                } while (changed);

                /* Remove these nonterminals. */
                auto result = cfg;
                result.nonterminals = productive;

                result.productions.erase(remove_if(result.productions.begin(), result.productions.end(), [&](const Production& p) {
                    return any_of(p.replacement.begin(), p.replacement.end(), [&](const Symbol& s) {
                        return s.type == Symbol::Type::NONTERMINAL && !productive.count(s.ch);
                    });
                }), result.productions.end());

                return result;
            }

            /* Cute little BFS. */
            CFG removeUnreachable(const CFG& cfg) {
                set<char32_t> reachable = { cfg.startSymbol };
                queue<char32_t> worklist;
                worklist.push(cfg.startSymbol);

                /* Discover everything reachable. */
                while (!worklist.empty()) {
                    auto curr = worklist.front();
                    worklist.pop();

                    for (const auto& p: cfg.productions) {
                        if (p.nonterminal == curr) {
                            for (const auto& s: p.replacement) {
                                if (s.type == Symbol::Type::NONTERMINAL && !reachable.count(s.ch)) {
                                    reachable.insert(s.ch);
                                    worklist.push(s.ch);
                                }
                            }
                        }
                    }
                }

                /* Nuke everything not here. */
                auto result = cfg;
                result.nonterminals = reachable;
                result.productions.erase(remove_if(result.productions.begin(), result.productions.end(), [&](const Production& p) {
                    return !reachable.count(p.nonterminal);
                }), result.productions.end());

                return result;
            }

            /* Given a CFG, returns a "cleaned" version of the CFG in which each
             * nonterminal both produces and can be produced. This eliminates all
             * nonterminals and rules that have no chance of being used.
             */
            CFG clean(const CFG& cfg) {
                return removeUnreachable(removeNonproductive(cfg));
            }

            /* Given a CFG, prepares that CFG for use in the McKenzie generator.
             * This puts the grammar into epsilon normal form - which removes epsilon
             * from the list of productions - removes useless rules, then eliminates
             * cycles by putting the grammar into unit normal form.
             *
             * We have to begin by putting things into epsilon normal form before we
             * start cleaning things. Otherwise, we risk that the grammar ends up
             * with useless productions. Here's an example. Consider this grammar:
             *
             *    S -> A
             *    A -> B
             *    B -> epsilon
             *
             * If we go to epsilon normal form, we'll remove the production B -> epsilon,
             * leaving both (1) a transition from A to B that can't expand and (2) an
             * official nonterminal B with no productions, which can break things later
             * on. We therefore have to do that first, before we start wiping things out.
             */
            CFG mcKenziePrepare(const CFG& input, const Nulls& nullable) {
                return unitNormalForm(clean(epsilonNormalFormOf(input, nullable)));
            }

            size_t sumOf(const vector<size_t>& input) {
                return accumulate(input.begin(), input.end(), 0);
            }

            /* Computes the table from the McKenzie paper.
             *
             * The basic idea behind this table is the following. We want to produce information
             * about each nonterminal in a grammar that tells us how many strings of a given length
             * it can produce (including multiplicity with ambiguity; remember, the problem of
             * determining whether a grammar is ambiguous is undecidable). We assume there are no
             * loops in the grammar and that there are no epsilon productions.
             *
             * So with that in mind, how many strings of length n can a given nonterminal S produce?
             * Well, that would be
             *
             *      ---
             *      \
             *      /    # strings of length n P can produce.
             *      ---
             *   production
             *  P = S -> alpha
             *
             * So that would tell us to start looking at the number of strings of length n that
             * one specific production P could produce.
             *
             * Suppose that we have the production S -> X alpha. There are two cases to consider:
             *
             *   Case 1: X is a terminal. In that case, the number of strings of length n that
             *           this production can produce is equal to the number of strings of length
             *           n-1 that alpha produces.
             *   Case 2: X is a nonterminal. In that case, the number of strings of length n that
             *           this production can produce is equal to the number of strings of length
             *           1 that X produces times the number of strings of length n-1 that alpha
             *           produces, plus the number of strings of length 2 that X produces plus the
             *           number of strings of length n-2 that alpha produces, etc.
             *
             * This gives us a nice recursive formulation for the problem. All of our subproblems
             * have the following form:
             *
             *    Given a nonterminal S, a production S -> alpha, a decomposition of alpha
             *    into alpha = gamma beta, and a length n, how many strings of length exactly
             *    n can beta produce?
             *
             * It turns out that we need a slightly more general version of this problem.
             * Specifically, we'll use this version:
             *
             *    Given a nonterminal S, a production S -> alpha, a decomposition of alpha into
             *    alpha = gamma X beta, and a length n, produce a list of entries such that the
             *    kth entry is the number of strings that X beta produces given that X produced
             *    a string of length exactly k.
             *
             * The terminology in the McKenzie paper is a bit dense, so we'll use something a
             * bit simpler. Every subproblem here can be parameterized in terms of the following:
             *
             *    1. S: Which nonterminal are you using?
             *    2. n: What is the length you want to produce?
             *    3. P: Which production of that nonterminal are you interested in?
             *    4. i: What index in that production does the symbol X occur at?
             *
             * We can encode this as a table Tail[S][n][P][i]. The answer to the question "how many
             * strings of length n can nonterminal S produce?," which we'll denote by Count[S][n], is
             * then
             *
             *                +-      ---          ---                                  -+
             *                |       \            \                       |             |
             *                |       /            /     Tail[S][k][P][1]  |             |
             * Count[S][n] =  |       ---          ---                     | 0 <= k < n  |
             *                |    production                              |             |
             *                |  P = S -> alpha                            |             |
             *                +-                                                        -+
             *
             * The outer sum, hopefully, make sense - we're summing up over all the possible productions
             * of the nonterminal. The value of Tail[S][n][P][1] is a vector saying "if the first symbol of
             * the production produces exactly one character, how many strings of length i can we make? then,
             * if first symbol of the production produces exactly two characters, how many strings of length
             * k can we make? etc.", so the inner sum essentially evaluates to "how many strings of length
             * k can this production make?"
             */

            const bool kGeneratorVerbose = false;

            /* For debugging. */
            string toString(const vector<size_t>& v) {
                ostringstream builder;
                builder << "[ ";
                for (auto i: v) {
                    builder << i << " ";
                }
                builder << "]";
                return builder.str();
            }

            /* Computes and returns Count[S][n]. */
            vector<size_t> computeCount(TailTable& table, const McKenzieGrammar& grammar,
                                        char32_t nonterminal, size_t n, size_t indent = 2);

            /* Computes and returns Tail[S][n][p][i]. */
            const vector<size_t>& computeTail(TailTable& table,
                                              const McKenzieGrammar& grammar,
                                              char32_t nonterminal, size_t n,
                                              const Production* prod, size_t index,
                                              size_t indent = 2) {
                /* Base case: Already known? Then just return it. */
                auto& result = table[nonterminal][n][prod][index];
                if (!result.empty()) return result;


                /* Current production; just for simplicity. */
                const auto& p = prod->replacement;

                if (kGeneratorVerbose) cout << string(indent, ' ') << "Tail[" << toUTF8(nonterminal) << "][" << n << "][" << *prod << "][" << index << "]" << endl;


                /* Base case: n = 0? That means we're asked to make no characters. */
                if (n == 0) {
                    /* If we're at the end of the production, there's one way to do this (do nothing
                     * at all). If we aren't, then there is no way to do this.
                     */
                    result = { index == p.size() };
                    if (kGeneratorVerbose) cout << string(indent, ' ') << toString(result) << endl;
                    return result;
                }

                /* Base case: past the end? Then we can't do this. */
                if (index == p.size()) {
                    result = { 0 };
                    if (kGeneratorVerbose) cout << string(indent, ' ') << toString(result) << endl;
                    return result;
                }

                /* If we're a terminal, the answer is "zero if you want make to make zero characters,
                 * whatever the rest produces if you want me to make one character, and zero for all
                 * other options."
                 *
                 * There's a bit of special-case handling here in that the generator routine isn't going
                 * to look at the indices, so we don't need to include them here.
                 */
                if (p[index].type == Symbol::Type::TERMINAL) {
                    result = { sumOf(computeTail(table, grammar, nonterminal, n - 1, prod, index + 1)) };
                    if (kGeneratorVerbose) cout << string(indent, ' ') << toString(result) << endl;
                    return result;
                }

                /* Otherwise, we're a nonterminal. We're going to try to split the n characters across
                 * this nonterminal and the tail of this production in all possible ways.
                 *
                 * Because each nonterminal produces at least one character, the loop bounds here
                 * start at 1 (munching the first character) and stop when there's one character
                 * left for each of the remaining symbols in the production. THIS IS CRITICAL TO
                 * KEEPING THE RECURSION CORRECT! If we don't ensure there's a drop in n before
                 * making these recursive calls, we could run into loops on
                 * productions like S -> Sa.
                 *
                 * This necessitates some special-case handling for when we're at the very end of
                 * a production with a nonterminal, since in that case the only option is to have
                 * the nonterminal produce everything. This can't loop because the grammar has
                 * no loops in it.
                 *
                 * It's weird that we construct the list to have just one item, and I definitely
                 * had a double-take when reading the paper on this. Turns out the generation
                 * routine special-cases the last nonterminal and doesn't use the indices the
                 * same way the other ones are used, so it's okay that this is technically in
                 * the slot for zero.
                 */
                if (index + 1 == p.size()) {
                    result = { computeCount(table, grammar, p[index].ch, n) };
                    if (kGeneratorVerbose) cout << string(indent, ' ') << toString(result) << endl;
                    return result;
                }

                result.push_back(0); // No ways to make the empty string.
                for (size_t k = 1; k + p.size() - index - 1 <= n; k++) {
                    size_t me   = sumOf(computeCount(table, grammar, p[index].ch, k, indent + 4));
                    size_t them = sumOf(computeTail(table, grammar, nonterminal, n - k, prod, index + 1, indent + 4));
                    result.push_back(me * them);
                }

                if (kGeneratorVerbose) cout << string(indent, ' ') << toString(result) << endl;
                return result;
            }

            vector<size_t> computeCount(TailTable& table, const McKenzieGrammar& grammar,
                                        char32_t nonterminal, size_t n, size_t indent) {
                if (kGeneratorVerbose) cout << string(indent, ' ') << "C[" << toUTF8(nonterminal) << "][" << n << "]" << endl;

                /* Add up all the tail values. */
                vector<size_t> result;
                for (const auto& prod: grammar.at(nonterminal)) {
                    result.push_back(sumOf(computeTail(table, grammar, nonterminal, n, &prod, 0, indent + 4)));
                }
                if (kGeneratorVerbose) cout << string(indent, ' ') << toString(result) << endl;
                return result;
            }

            /* Procedure to generate a random string of the given length with uniform (modulo ambiguity)
             * probability. The basic idea is the following:
             *
             * 1. Select a production. The weight assigned to each production should be the number of
             *    strings it can derive of the given length.
             * 2. Go through the production one symbol at a time. When you see a nonterminal, generate
             *    it randomly. When you see a terminal, generate it.
             */

            pair<bool, string> generateNonterminal(TailTable& table,
                                                   const McKenzieGrammar& grammar,
                                                   char32_t nonterminal,
                                                   size_t n);

            pair<bool, string> generateProduction(TailTable& table,
                                                  const McKenzieGrammar& grammar,
                                                  char32_t nonterminal,
                                                  size_t n,
                                                  const Production* prod,
                                                  size_t index) {
                /* For convenience. */
                auto& p = prod->replacement;

                /* Base case: last character? */
                if (index + 1 == p.size()) {
                    /* Terminal? Just return it. */
                    if (p[index].type == Symbol::Type::TERMINAL) {
                        if (n != 1) abort(); // Logic error!
                        return make_pair(true, toUTF8(p[index].ch));
                    }
                    /* Nonterminal? Generate it. */
                    return generateNonterminal(table, grammar, p[index].ch, n);
                }

                /* Otherwise, there's more after us. */
                if (p[index].type == Symbol::Type::TERMINAL) {
                    auto result = generateProduction(table, grammar, nonterminal, n - 1, prod, index + 1);
                    result.second = toUTF8(p[index].ch) + result.second;
                    return result;
                }

                /* Select how many characters to read. */
                auto& options = table[nonterminal][n][prod][index];
                size_t k = discrete_distribution<size_t>(options.begin(), options.end())(theGenerator);

                auto lhs = generateNonterminal(table, grammar, p[index].ch, k);
                auto rhs = generateProduction(table, grammar, nonterminal, n - k, prod, index + 1);
                return make_pair(lhs.first && rhs.first, lhs.second + rhs.second);
            }

            pair<bool, string> generateNonterminal(TailTable& table,
                                                   const McKenzieGrammar& grammar,
                                                   char32_t nonterminal,
                                                   size_t n) {
                /* Get the relative frequencies of each production. */
                vector<size_t> weights;
                for (const auto& p: grammar.at(nonterminal)) {
                    /* Number of strings of this length is given by
                     * sum(Tail[nonterminal][n][p][0]) because that counts the number of
                     * strings of length n we can make using all characters of the
                     * production.
                     */
                    weights.push_back(sumOf(computeTail(table, grammar, nonterminal, n, &p, 0)));
                }

                /* If we can't make anything of this length, report an error. */
                if (all_of(weights.begin(), weights.end(), [](size_t val) {
                    return val == 0;
                })) {
                    return make_pair(false, "");
                }

                /* Select a production, then generate it. */
                auto* prod = &grammar.at(nonterminal)[discrete_distribution<size_t>(weights.begin(), weights.end())(theGenerator)];
                return generateProduction(table, grammar, nonterminal, n, prod, 0);
            }
        }

        McKenzieGenerator::McKenzieGenerator(const CFG& cfg) {
            auto nullable = nullablesOf(cfg);
            CFG g = mcKenziePrepare(cfg, nullable);

            /* Remember the start symbol. */
            start = g.startSymbol;

            /* Build the productions table. */
            for (const auto& p: g.productions) {
                grammar[p.nonterminal].push_back(p);
            }

            /* We can produce epsilon if the start symbol is nullable. */
            hasEpsilon = nullable.count(cfg.startSymbol);
        }

        pair<bool, string> McKenzieGenerator::operator()(size_t n) const {
            /* Edge case: If the grammar is empty, return nothing. */
            if (grammar.empty()) return make_pair(false, "");

            /* Edge case: If the length is zero, return epsilon iff the grammar
             * can produce epsilon.
             */
            if (n == 0) return make_pair(hasEpsilon, "");

            /* Otherwise, use the generator. */
            return generateNonterminal(tail, grammar, start, n);
        }
    }

    Generator generatorFor(const CFG& cfg) {
        return McKenzieGenerator(cfg);
    }

    /**************************************************************************
     **************************************************************************
     ***                 Chomsky Normal Form Conversion                     ***
     **************************************************************************
     **************************************************************************

     The logic below is for the algorithm to convert a grammar to Chomsky
     Normal Form. This allows for use of the (faster) CYK parser compared with
     the (slower but more general) Earley parser. (It's also just a nice
     subroutine to have lying around for whenever you need it!)

     The conversion to CNF follows this procedure:

       1. Add a new start symbol.
       2. Add nonterminals to rewrite all non-unit terminals as nonterminals.
       3. Break long productions apart into binary productions.
       4. Convert to epsilon normal form.
       5. Convert to strong unit normal form.

     *************************************************************************/
    namespace {
        /* Given a grammar, constructs a new grammar with a new start symbol appearing
         * nowhere else in the grammar. This new start symbol will not necessarily have
         * a Nice or Pretty Name.
         */
        CFG addUniqueStartTo(const CFG& cfg) {
            CFG result = cfg;

            /* Find a character we haven't used. */
            char32_t ch;
            for (ch = 'A'; cfg.nonterminals.count(ch); ch++) {
                // Intentionally empty
            }

            result.productions.push_back( { ch, { Symbol{Symbol::Type::NONTERMINAL, cfg.startSymbol }} });
            result.startSymbol = ch;
            result.nonterminals.insert(ch);

            return result;
        }

        /* Given a grammar, restructures the grammar so that each production that isn't a
         * terminal unit (one char on RHS, which is a terminal) has no terminals in it. This
         * is done by introducing new nonterminals as needed with productions that produce
         * units.
         */
        CFG indirectTerminals(const CFG& cfg) {
            CFG result = cfg;

            /* Next nonterminal name to try. */
            char32_t next = 'A';

            /* Map from terminals to their unit nonterminal. */
            map<char32_t, char32_t> replacements;

            /* Rebuild the productoins. */
            result.productions.clear();
            for (auto prod: cfg.productions) { // By copy, not by reference
                if (!isTerminalUnit(prod)) {
                    /* For convenience. */
                    auto& p = prod.replacement;

                    /* Replace each terminal with a nonterminal. */
                    for (Symbol& s: p) {
                        if (s.type == Symbol::Type::TERMINAL) {
                            if (!replacements.count(s.ch)) {
                                /* Scoot to next available nonterminal. */
                                while (result.nonterminals.count(next)) next++;

                                /* Introduce A -> a as a production. */
                                result.productions.push_back({ next, { s } });
                                result.nonterminals.insert(next);

                                replacements[s.ch] = next;
                            }
                            s = { Symbol::Type::NONTERMINAL, replacements[s.ch] };
                        }
                    }
                }

                result.productions.push_back(prod);
            }

            return result;
        }

        /* Rewrites productions so that all RHS's are at most two characters long. */
        CFG binarize(const CFG& cfg) {
            CFG result = cfg;

            /* Next nonterminal name to try. */
            char32_t next = 'A';

            /* Start rewriting productions! */
            result.productions.clear();
            for (auto prod: cfg.productions) { // Copy, not reference
                auto& p = prod.replacement;
                while (p.size() > 2) {
                    /* Peel off the last two characters into their own production. */
                    auto last2 = p.back(); p.pop_back();
                    auto last1 = p.back(); p.pop_back();

                    /* Need a new nonterminal. */
                    while (result.nonterminals.count(next)) next++;

                    /* Introduce A -> BC. */
                    result.productions.push_back({ next, { last1, last2 }});
                    result.nonterminals.insert(next);

                    /* Replace BC with A in the original production. */
                    p.push_back({ Symbol::Type::NONTERMINAL, next });
                }
                result.productions.push_back(prod);
            }

            return result;
        }

        /* Returns a reverse topological ordering of the given graph. */
        vector<char32_t> reverseTopologicalOrderingOf(const Graph& g) {
            /* DFS-based topological sort! */
            vector<char32_t> order;
            set<char32_t> visited;
            for (const auto& entry: g) {
                dfs(entry.first, g, order, visited);
            }
            return order;
        }

        /* Converts the given grammar to "strong unit normal form," which is the above version of unit
         * normal form (units may exist, but form a DAG) except that units are removed entirely. This
         * means that if we have A -> B and B -> alpha, then we'll add A -> alpha as well (and eventually
         * remove A -> B.
         *
         * The basic procedure here is pretty similar to computing unit normal form. In fact, that's the
         * first step! From there, we do a reverse topological sort, pulling each production up from
         * the direct children as we go.
         */
        CFG strongUnitNormalForm(const CFG& cfg) {
            auto unit = unitNormalForm(cfg);

            /* Restructure the grammar in a way that supports fast lookup of productions
             * per nonterminal.
             */
            map<char32_t, set<Production>> productions;
            for (const auto& prod: unit.productions) {
                productions[prod.nonterminal].insert(prod);
            }

            /* Run a reverse topological sort over the unit graph to copy things from
             * children, removing units in the process.
             */
            for (char32_t nonterminal: reverseTopologicalOrderingOf(unitGraphOf(unit))) {
                set<Production> next;

                /* Copy non-units, replace units.
                 *
                 * We use square brackets here rather than .at because it's entirely
                 * possible that the grammar we're working with has no production
                 * for this nonterminal.
                 */
                for (const auto& prod: productions[nonterminal]) {
                    /* Nonterminal units get replaced by all of the derived productions. */
                    if (isNonterminalUnit(prod)) {
                        /* If this is a self-loop, just skip it. */
                        if (prod.replacement[0].ch != prod.nonterminal) {
                            /* Copy, not reference. */
                            for (auto derived: productions.at(prod.replacement[0].ch)) {
                                derived.nonterminal = nonterminal;
                                next.insert(derived);
                            }
                        }
                    }
                    /* Everything else is copied as usual. */
                    else {
                        next.insert(prod);
                    }
                }

                productions[nonterminal] = next;
            }

            /* Rebuild the grammar. */
            auto result = unit;
            result.productions.clear();
            for (const auto& entry: productions) {
                for (const auto& prod: entry.second) {
                    result.productions.push_back(prod);
                }
            }

            return result;
        }
    }

    /* Generic routine to turn something into one of the CNF flavors. */
    CFG toCNF(const CFG& cfg, CFG unitFormer(const CFG &)) {
        auto nulls = nullablesOf(cfg);
        auto transformed = unitFormer(clean(epsilonNormalFormOf(binarize(indirectTerminals(addUniqueStartTo(cfg))))));

        /* Add S -> epsilon if the original start symbol was nullable. */
        if (nulls.count(cfg.startSymbol)) {
            transformed.productions.push_back({ transformed.startSymbol, {} });
        }

        CFG result;
        result.alphabet = transformed.alphabet;
        result.productions = transformed.productions;
        result.startSymbol = transformed.startSymbol;
        result.nonterminals = transformed.nonterminals;
        return result;
    }

    /* TODO: This doesn't handle the case where there are useless rules.
     * Make sure to clean the grammar first.
     */
    CFG toCNF(const CFG& cfg) {
        return toCNF(cfg, strongUnitNormalForm);
    }

    /* TODO: This doesn't handle the case where there are useless rules.
     * Make sure to clean the grammar first.
     *
     * TODO: Refactor this and the above function.
     */
    CFG toWeakCNF(const CFG& cfg) {
        return toCNF(cfg, unitNormalForm);
    }

    /**************************************************************************
     **************************************************************************
     ***                  CYK Algorithm Implementation                      ***
     **************************************************************************
     **************************************************************************

     The CYK algorithm is a fast (O(n^3)) time recognition algorithm that works
     on grammars encoded in CNF. This restriction means that it's not very good
     at producing derivations of arbitrary grammars, but that it is fast when
     we're allowed to do some preprocessing work on the grammars up front.

     *************************************************************************/

    namespace {
        /* Key in the CYK table: it's of the form [nonterminal, start, end) */
        using CYKKey = tuple<char32_t, size_t, size_t>;

        /* CYK parsing state. */
        struct CYKState {
            /* Direct terminal productions: S -> a */
            map<char32_t, set<char32_t>> terminals;

            /* Unit productions: S -> A */
            map<char32_t, set<char32_t>> units;

            /* Binary productions: S -> AB */
            map<char32_t, set<pair<char32_t, char32_t>>> binaries;

            /* Memoization table. */
            map<CYKKey, bool> memo;
        };

        /* CYK parser, implemented top-down via memoization. The idea is the following:
         *
         * 1. If the input length is one, we match iff (1) the current nonterminal
         *    directly produces the terminal or (2) the nonterminal produces a unit that
         *    produces the terminal.
         * 2. Otherwise, we match iff (1) the current nonterminal has a binary production
         *    that can match the range at some partition point or (2) the current nonterminal
         *    has a unit produces the range.
         *
         * We assume the grammar is in weak CNF on entry to this function.
         */
        bool cyk(CYKState& state,
                 const vector<char32_t>& input,
                 char32_t nonterminal,
                 size_t start, size_t end, size_t indent = 2) {
            if (kParserVerbose) cout << string(indent, ' ') << toUTF8(nonterminal) << " [" << start << ", " << end << ")" << endl;

            /* Base case: Memoized? Then return what we know. */
            CYKKey key = make_tuple(nonterminal, start, end);
            if (state.memo.count(key)) {
                if (kParserVerbose) cout << string(indent, ' ') << "  Known; answer is " << state.memo[key] << endl;
                return state.memo[key];
            }

            /* For convenience. */
            bool& result = state.memo[key];

            /* Base case: Range has size one. */
            if (start + 1 == end) {
                if (kParserVerbose) cout << string(indent, ' ') << "  Single character, terminal " << toUTF8(input[start]) << endl;

                /* Can we immediately make this terminal? */
                if (state.terminals[nonterminal].count(input[start])) {
                    if (kParserVerbose) cout << string(indent, ' ') << "  We make this!" << endl;
                    return result = true;
                }

                if (kParserVerbose) cout << string(indent, ' ') << "  We don't make this; trying units." << endl;

                /* Can we use a unit to get there? */
                for (char32_t next: state.units[nonterminal]) {
                    if (kParserVerbose) cout << string(indent, ' ') << "  Unit: " << toUTF8(next) << endl;
                    if (cyk(state, input, next, start, end, indent + 4)) {
                        if (kParserVerbose) cout << string(indent, ' ') << "  Unit worked, so we do too!" << endl;
                        return result = true;
                    }
                }

                if (kParserVerbose) cout << string(indent, ' ') << "  We can't make this terminal." << endl;

                /* Oops, nothing works. */
                return result = false;
            }

            if (kParserVerbose) cout << string(indent, ' ') << "  Range size > 1, trying binaries." << endl;

            /* Otherwise, the range has size at least two. Try all binary productions
             * and all splits.
             */
            for (auto next: state.binaries[nonterminal]) {
                if (kParserVerbose) cout << string(indent, ' ') << "  " << toUTF8(next.first) << toUTF8(next.second) << endl;
                for (size_t mid = start + 1; mid < end; mid++) {
                    if (kParserVerbose) cout << string(indent, ' ') << "  [" << start << ", " << mid << ") and [" << mid << ", " << end << ")" << endl;
                    /* Does this split work? */
                    if (cyk(state, input, next.first,  start, mid, indent + 4) &&
                        cyk(state, input, next.second, mid,   end, indent + 4)) {
                        if (kParserVerbose) cout << string(indent, ' ') << "  Success!" << endl;
                        return result = true;
                    }
                }
            }

            /* If binaries fail, try units. */
            if (kParserVerbose) cout << string(indent, ' ') << "  No binaries succeeded; trying units." << endl;
            for (auto next: state.units[nonterminal]) {
                if (kParserVerbose) cout << string(indent, ' ') << "  Trying unit " << toUTF8(next) << endl;
                if (cyk(state, input, next, start, end, indent + 4)) {
                    if (kParserVerbose) cout << string(indent, ' ') << "  Success!" << endl;
                    return result = true;
                }
            }


            return result = false;
        }

        Matcher cykMatcherFor(const CFG& cfg) {
            /* Convert to weak CNF to ensure all RHS's have the
             * right sizes.
             */
            auto weakCNF = toWeakCNF(cfg);

            cout << weakCNF << endl;

            /* Epsilon is a special case. */
            bool hasEpsilon = nullablesOf(cfg).count(cfg.startSymbol);

            /* Convert the grammar into a more usable form. */
            CYKState state;
            for (const auto& prod: weakCNF.productions) {
                const auto& p = prod.replacement;

                /* Classify production. */
                if (p.size() == 2) {
                    state.binaries[prod.nonterminal].insert(make_pair(p[0].ch, p[1].ch));
                } else if (p.size() == 1 && p[0].type == Symbol::Type::TERMINAL) {
                    state.terminals[prod.nonterminal].insert(p[0].ch);
                } else if (p.size() == 1 && p[0].type == Symbol::Type::NONTERMINAL) {
                    state.units[prod.nonterminal].insert(p[0].ch);
                } else {
                    throw runtime_error("Illegal production.");
                }
            }

            /* Start symbol. */
            char32_t start = weakCNF.startSymbol;

            return [=](const string& str) mutable {
                auto input = utf8Decode(str, weakCNF.alphabet);
                if (input.empty()) return hasEpsilon;

                /* Wipe any previous memoization result. */
                state.memo.clear();
                return cyk(state, input, start, 0, input.size());
            };
        }

        /**************************************************************************
         **************************************************************************
         ***                LR(0)-Based Earley Implementtion                    ***
         **************************************************************************
         **************************************************************************

         An implementation of Earley's algorithm that's sped up by precomputing
         which sorts of item sets will travel together as a unit. This works by
         making what the paper "Practical Earley Parsing" by Aycock and Horspool
         refer to as the "split LR(0) e-DFA."

         This implementation has been aggressively optimized to try to squeeze out
         as much performance as possible, as we use it as our default matcher. This
         means that the code may not be nearly as readable as you might like/hope.
         Sorry about that!

         The basic idea is the following:

           1. Build an automaton that essentially does all the work the predictor
              usually does up-front so that instead of pushing around individual
              productions, we're working with collapsed sets of LR(0) states.
           2. Optimize the automaton so that its GOTO table is done purely using
              some quick vector index lookups rather than using sets or maps or
              anything like that.

         *************************************************************************/

        /* LR(0) item is a production and a dot position. */
        struct LR0Item {
            const Production* production;
            size_t dotPos;
        };

        bool operator< (const LR0Item& lhs, const LR0Item& rhs) {
            if (lhs.production != rhs.production) return lhs.production < rhs.production;
            return lhs.dotPos < rhs.dotPos;
        }

        ostream& operator<< (ostream& out, const LR0Item& item) {
            ostringstream builder;
            builder << Symbol{ Symbol::Type::NONTERMINAL, item.production->nonterminal } << " -> ";
            for (size_t i = 0; i < item.dotPos; i++) {
                builder << item.production->replacement[i];
            }
            builder << ".";
            for (size_t i = item.dotPos; i < item.production->replacement.size(); i++) {
                builder << item.production->replacement[i];
            }
            return out << builder.str();
        }

        /* For debugging. */
        ostream& operator<< (ostream& out, const set<LR0Item>& set) {
            for (auto item: set) {
                out << item << endl;
            }
            return out;
        }

        /* LR(0) e-DFA item. */
        struct LR0EState {
            /* Numeric index of this state. */
            size_t index;

            /* Set of items here. This isn't strictly needed, but it's
             * helpful for debugging.
             */
            set<LR0Item> items;

            /* Transitions to other LR(0) e-states. */
            unordered_map<Symbol, LR0EState*> transitions;

            /* Epsilon transition, if any. */
            LR0EState* epsilon;
        };

        /* LR(0) e-DFA. This is the actual automaton representation, and it's designed to
         * be as fast as we can make it, potentially at the expense of readability.
         */
        struct LR0EDFA {
            /* Set of all states. */
            set<shared_ptr<LR0EState>> states;

            /* Start state. */
            LR0EState* start;

            /* A huge amount of our time is spent doing lookups of the form
             * "given state X and symbol Y, what's the successor state?" To speed
             * this up, we build a flat vector representing a 2D "goto" table for
             * our automaton. We will number the symbols we might encounter 0, 1
             * 2, 3, ..., n-1, then build an array of the form [goto for state 0],
             * [goto for state 1], etc. by linearizing everything.
             *
             * To make this work, we first need a way of relabeling symbols with
             * numbers, which is this first table.
             */
            unordered_map<Symbol, size_t> toIndex;

            /* Multiplier to use to go from a state number to a table index. */
            /* TODO: Would it be better to use a shift here? */
            size_t multiplier;

            /* Next, we need that linearized GOTO table. */
            vector<const LR0EState*> goTo;

            /* Map from state numbers to completed sets. */
            vector<vector<size_t>> completed;
        };

        /* Utility functions for working with LR(0) items. */
        bool dotAtEnd(const LR0Item& item) {
            return item.dotPos == item.production->replacement.size();
        }
        Symbol afterDot(const LR0Item& item) {
            if (dotAtEnd(item)) abort(); // Logic error!
            return item.production->replacement[item.dotPos];
        }
        LR0Item advanceDot(LR0Item item) {
            if (dotAtEnd(item)) abort();
            item.dotPos++;
            return item;
        }

        /* Advances the dot of each item in a set of LR(0) items. */
        set<LR0Item> advanceDot(const set<LR0Item>& items, Symbol s) {
            set<LR0Item> result;
            for (const auto& item: items) {
                if (!dotAtEnd(item) && afterDot(item) == s) {
                    result.insert(advanceDot(item));
                }
            }
            return result;
        }

        /* Computes the LR(0) e-DFA closure of the given set of LR(0) items.
         *
         * There are two different types of closures. The first one yields
         * the KERNEL STATE and is formed by only moving the dot forward
         * over nullable nonterminals. The second closure yields NON-KERNEL
         * STATES and is a combination of the previous rule and the regular
         * LR(0) lookahead rules.
         */
        enum ClosureType {
            KERNEL,
            NON_KERNEL
        };

        ostream& operator<< (ostream& out, ClosureType type) {
            return out << (type == ClosureType::KERNEL? "Kernel" : "Non-Kernel");
        }

        set<LR0Item> closureOf(const set<LR0Item>& items,
                               const CFG& cfg,
                               const Nulls& nullable,
                               ClosureType type) {
            set<LR0Item> result;

            /* Form a worklist of all items that have dots before nonterminals. */
            queue<LR0Item> worklist;

            /* The setup here differs based on the type of the closure.
             *
             * If this is a kernel closure, we preserve all the items already in the
             * closure, then place all items with dots before nonterminals into the
             * worklist.
             *
             * If this is a non-kernel closure, we do NOT preserve the items already
             * in the closure. Instead, for each production S -> alpha . X beta, we
             * add the productions X -> .gamma into the worklist.
             */
            if (type == ClosureType::KERNEL) {
                for (const auto& item: items) {
                    if (!dotAtEnd(item) && afterDot(item).type == Symbol::Type::NONTERMINAL) {
                        worklist.push(item);
                    }
                    result.insert(item);
                }
            } else {
                for (const auto& item: items) {
                    if (!dotAtEnd(item) && afterDot(item).type == Symbol::Type::NONTERMINAL) {
                        /* Find all productions to use. */
                        for (const auto& prod: cfg.productions) {
                            if (prod.nonterminal == afterDot(item).ch) {
                                /* Place the item in the result. */
                                LR0Item item = { &prod, 0 };
                                result.insert(item);

                                /* And if the item at the beginning is a nonterminal, add it for further
                                 * processing.
                                 */
                                if (!dotAtEnd(item) && afterDot(item).type == Symbol::Type::NONTERMINAL) {
                                    worklist.push(item);
                                }
                            }
                        }
                    }
                }
            }

            /* Now, compute the closure of the initial set. */
            while (!worklist.empty()) {
                auto curr = worklist.front();
                worklist.pop();
                auto symbol = afterDot(curr);
                if (symbol.type == Symbol::Type::TERMINAL) abort(); // Logic error!

                /* If the nonterminal is nullable, shift the dot forward. */
                if (nullable.count(symbol.ch)) {
                    auto next = advanceDot(curr);
                    if (result.insert(next).second) {
                        /* If the dot is before a nonterminal, add back for later processing. */
                        if (!dotAtEnd(next) && afterDot(next).type == Symbol::Type::NONTERMINAL) {
                            worklist.push(next);
                        }
                    }
                }

                /* If we're making the non-kernel state, do some predictions as
                 * well.
                 */
                if (type == ClosureType::NON_KERNEL) {
                    /* For each production S -> alpha, add S -> .alpha. */
                    for (const Production& prod: cfg.productions) {
                        if (prod.nonterminal == symbol.ch) {
                            LR0Item next = { &prod, 0 };
                            if (result.insert(next).second) {
                                if (!dotAtEnd(next) && afterDot(next).type == Symbol::Type::NONTERMINAL) {
                                    worklist.push(next);
                                }
                            }
                        }
                    }
                }
            }

            return result;
        }

        /* Constructs the LR(0) e-state(s) associated with the given closure.
         * If the provided closure is a kernel closure, we'll also build the
         * non-kernel closure and link it in with the rest of things, though
         * it won't be returned.
         */
        LR0EState*
        buildStateFor(const set<LR0Item>& closure,
                      ClosureType type,
                      const CFG& cfg,
                      const Nulls& nullable,
                      map<set<LR0Item>, shared_ptr<LR0EState>>& states,
                      size_t indent = 2) {
            /* If we already know this one, there's nothing to do. */
            if (states.count(closure)) return states[closure].get();

            if (kParserVerbose) {
                cout << string(indent, ' ') << "State: (" << type << ")" << endl;
                for (const auto& item: closure) {
                    cout << string(indent, ' ') << "  " << item << endl;
                }
            }

            auto state = make_shared<LR0EState>();
            state->items = closure;
            state->index = states.size(); // Next free number

            if (kParserVerbose) cout << string(indent, ' ') << "Number " << state->index << endl;

            /* Store the state now so we don't infinitely loop. :-) */
            states[closure] = state;

            /* Fill in transitions. */
            vector<Symbol> follows;
            for (char32_t ch: cfg.nonterminals) {
                follows.push_back({ Symbol::Type::NONTERMINAL, ch });
            }
            for (char32_t ch: cfg.alphabet) {
                follows.push_back({ Symbol::Type::TERMINAL, ch });
            }

            for (Symbol s: follows) {
                /* See if there's anything resulting from a dot shift here. */
                auto next = closureOf(advanceDot(closure, s), cfg, nullable, ClosureType::KERNEL);
                if (next.empty()) {
                    state->transitions[s] = nullptr;
                } else {
                    if (kParserVerbose) {
                        cout << string(indent, ' ') << "On " << s << ", we go here: " << endl;
                        for (const auto& item: next) {
                            cout << string(indent, ' ') << "  " << item << endl;
                        }
                    }

                    state->transitions[s] = buildStateFor(next, ClosureType::KERNEL, cfg, nullable, states, indent + 4);
                }
            }

            /* If this is a kernel state, create the non-kernel state for it as well. */
            if (type == ClosureType::KERNEL) {
                /* Get the non-kernel closure. It may be empty, which would happen if we
                 * have no dots before nonterminals.
                 */
                auto next = closureOf(closure, cfg, nullable, ClosureType::NON_KERNEL);
                if (next.empty()) {
                    state->epsilon = nullptr;
                    if (kParserVerbose) cout << string(indent, ' ') << "  No epsilon." << endl;
                } else {
                    if (kParserVerbose) cout << string(indent, ' ') << "  Epsilon exists. Processing." << endl;
                    state->epsilon = buildStateFor(next, ClosureType::NON_KERNEL, cfg, nullable, states, indent + 4);
                }
            }
            /* Otherwise, we have no epsilon. */
            else {
                state->epsilon = nullptr;
            }

            return state.get();
        }

        struct EDFAEarleyItem {
            const LR0EState* state;
            size_t itemPos;
        };
    }
}
namespace std {
    template <> struct hash<CFG::EDFAEarleyItem> {
        size_t operator()(const CFG::EDFAEarleyItem& e) const {
            return size_t(e.state) + e.itemPos * 0xFFFF;
        }
    };
}
namespace CFG {
    namespace {
        /* For debugging. */
        ostream& operator<< (ostream& out, const EDFAEarleyItem& item) {
            for (const auto& lri: item.state->items) {
                out << lri << " @" << item.itemPos << endl;
            }
            return out;
        }

        void printItems(const vector<vector<EDFAEarleyItem>>& items, size_t index) {
            cout << "=== Items at index " << index << " === " << endl;
            for (const auto& item: items[index]) {
                for (const auto& lri: item.state->items) {
                    cout << "  " << lri << " @" << item.itemPos << endl;
                }
                cout << endl;
            }
            cout << "=========================" << endl;
        }

        /* The "hot" part of the code here consists of inserting and tracking previous
         * Earley items. Specifically, we need to be able to perform the following
         * operations quickly:
         *
         *   1. Determine whether a particular triple of (state + @n + item index)
         *      exists. (This is done during the scan/complete/predict steps.)
         *   2. Add such a triplet to the collection.
         *   3. Iterate over all (state + @n) items in a given index.
         *
         * Originally, we did this by having a vector<unordered_set<EDFAEarleyItem>>
         * that held everything. The problem here is that the unordered_set was taking
         * a while - I guess it doesn't have the best performance and the cost of
         * hashing is a bit high?
         *
         * Instead, we're going to make a time/space tradeoff. Empirically, we usually
         * have something like ~50 states, and our strings are short (say, 25 characters
         * long). If we store all possible state/pos/pos pairs, that requies us to use
         * about 50 * 25 * 25 = 31,250 combinations. (We could potentially halve this if
         * we wanted to because we know that the indices are always sorted, but let's
         * ignore that for now). If we imagine that each combination is a single bit,
         * then we need about 4k memory, tops, to hold all of these combinations.
         *
         * As an optimization, we'll allocate that array and then use it instead of a
         * hash table. We'll then use a simpler vector<vector<EDFAEarleyItem>> to hold
         * the actual items, still letting us scan over things if we need them.
         */
        struct Bitmap3D {
            vector<uint64_t> bits;
            size_t widthMultiplier;
            size_t depthMultiplier;

            Bitmap3D(size_t w, size_t d, size_t h) : bits((w * d * h + 63) / 64),
                                                     widthMultiplier(d * h),
                                                     depthMultiplier(h) {
                // (w * d * h + 63) / 64 is ceil(wdh / 64).
                // Index calculation is (x, y, z) -> x * d * h + y * h + z,
                // so we store dh and h.
            }
        };

        bool insert(vector<vector<EDFAEarleyItem>>& items,
                    Bitmap3D& bitmap,
                    size_t index, const EDFAEarleyItem& item) {
            size_t pos = index * bitmap.widthMultiplier + item.state->index * bitmap.depthMultiplier + item.itemPos;
            size_t arrSlot = pos >> 6;                  // Pos / 64
            size_t bit     = uint64_t(1) << (pos & 63); // Pos % 64

            if (kParserVerbose) cout << "Attempting to add this item to slot " << index << ":" << endl;
            if (kParserVerbose) cout << item << endl;
            if (kParserVerbose) cout << "Position: " << pos << endl;
            if (kParserVerbose) cout << "ArrSlot:  " << arrSlot << endl;
            if (kParserVerbose) cout << "Bit:      " << (pos & 63) << endl;

            if (bitmap.bits[arrSlot] & bit) {
                if (kParserVerbose) cout << "Already exists." << endl;
                return false;
            }

            if (kParserVerbose) cout << "This is new, and is added." << endl;

            bitmap.bits[arrSlot] |= bit;
            items[index].push_back(item);
            return true;
        }

        /* Runs the e-DFA-backed version of Earley. */
        bool eDFAEarley(const LR0EDFA& eDFA, size_t startSymbol, const vector<size_t>& input) {
            /* Item storage per slot. */
            vector<vector<EDFAEarleyItem>> items(input.size() + 1);

            /* Bitmap, as described above. Dimensions are index / state # / item position. */
            Bitmap3D bitmap(input.size() + 1, eDFA.states.size(), input.size() + 1);

            /* Seed with the initial state, and its epsilon if it has one. */
            insert(items, bitmap, 0, { eDFA.start, 0 });
            //items[0].insert({ eDFA.start, 0 });
            if (eDFA.start->epsilon) {
                //items[0].insert({ eDFA.start->epsilon, 0 });
                insert(items, bitmap, 0, { eDFA.start->epsilon, 0 });
            }

            /* Run the main loop. Note that the traditional roles of "scan," "complete,"
             * and "predict" have been fused together in several ways.
             *
             * This loop is unusual in that it needs to run one more time than what
             * we'd usually expect to see, because some of the "predict" logic has been
             * offloaded into the very last iteration.
             */
            for (size_t i = 0; i <= input.size(); i++) {
                if (kParserVerbose) cout << "Before: " << endl;
                if (kParserVerbose) printItems(items, i);

                /* Create a worklist of what we need to process in this column. */
                queue<EDFAEarleyItem> worklist;
                for (const auto& item: items[i]) {
                    worklist.push(item);
                }

                while (!worklist.empty()) {
                    auto curr = worklist.front();
                    worklist.pop();

                    if (kParserVerbose) cout << "Processing this state:" << endl;
                    if (kParserVerbose) cout << curr << endl;

                    /* Do not do a scan step if we are in the last column. */
                    if (i != input.size()) {
                        /* "Scan" step. Shift each dot over the current symbol. */
                        //auto* next = curr.state->transitions.at({ Symbol::Type::TERMINAL, input[i]});
                        auto* next = eDFA.goTo.at(curr.state->index * eDFA.multiplier + input[i]);
                        if (next != nullptr) {
                            if (kParserVerbose) cout << "Scanning produces this state:" << endl;
                            if (kParserVerbose) cout << next->items << endl;

                            /* Item position hasn't changed; we're still scanning from the same
                             * start position.
                             */
                            //items[i + 1].insert({ next, curr.itemPos });
                            insert(items, bitmap, i + 1, { next, curr.itemPos });

                            /* We may have an epsilon, too! If we do, this corresponds to a "predict"
                             * step and the items start at the next position.
                             */
                            if (next->epsilon) {
                                if (kParserVerbose) cout << "This has an epsilon production in it." << endl;
                                if (kParserVerbose) cout << next->epsilon->items << endl;

                                //items[i + 1].insert({ next->epsilon, i + 1 });
                                insert(items, bitmap, i + 1, { next->epsilon, i + 1 });
                            }
                        } else {
                            if (kParserVerbose) cout << "Nothing here transitions on " << toUTF8(input[i]) << endl;
                        }
                    }

                    /* "Complete" step. This is a bit different from usual. In particular:
                     *
                     * 1. We will be updating the CURRENT set, not the next one. The reason
                     *    for this is that we aren't doing explicit "predict" steps, meaning
                     *    that the sets we set up in the previous step might not have been
                     *    fully expanded out.
                     * 2. We have to factor in epsilon transitions in the automaton, which
                     *    represent that missing "predict" step.
                     * 3. We don't process any items that begin at the current location. The
                     *    reason for this is that any completed items that occur at the current
                     *    location correspond to items that were completed via nulling, and
                     *    we've already precomputed everything that's going to happen as a result.
                     *    (Plus, from an automaton perspective, we don't want to shift the
                     *    nonterminals here without having read something).
                     */
                    if (curr.itemPos == i) continue;

                    if (kParserVerbose) cout << "Looking for completions." << endl;
                    for (size_t completed: eDFA.completed[curr.state->index]) {
                        if (kParserVerbose) cout << "Nonterminal index " << completed << " is completed." << endl;

                        /* Find items to shift over. */
                        for (const auto& prev: items[curr.itemPos]) {
                            /* See where to go; if the answer is "nowhere," skip this. */
                            //auto next = prev.state->transitions.at({Symbol::Type::NONTERMINAL, completed});

                            auto next = eDFA.goTo.at(prev.state->index * eDFA.multiplier + completed);
                            if (!next) continue;

                            /* Standard "complete" step: the item position hasn't
                             * changed; we've just made more progress.
                             */
                            //if (items[i].insert({ next, prev.itemPos }).second) {
                            if (insert(items, bitmap, i, { next, prev.itemPos })) {
                                worklist.push({ next, prev.itemPos });

                                /* "Predict" step. Check if there's an epsilon and,
                                 * if so, those items start here because they
                                 * correspond to expanding out something that appears
                                 * after a dot in a non-epsilon way.
                                 */
                                if (next->epsilon) {
                                    //if (items[i].insert({ next->epsilon, i }).second) {
                                    if (insert(items, bitmap, i, { next->epsilon, i })) {
                                        worklist.push({ next->epsilon, i });
                                    }
                                }
                            }
                        }
                    }
                }

                if (kParserVerbose) cout << "After: " << endl;
                if (kParserVerbose) printItems(items, i);
            }

            /* See if anything completes the start. */
            for (const auto& item: items.back()) {
                if (item.itemPos == 0) {
                    const auto& completed = eDFA.completed.at(item.state->index);
                    if (find(completed.begin(), completed.end(), startSymbol) != completed.end()) {
                        return true;
                    }
                }
            }

            return false;
        }

        /* Builds the index used by the DFA matcher to translate from symbols to indices. */
        void buildIndex(LR0EDFA& eDFA, const CFG& cfg) {
            /* Lay them out linearly. */
            for (char32_t ch: cfg.nonterminals) {
                eDFA.toIndex.insert(make_pair(Symbol{ Symbol::Type::NONTERMINAL, ch }, eDFA.toIndex.size()));
            }
            for (char32_t ch: cfg.alphabet) {
                eDFA.toIndex.insert(make_pair(Symbol{ Symbol::Type::TERMINAL, ch }, eDFA.toIndex.size()));
            }

            eDFA.multiplier = eDFA.toIndex.size();
        }

        /* Builds the GOTO table for the given eDFA. This works by linearizing the individual
         * transition tables based on the ordering given in the index.
         */
        void buildGoToTable(LR0EDFA& eDFA) {
            /* We have one slot per symbol (eDFA.toIndex.size()) per state. */
            eDFA.goTo.resize(eDFA.states.size() * eDFA.multiplier);
            for (const auto& state: eDFA.states) {
                for (const auto& entry: eDFA.toIndex) {
                    size_t index = state->index * eDFA.multiplier + entry.second;
                    eDFA.goTo.at(index) = state->transitions[entry.first];
                }
            }
        }

        /* Builds the 'completed' table for the given eDFA. This maps each state to the set
         * of nonterminals that end there. However, for efficiency each nonterminal is replaced
         * by an index.
         */
        void buildCompletedTable(LR0EDFA& eDFA) {
            eDFA.completed.resize(eDFA.states.size());
            for (const auto& state: eDFA.states) {
                /* Look at all items here and find the completed ones. */
                set<size_t> completed;
                for (const auto& item: state->items) {
                    /* If completed, this nonterminal - or at least, its translated version - is completed. */
                    if (dotAtEnd(item)) {
                        completed.insert(eDFA.toIndex.at({ Symbol::Type::NONTERMINAL, item.production->nonterminal }));
                    }
                }

                eDFA.completed.at(state->index).assign(completed.begin(), completed.end());
            }
        }

        Matcher earleyLR0MatcherFor(const CFG& cfg) {
            /* Clone the grammar so we have a persistent copy. */
            auto ourCFG = make_shared<CFG>(addUniqueStartTo(cfg));

            /* Need to know what's nullable to do dot shifts. */
            auto nulls = nullablesOf(*ourCFG);

            /* Map from LR(0) e-configurating sets to states. */
            map<set<LR0Item>, shared_ptr<LR0EState>> states;

            /* Seed the initial state with all productions from the start
             * symbol.
             */
            set<LR0Item> initial;
            for (const auto& prod: ourCFG->productions) {
                if (prod.nonterminal == ourCFG->startSymbol) {
                    initial.insert({ &prod, 0 });
                }
            }

            /* Construct the automaton. We seed things with the kernel state corresponding to
             * the start configuration.
             */
            LR0EDFA eDFA;
            eDFA.start = buildStateFor(closureOf(initial, *ourCFG, nulls, ClosureType::KERNEL),
                                       ClosureType::KERNEL,
                                       *ourCFG,
                                       nulls,
                                       states);

            /* Form a proper automaton. Copy the states over. */
            for (const auto& entry: states) {
                eDFA.states.insert(entry.second);
            }

            /* Form the linear ordering used in the GOTO table. */
            buildIndex(eDFA, *ourCFG);

            /* Build the GOTO table. */
            buildGoToTable(eDFA);

            /* And the completed table, too! */
            buildCompletedTable(eDFA);

            return [=](const string& str) {
                /* Translate input characters to their indices. */
                vector<size_t> input;
                for (char32_t terminal: utf8Decode(str, ourCFG->alphabet)) {
                    input.push_back(eDFA.toIndex.at({ Symbol::Type::TERMINAL, terminal }));
                }
                return eDFAEarley(eDFA, eDFA.toIndex.at({ Symbol::Type::NONTERMINAL, ourCFG->startSymbol }), input);
            };
        }
    }

    Matcher matcherFor(const CFG& cfg, MatcherType type) {
        if (type == MatcherType::EARLEY) {
            return earleyMatcherFor(cfg);
        } else if (type == MatcherType::CYK) {
            return cykMatcherFor(cfg);
        } else if (type == MatcherType::EARLEY_LR0) {
            return earleyLR0MatcherFor(cfg);
        } else {
            throw runtime_error("Unknown matcher type.");
        }
    }

    namespace {
        /* Base index for Unicode characters for nonterminals. Just for fun, I've
         * picked it to be near a bunch of cute symbols and emojis. :-)
         */
        const char32_t kBaseUnicode = 0x1F300;
    }

    /**************************************************************************
     **************************************************************************
     ***                Language Transform Implementations                  ***
     **************************************************************************
     **************************************************************************

     Implementation of functions to compute various transformations on CFLs and
     regular languages.

     *************************************************************************/

    /* Returns a new CFG that's the union of the two input CFGs. This works
     * by assigning unique names to all the nonterminals in the two grammars,
     * then adding a new start symbol S' with productions S' -> S_L | S_R.
     */
    CFG unionOf(const CFG& lhs, const CFG& rhs) {
        if (lhs.alphabet != rhs.alphabet) throw runtime_error("Alphabets don't match.");

        CFG result;
        result.alphabet = lhs.alphabet;

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
        for (auto prod: lhs.productions) { // Copy, not ref
            prod.nonterminal = nameFor(prod.nonterminal);
            for (auto& symbol: prod.replacement) {
                if (symbol.type == Symbol::Type::NONTERMINAL) {
                    symbol.ch = nameFor(symbol.ch);
                }
            }
            result.productions.push_back(prod);
        }
        for (auto prod: rhs.productions) { // Copy, not ref
            prod.nonterminal = nameFor(prod.nonterminal);
            for (auto& symbol: prod.replacement) {
                if (symbol.type == Symbol::Type::NONTERMINAL) {
                    symbol.ch = nameFor(symbol.ch);
                }
            }
            result.productions.push_back(prod);
        }

        /* Add new start symbol S' with productions S' -> S_L and S' -> S_R. */
        result.startSymbol = next;
        result.nonterminals.insert(next);
        result.productions.push_back({ result.startSymbol, { nonterminal(nameFor(lhs.startSymbol)) } });
        result.productions.push_back({ result.startSymbol, { nonterminal(nameFor(rhs.startSymbol)) } });

        return result;
    }

    /* Given a DFA and a CFG, forms a new CFG whose language is the intersection
     * of the languages of the two inputs. This uses an algorithm (I believe) by
     * Bar-Hillel, which I heard about originally from Grune and Jacobs' book
     * "Parsing Techniques: A Practical Guide" and adapted from these lecture
     * notes: https://www.cs.umd.edu/~gasarch/COURSES/452/F14/cfgreg.pdf
     *
     * The intuitive idea here is to create a series of nonterminals that
     * collectively encode the idea of "we read an (original) nonterminal
     * while also transitioning from state qx to qy." These nonterminals will
     * be denoted [S, qx, qy].
     *
     * We begin by getting the grammar into (weak) CNF (we could use regular
     * CNF, but that's going to blow up the grammar size pretty significantly
     * and we don't want to do that - this next step is already going to add
     * a bunch of nonterminals!)
     *
     * Now, we begin rewriting the grammar. For each production of the form
     *
     *    A -> a
     *
     * we introduce productions of the form
     *
     *    [A, qx, delta(qx, a)] -> a
     *
     * This corresponds to producing A -> a while also transitioning from qx to qy.
     *
     * Next, for each production of the form
     *
     *    A -> BC,
     *
     * we create
     *
     *    [A, qx, qy] -> [B, qx, qz] [C, qz, qy]
     *
     * for each triple of states qx, qy, and qz. This corresponds to the idea
     * that if we read A -> BC while transitioning from qx to qy, it means we
     * read B by going from qx to some state qz, then read C by going from
     * qz to some state qy.
     *
     * Then, for each unit of the form
     *
     *    A -> B,
     *
     * we create
     *
     *    [A, qx, qy] -> [B, qx, qy]
     *
     * which encodes the idea that if we want to use A -> B while going from qx
     * to qy, then we can do so by instead producing B and going from qx to qy.
     *
     * If the grammar's start symbol has an epsilon production, then we create
     *
     *    [S, q0, q0] -> epsilon
     *
     * only in the case where q0 in F. In other words, if we want to produce
     * epsilon, it has to be the case that the DFA's start state is accepting.
     *
     * Finally, we introduce a new start symbol S' and then introduce the
     * production
     *
     *    S' -> [S, q0, qf]
     *
     * for each accepting state qf. This says "anything we produce needs to
     * be something in the CFG (derived from S) and also something accepted
     * by the DFA (getting us from q0 to qf).
     *
     * This grammar will have a LOT of nonterminals in it, many of which will
     * be completely unreachable. To be a good citizen about it, as a final
     * postprocessing step we convert things to weak CNF, which has the effect
     * of cleaning up the grammar a bit.
     */
    namespace {
        /* Given a DFA, return its start state. */
        const Automata::State* startOf(const Automata::DFA& dfa) {
            for (auto state: dfa.states) {
                if (state->isStart) return state.get();
            }
            abort(); // Logic error!
        }

        /* Delta function for a DFA. */
        const Automata::State* delta(const Automata::State* state, char32_t ch) {
            return state->transitions.find(ch)->second;
        }

        const bool kIntersectVerbose = false;
    }

    CFG intersect(const CFG& input, const Automata::DFA& dfa) {
        if (input.alphabet != dfa.alphabet) throw runtime_error("Alphabets don't match.");

        /* Place the grammar into weak CNF. */
        auto cfg = toWeakCNF(input);
        CFG result;

        /* Map from (nonterminal, start, end) to new nonterminal names. */
        map<tuple<char32_t, const Automata::State*, const Automata::State*>, char32_t> names;
        char32_t next = kBaseUnicode;
        auto symbolFor = [&](char32_t nonterminal, const Automata::State* from, const Automata::State* to) {
            auto key = make_tuple(nonterminal, from, to);
            if (!names.count(key)) {
                /* Skip to next free nonterminal name.
                 *
                 * TODO: This isn't necessary, right? Because we're producing an entirely
                 * new set of nonterminal names?
                 */
                while (cfg.nonterminals.count(next)) next++;

                names[key] = next;
                result.nonterminals.insert(next);
                next++;
                if (kIntersectVerbose) cout << "  Symbol " << toUTF8(names[key]) << " denotes [" << toUTF8(nonterminal) << ", " << from->name << ", " << to->name << "]" << endl;
            }
            return names[key];
        };

        result.alphabet = cfg.alphabet;
        auto q0 = startOf(dfa);

        /* Transform all productions. */
        for (const auto& prod: cfg.productions) {
            const auto& p = prod.replacement;
            if (kIntersectVerbose) cout << prod << endl;

            /* Epsilon? */
            if (p.size() == 0) {
                if (kIntersectVerbose) cout << "  Epsilon production." << endl;
                /* Add [S, q0, q0] -> epsilon, but only if q0 is accepting. */
                if (q0->isAccepting) {
                    result.productions.push_back({ symbolFor(prod.nonterminal, q0, q0), { } });
                    if (kIntersectVerbose) cout << "  Made " << result.productions.back() << endl;
                } else {
                    if (kIntersectVerbose) cout << "  Start state isn't accepting." << endl;
                }
            }
            /* Terminal unit? */
            else if (p.size() == 1 && p[0].type == Symbol::Type::TERMINAL) {
                if (kIntersectVerbose) cout << "  Unit terminal." << endl;
                /* Add [A, qx, delta(qx, a)] -> a for all states qx. */
                for (const auto& from: dfa.states) {
                    auto* to = delta(from.get(), p[0].ch);

                    result.productions.push_back({ symbolFor(prod.nonterminal, from.get(), to), { p[0] } });
                    if (kIntersectVerbose) cout << "  Made " << result.productions.back() << endl;
                }
            }
            /* Nonterminal unit? */
            else if (p.size() == 1 && p[0].type == Symbol::Type::NONTERMINAL) {
                if (kIntersectVerbose) cout << "  Unit nonterminal." << endl;

                /* Add [A, qx, qy] -> [B, qx, qy] for all states qx, qy. */
                for (const auto& qx: dfa.states) {
                    for (const auto& qy: dfa.states) {
                        auto lhs = symbolFor(prod.nonterminal, qx.get(), qy.get());
                        auto rhs = symbolFor(p[0].ch, qx.get(), qy.get());

                        result.productions.push_back({ lhs, { nonterminal(rhs) } });
                        if (kIntersectVerbose) cout << "  Made " << result.productions.back() << endl;
                    }
                }
            }
            /* Binary production? */
            else if (p.size() == 2) {
                if (kIntersectVerbose) cout << "  Binary production." << endl;

                /* Add [A, qx, qy] -> [B, qx, qz] [C, qz, qy] for all states qx, qy, qz. */
                for (const auto& qx: dfa.states) {
                    for (const auto& qy: dfa.states) {
                        for (const auto& qz: dfa.states) {
                            auto lhs  = symbolFor(prod.nonterminal, qx.get(), qy.get());
                            auto rhs1 = symbolFor(p[0].ch, qx.get(), qz.get());
                            auto rhs2 = symbolFor(p[1].ch, qz.get(), qy.get());

                            result.productions.push_back({ lhs, { nonterminal(rhs1), nonterminal(rhs2) } });
                            if (kIntersectVerbose) cout << "  Made " << result.productions.back() << endl;
                        }
                    }
                }
            }
            /* Oops, that's impossible. */
            else {
                abort(); // Logic error!
            }
        }

        /* Set up the start symbol. */
        result.startSymbol = next;
        result.nonterminals.insert(result.startSymbol);

        /* Productions for the start nonterminal. */
        for (const auto& qf: dfa.states) {
            /* Add S' -> [S, q0, qf] for each accepting qf. */
            if (qf->isAccepting) {
                auto rhs = symbolFor(cfg.startSymbol, q0, qf.get());
                result.productions.push_back({ result.startSymbol, { nonterminal(rhs) } });
                if (kIntersectVerbose) cout << "  Start: Made " << result.productions.back() << endl;
            }
        }

        return clean(result);
    }
}
