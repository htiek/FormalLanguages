#include "Utilities/JSON.h"
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <set>
#include <queue>
#include <stack>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <iterator>
using namespace std;

namespace {
  /* Name of the special START symbol that kicks things off. */
  const string kStartSymbol = "_parserInternalStart";

  /* Type representing a production rule. The second expression, if any, is the code
   * to run when performing the reduction. Use bison-style terms to denote the code
   * that should be run here ($$ for the value of the nonterminal, $1 for the first item,
   * $2 for the second, etc.
   */
  struct ProductionRule {
    vector<string> terms;
    string action;
  };

  /* Type representing a grammar, plus all associated information. */
  struct Grammar {
    /* The grammar itself, with semantic actions. */
    map<string, vector<ProductionRule>> grammar;

    /* Priority ordering of the terminals. */
    vector<string> priorities;

    /* Map from the types associated with each nonterminal to the
     * name of the autogenerated field included in AuxData for
     * that type.
     */
    map<string, string> typeToField;

    /* Types associated with each nonterminal. */
    map<string, string> nonterminalTypes;

    /* Extra code to include in the header. */
    vector<string> headerExtras;

    /* Whether to generate a verbose parser. */
    bool verbose;

    /* Name of this parser, used to come up with filenames and name parsers. */
    string name;
  };

  struct Production {
    string nonterminal;
    vector<string> items;
    size_t index;
  };

  bool operator< (const Production& lhs, const Production& rhs) {
    if (lhs.nonterminal != rhs.nonterminal) return lhs.nonterminal < rhs.nonterminal;
    if (lhs.items != rhs.items) return lhs.items < rhs.items;
    return lhs.index < rhs.index;
  }

  ostream& operator<< (ostream& out, const Production& toPrint) {
    out << toPrint.nonterminal << " -> ";
    
    for (size_t i = 0; i < toPrint.items.size(); i++) {
      /* Is the dot just before us? */
      if (toPrint.index == i) out << ". ";
      out << toPrint.items[i] << " ";
    }
    
    /* Is the dot at the end? */
    if (toPrint.index == toPrint.items.size()) out << ".";
    return out;
  }

  /* Is this a nonterminal? */
  bool isNonterminal(const Grammar& g, const string& symbol) {
    return g.grammar.count(symbol);
  }

  /* Recursively compute the closure of a production, given a set of already-
   * produced items.
   */
  void closureRec(const Grammar& g, const Production& production, set<Production>& result) {
    /* Base case: Are we already contained here? */
    if (result.count(production)) return;
    
    /* Add us, then add all possible expansions of this production. */
    result.insert(production);
    
    /* Nothing after the dot? We're done! */
    if (production.index == production.items.size()) return;
    
    /* Next item isn't a nonterminal? We're done! */
    string symbol = production.items[production.index];
    if (!g.grammar.count(symbol)) return;
    
    /* Expand them all out. */
    for (const auto& rule: g.grammar.at(symbol)) {
      Production toAdd;
      toAdd.nonterminal = symbol;
      toAdd.index = 0;
      toAdd.items = rule.terms;
      
      closureRec(g, toAdd, result);
    }
  }

  /* Given a production, produces the LR closure of that production. */
  set<Production> closureOf(const Grammar& g, const Production& production) {
    set<Production> result;
    closureRec(g, production, result);
    return result;
  }

  /* Type representing what we shifted from the previous state to get here. */
  struct ShiftInfo {
    string shifted;
    size_t parent;
  };

  /* Returns all successors of the given state. */
  map<string, set<Production>> successorsOf(const Grammar& g,
                                            const set<Production>& productions) {
    map<string, set<Production>> result;
    
    /* For each production, see if we can move the dot over one step. */
    for (Production p: productions) {
      if (p.index != p.items.size()) {
        const string symbol = p.items[p.index];
        
        /* Have we seen this before? */
        if (!result.count(symbol)) {
          /* Shift over everything that starts with the next symbol. */
          for (Production toShift: productions) {
            if (toShift.index != toShift.items.size() &&
                toShift.items[toShift.index] == symbol) {
              toShift.index++;
              
              /* Include the closure of this new item. */
              for (Production q: closureOf(g, toShift)) {
                result[symbol].insert(q);
              }
            }
          }
        }
      }
    }
    
    return result;
  }

  /* Given a production, returns the priority of that production, which is the
   * priority associated with the leftmost ranked terminal.
   */
  int priorityOf(const Grammar& g, const Production& production) {
    for (string symbol: production.items) {
      if (!g.grammar.count(symbol)) { // Is terminal
        /* Do we have a priority for this one? */
        auto itr = find(g.priorities.begin(), g.priorities.end(), symbol);
        if (itr != g.priorities.end()) return itr - g.priorities.begin();
      }
    }

    /* Lowest possible priority. */
    return g.priorities.size();
  }

  /* Finds all reducing actions in the configurating set. */
  set<Production> reductionsIn(const set<Production>& s) {
    set<Production> result;
    
    for (const auto& prod: s) {
      if (prod.index == prod.items.size() && prod.nonterminal != kStartSymbol) {
        result.insert(prod);
      }
    }
    
    return result;
  }

  /* Finds all halting actions in the configurating set. */
  set<Production> haltsIn(const set<Production>& s) {
    set<Production> result;
    
    for (const auto& prod: s) {
      if (prod.index == prod.items.size() && prod.nonterminal == kStartSymbol) {
        result.insert(prod);
      }
    }
    
    return result;
  }

  /* Finds all shift actions in the configurating set. */
  set<Production> shiftsIn(const set<Production>& s) {
    set<Production> result;
    
    for (const auto& prod: s) {
      if (prod.index != prod.items.size()) {
        result.insert(prod);
      }
    }
    
    return result;
  }

  /* Maps from configurating sets to indices and vice-versa. */
  using SetToIndex = map<set<Production>, size_t>;
  using IndexToSet = map<size_t, set<Production>>;

  pair<SetToIndex, IndexToSet> generateConfiguratingSets(const Grammar& g) {
    /* DFS over LR sets to consider. */
    stack<set<Production>> worklist;
    worklist.push(closureOf(g, { kStartSymbol, g.grammar.at(kStartSymbol)[0].terms, 0 }));
    
    /* Map tracking what we've seen so far and giving each an index. */
    map<set<Production>, size_t> setToIndex;
    map<size_t, set<Production>> indexToSet;
    
    while (!worklist.empty()) {
      auto curr = worklist.top();
      worklist.pop();
      
      /* Have we already seen this? If not, add it. */
      if (setToIndex.count(curr)) continue;
      
      /* Otherwise, give it the new index. */
      setToIndex.insert({ curr, setToIndex.size() });
      indexToSet.insert({ indexToSet.size(), curr });

      /* Find all successors. */
      for (const auto& successor: successorsOf(g, curr)) {
        worklist.push(successor.second);
      }
    }
    
    return { setToIndex, indexToSet };
  }

  /* Given a production that's a reduce item, generates the name of the
   * function that operates on the arguments of that reduce item.
   */
  string reduceFunctionNameFor(const Production& p) {
    ostringstream result;
    result << "reduce_" << p.nonterminal << "_from";
    for (size_t i = 0; i < p.items.size(); i++) {
      result << "_" << p.items[i];
    }
    return result.str();
  }

  string reduceThunkNameFor(const Production& p) {
    return reduceFunctionNameFor(p) + "__thunk";
  }

  /* Given a name of a nonterminal, returns the type of that field. */
  string typeFor(const Grammar& g, const string& nonterminal) {
    if (!g.nonterminalTypes.count(nonterminal)) return "_unused_";

    return g.nonterminalTypes.at(nonterminal);
  }

  string replace(string source, const string& what, const string& with) {
    size_t i = 0;
    while (i = source.find(what, i), i != string::npos) {
      source.replace(i, what.size(), with);
    }
    return source;
  }

  /* Given a production, produces the user code for that production. */
  string codeFor(const Grammar& g, const Production& p) {
    /* This uses a bunch of linear scans. I'm banking on the fact that the grammars
     * aren't big enough to warrant a better approach. :-)
     */

    /* Find the ProductionRule this corresponds to. */
    for (const auto& entry: g.grammar.at(p.nonterminal)) {
      if (entry.terms == p.items) {
        string result = entry.action;
        result = replace(result, "$$", "_parserArg0");
        result = replace(result, "$", "_parserArg");
        return result;
      }
    }

    throw runtime_error("No code for this production?");
  }

  /* Given a production and an argument index, returns whether the user
   * code actually uses the specified argument.
   */
  bool codeUsesArgument(const Grammar& g, const Production& p, size_t index) {
    return codeFor(g, p).find("_parserArg" + to_string(index)) != string::npos;
  }

  /* Given a production that's a reduce item, generates the signature of the
   * function that operates on the arguments of that reduce item.
   */
  string reduceFunctionFor(const Grammar& g, const Production& p) {
    ostringstream result;
    result << typeFor(g, p.nonterminal) << " " << reduceFunctionNameFor(p) << "(";
    for (size_t i = 0; i < p.items.size(); i++) {
      if (g.grammar.count(p.items[i])) {
        result << typeFor(g, p.items[i]);
      } else {
        result << "const std::string&";
      }

      /* Determine whether this argument is actually used in the semantic
       * action. If so, give it a name.
       */
      if (codeUsesArgument(g, p, i + 1)) {
        result << " _parserArg" << (i + 1);
      }
      if (i + 1 != p.items.size()) result << ", ";
    }
    result << ")";
    return result.str();
  }

  /* Given a nonterminal, returns the name of the field in the AuxData struct
   * associated with that nonterminal.
   */
  string fieldNameFor(const Grammar& g, const string& nonterminal) {
    return g.typeToField.at(g.nonterminalTypes.at(nonterminal));
  }

  string reduceThunkFor(const Grammar& g, const Production& p) {
    ostringstream result;
    
    /* Function header */
    result << "  AuxData " << reduceThunkNameFor(p) << "(";
    for (size_t i = 0; i < p.items.size(); i++) {
      result << "StackData";

      /* If we will actually use this argument, give it a name. We use the
       * argument if
       *
       *   (1) This nonterminal of the production has an associated field, and
       *   (2) The argument is not a nonterminal that has no associated field.
       */
      if (g.nonterminalTypes.count(p.nonterminal) && (!isNonterminal(g, p.items[i]) || g.nonterminalTypes.count(p.items[i]))) {
        result << " a" << i;
      }
      if (i + 1 != p.items.size()) result << ", ";
    }
    result << ") {" << endl;
    
    /* Function body: Call the actual reduce function with the right args. */
    
    /* Special case: If we don't need to do anything, don't call the reduce function. */
    if (!g.nonterminalTypes.count(p.nonterminal)) {
      result << "    return {};" << endl;
    } else {
      result << "    AuxData result;" << endl;
      result << "    result." << fieldNameFor(g, p.nonterminal) << " = " << reduceFunctionNameFor(p) << "(";
      
      for (size_t i = 0; i < p.items.size(); i++) {
        if (isNonterminal(g, p.items[i])) {
          /* See what the associated field is. */
          if (g.nonterminalTypes.count(p.items[i])) {
            result << "a" << i << ".data." << fieldNameFor(g, p.items[i]);
          } else {
            result << "{}";
          }
        } else {
          result << "a" << i << ".token.data";
        }
        if (i + 1 != p.items.size()) result << ", ";
      }
      
      result << ");" << endl;
      result << "    return result;" << endl;
    }
    result << "  }" << endl;
    return result.str();
  }

  /* Given a production that's a reduce item, generates the name of the
   * reduce action to use for it.
   */
  string reduceActionFor(const Production& p) {
    ostringstream result;
    result << "new ReduceActionN<" << to_string(p.items.size()) << ">"
           << "(Nonterminal::" + p.nonterminal << ", "
           << reduceThunkNameFor(p) << ")";
    return result.str();
  }

  /* Determines the nullable nonterminals of a grammar. */
  set<string> nullables(const Grammar& g) {
    set<string> result;

    /* Repeatedly expand the set by adding in terms where a production rule
     * consists purely of nullable terms. Note that this accounts for the
     * base case of nonterminals with empty productions, since the condition
     * is vacuously true there.
     */
    bool changed = false;
    do {
      changed = false;

      for (const auto& entry: g.grammar) {
        string nonterminal = entry.first;
        for (const auto& prod: entry.second) {
          if (all_of(prod.terms.begin(), prod.terms.end(),
                     [&] (const string& term) {
                       return result.count(term);
                     })) {
            changed |= result.insert(nonterminal).second;
          }
        }
      }
    } while (changed);

    return result;
  }

  /* Adds all terms of the left-hand set to the right-hand set, returning
   * whether anything was added.
   */
  template <typename SetType>
  bool addAll(const SetType& lhs, SetType& rhs) {
    auto before = rhs.size();
    rhs.insert(lhs.begin(), lhs.end());
    return before != rhs.size();
  }

  /* Computes the FIRST sets for the given grammar. */
  map<string, set<string>> firstSets(const Grammar& g) {
    /* The rule for computing FIRST sets is as follows:
     *
     * 1. If S -> atx is a production, a is nullable, and t is a terminal,
     *    then t in FIRST(S).
     * 2. If S -> aAx is a production, a is nullable, and A is a nonterminal,
     *    then FIRST(A) subset FIRST(S).
     */
    map<string, set<string>> result;
    auto nullable = nullables(g);

    bool changed = false;
    do {
      changed = false;

      for (const auto& entry: g.grammar) {
        string nonterminal = entry.first;
        for (const auto& prod: entry.second) {
          /* Scan across terms until we hit a terminal or non-nullable nonterminal. */
          for (const auto& term: prod.terms) {
            if (isNonterminal(g, term)) {
              /* FIRST(term) subset FIRST(nonterminal) */
              changed |= addAll(result[term], result[nonterminal]);

              /* Continue forward if this nonterminal is nullable. */
              if (!nullable.count(term)) break;
            }
            /* Hit a terminal; stop. */
            else {
              changed |= result[nonterminal].insert(term).second;
              break;
            }
          }
        }
      }

    } while (changed);

    return result;
  }

  /* Computes the FIRST sets for the given grammar. */
  map<string, set<string>> followSets(const Grammar& g) {
    auto nullable = nullables(g);

    cout << "Nullables: ";
    copy(nullable.begin(), nullable.end(), ostream_iterator<string>(cout, " "));
    cout << endl;

    auto firsts = firstSets(g);

    for (const auto& entry: firsts) {
      cout << "FIRST(" << entry.first << ") = { ";
      copy(entry.second.begin(), entry.second.end(), ostream_iterator<string>(cout, " "));
      cout << "}" << endl;
    }

    map<string, set<string>> result;

    bool changed;
    do {
      changed = false;

      /* Rules for follow sets:
       *
       * 1. SCAN_EOF in FOLLOW(START)
       * 2. If S -> aAw   for nullable w, then FOLLOW(S) subset FOLLOW(A)
       * 3. If S -> aAtw  for terminal t, then t in FOLLOW(A)
       * 4. If S -> aAxBw for nullable x, then FIRST(B) subset FOLLOW(A)
       */

      /* Rule 1. */
      changed |= result[kStartSymbol].insert("SCAN_EOF").second;

      for (const auto& entry: g.grammar) {
        string nonterminal = entry.first;
        for (const auto& prod: entry.second) {
          /* This could be made more efficient - right now we use a potentially
           * quadratic-time scanning algorithm - but we imagine that we're not
           * dealing with large enough productions to care. :-)
           */
          for (size_t i = 0; i < prod.terms.size(); i++) {
            /* If this is a nonterminal, look at what follows. */
            if (isNonterminal(g, prod.terms[i])) {
              /* Scan forward until we either (1) run off the end of the production
               * or (2) hit something non-nullable.
               */
              bool nullableToEnd = true;
              for (size_t j = i + 1; j < prod.terms.size(); j++) {
                /* Are we a terminal? If so, add this and stop (Rule 3) */
                if (!isNonterminal(g, prod.terms[j])) {
                  changed |= result[prod.terms[i]].insert(prod.terms[j]).second;
                  nullableToEnd = false;
                  break;
                } else {
                  /* Everything in our FIRST set gets added (Rule 4). */
                  changed |= addAll(firsts[prod.terms[j]], result[prod.terms[i]]);
                  if (!nullable.count(prod.terms[j])) {
                    nullableToEnd = false;
                    break;
                  }
                }
              }

              /* Were we nullable to the end? (Rule 2). */
              if (nullableToEnd) {
                changed |= addAll(result[nonterminal], result[prod.terms[i]]);
              }
            }
          }
        }
      }

    } while (changed);


    /* Output results. */
    for (const auto& entry: result) {
      cout << "FOLLOW(" << entry.first << ") = { ";
      for (const auto& follower: entry.second) {
        cout << follower << " ";
      }
      cout << "}" << endl;
    }

    return result;
  }

  string actionTable(const Grammar& g, const SetToIndex& setToIndex, const IndexToSet& indexToSet) {
    ostringstream result;

    auto follow = followSets(g);
    
    /* Generate the action table. */
    for (size_t i = 0; i < indexToSet.size(); i++) {
      set<Production> curr = indexToSet.at(i);
      
      /* Action table: symbol -> action. */
      map<string, string> actions;
      
      /* Who owns each slot in the table. */
      map<string, Production> owners;
      
      /* Fill in reduce items. */
      for (Production r: reductionsIn(curr)) {
        for (string symbol: follow.at(r.nonterminal)) {
          if (!actions.count(symbol)) {
            actions[symbol] = reduceActionFor(r);
            owners[symbol] = r;
          } else {
            cerr << "Reduce/reduce conflict in state " << i << endl;
            cerr << "Current reduce action: " << owners[symbol] << endl;
            cerr << "Other reduce action:   " << r << endl;
            cerr << endl;
          }
        }
      }
      
      /* Fill in halting items. */
      for (Production r: haltsIn(curr)) {
        for (string symbol: follow.at(r.nonterminal)) {
          if (!actions.count(symbol)) {
            actions[symbol] = "new HaltAction()";
            owners[symbol] = r;
          } else {
            cerr << "Reduce/reduce conflict in state " << i << endl;
            cerr << "Current reduce action: " << owners[symbol] << endl;
            cerr << "Other reduce action:   " << r << endl;
            cerr << endl;
          }
        }
      }
      
      /* Fill in shift items. */
      map<string, set<Production>> successors = successorsOf(g, curr);
      for (Production s: shiftsIn(curr)) {
        string symbol = s.items[s.index];
        
        string command = "new ShiftAction{" + to_string(setToIndex.at(successors.at(symbol))) + "}";
      
        /* Take this one if either (1) it's unclaimed, (2) it's already going where we want to
         * go, or (3) we have equal or higher priority.
         */
        if (!actions.count(symbol) || actions[symbol] == command || priorityOf(g, owners[symbol]) >= priorityOf(g, s)) {
          actions[symbol] = command;
          owners[symbol] = s;
        }
      }
      
      /* Generate the table. */
      result << "{" << endl;
      for (const auto& entry: actions) {
        result << "  {";
        if (isNonterminal(g, entry.first)) {
          result << "    Nonterminal::" << entry.first;
        } else {
          result << "    TokenType::" << entry.first;
        }
        result << ", " << entry.second << " }," << endl;
      }
      result << "}," << endl;
    }
    
    return result.str();
  }

  string reduceFunctions(const Grammar& g, const IndexToSet& indexToSet) {
    /* Map from functions to any one production associated with that function. */
    map<string, Production> functions;
    for (size_t i = 0; i < indexToSet.size(); i++) {
      set<Production> curr = indexToSet.at(i);
      for (Production r: reductionsIn(curr)) {
        /* Don't generate something we don't need. */
        if (g.nonterminalTypes.count(r.nonterminal)) {
          functions[reduceFunctionFor(g, r)] = r;
        }
      }
    }
    
    /* For each function, execute the appropriate action. */
    ostringstream result;
    for (const auto& entry: functions) {
      result << "  " << entry.first << " {" << endl;
      /* Declare a variable to hold the result. */
      result << "    " << typeFor(g, entry.second.nonterminal) << " _parserArg0;" << endl;
      
      /* Run user code. */
      result << "    " << codeFor(g, entry.second) << endl;
      
      /* Return the result. */
      result << "    return _parserArg0;" << endl;
      result << "  }" << endl;
      result << endl;
    }
    
    return result.str();
  }

  string reducePrototypes(const Grammar& g, const IndexToSet& indexToSet) {
    set<string> functions;
    for (size_t i = 0; i < indexToSet.size(); i++) {
      set<Production> curr = indexToSet.at(i);
      for (Production r: reductionsIn(curr)) {
        /* Don't generate something we don't need. */
        if (g.nonterminalTypes.count(r.nonterminal)) {
          functions.insert(reduceFunctionFor(g, r));
        }
      }
    }
    
    ostringstream result;
    for (string fn: functions) {
      result << "  " << fn << ";" << endl;
    }
    
    return result.str();
  }

  string reduceThunks(const Grammar& g, const IndexToSet& indexToSet) {
    set<string> functions;
    for (size_t i = 0; i < indexToSet.size(); i++) {
      set<Production> curr = indexToSet.at(i);
      for (Production r: reductionsIn(curr)) {
        functions.insert(reduceThunkFor(g, r));
      }
    }
    
    ostringstream result;
    for (string fn: functions) {
      result << fn << endl;
    }
    
    return result.str();
  }

  string nonterminals(const Grammar& g) {
    ostringstream result;
    for (const auto& entry: g.grammar) {
      result << "    " << entry.first << "," << endl;
    }
    return result.str();
  }

  string auxEntries(const Grammar& g) {
    ostringstream result;
    for (const auto& entry: g.typeToField) {
      result << "    " << entry.first << " " << entry.second << ";" << endl;
    }
    return result.str();
  }

  string parserReturn(const Grammar& g) {
    return g.nonterminalTypes.at(kStartSymbol);
  }

  string headerExtras(const Grammar& g) {
    ostringstream result;
    for (const auto& entry: g.headerExtras) {
      result << entry << endl;
    }
    return result.str();
  }

  string verbose(const Grammar& g) {
    return g.verbose? "true" : "false";
  }

  string contentsOf(const string& filename) {
    ifstream input(filename);
    if (!input) throw runtime_error("Cannot open " + filename + " for reading.");
    
    ostringstream result;
    result << input.rdbuf();
    return result.str();
  }

  void writeToFile(const string& filename, const string& contents) {
    ofstream output(filename);
    
    if (output << contents, !output) throw runtime_error("Couldn't write to file " + filename);
  }

  /* Does template substitutions in a .template file to generate the resulting
   * file.
   */
  void outputReplaced(const Grammar& g,
                      const string& templateFile,
                      const string& outputFile,
                      const SetToIndex& setToIndex,
                      const IndexToSet& indexToSet) {
    string result = contentsOf(templateFile);
    result = replace(result, "%% Nonterminals %%", nonterminals(g));
    result = replace(result, "%% Aux Entries %%", auxEntries(g));
    result = replace(result, "%% Action Table %%", actionTable(g, setToIndex, indexToSet));
    result = replace(result, "%% Reduce Prototypes %%", reducePrototypes(g, indexToSet));
    result = replace(result, "%% Reduce Thunks %%", reduceThunks(g, indexToSet));
    result = replace(result, "%% Reduce Functions %%", reduceFunctions(g, indexToSet));
    result = replace(result, "%% Parser Return %%", parserReturn(g));
    result = replace(result, "%% Header Extras %%", headerExtras(g));
    result = replace(result, "%% Verbose %%", verbose(g));
    result = replace(result, "%% Return Field %%", fieldNameFor(g, kStartSymbol));
    result = replace(result, "%% Parser Name %%", g.name);
    writeToFile(outputFile, result);
  }

  /* Outputs the generated configurating sets. */
  void printConfiguratingSets(const IndexToSet& indexToSet) {
    for (size_t i = 0; i < indexToSet.size(); i++) {
      cout << "State (" << i << ")" << endl;
      for (const auto& entry: indexToSet.at(i)) {
        cout << "\t" << entry << endl;
      }
      cout << endl;
    }
  }

  /* Given a stream containing a parser configuration, parses that configuration into
   * a Grammar object.
   */
  Grammar parseGrammar(istream& input) {
    JSON data = JSON::parse(input);

    Grammar result;

    /* Extract the grammar. */
    for (const auto& nonterminal: data["grammar"]) {
      if (nonterminal.asString() == kStartSymbol) {
        throw runtime_error("Oops - the nonterminal name " + kStartSymbol + " is reserved.");
      }

      /* Each nonterminal is associated with an array of productions. */
      for (const auto& production: data["grammar"][nonterminal]) {
        ProductionRule prod;
        prod.action = production["code"].asString();

        for (const auto& term: production["production"]) {
          prod.terms.push_back(term.asString());
        }

        result.grammar[nonterminal.asString()].push_back(prod);
      }
    }

    /* Read the start symbol. */
    string startSymbol = data["start-symbol"].asString();
    if (!result.grammar.count(startSymbol)) {
      throw runtime_error("Start symbol has no productions.");
    }

    /* Generate a grammar entry for this start symbol. */
    result.grammar[kStartSymbol] = {
      {{ startSymbol }, "This won't be generated."}
    };

    /* Output the grammar. */
    for (const auto& entry: result.grammar) {
      for (const auto& production: entry.second) {
        cout << entry.first << " -> ";
        copy(production.terms.begin(), production.terms.end(), ostream_iterator<string>(cout, " "));
        cout << endl;
      }
    }

    /* Get the priority ordering. */
    for (const auto& term: data["priorities"]) {
      result.priorities.push_back(term.asString());
    }

    /* Nonterminal types. */
    for (const auto& nonterminal: data["nonterminal-types"]) {
      string symbol = nonterminal.asString();
      string type   = data["nonterminal-types"][nonterminal].asString();
      result.nonterminalTypes[symbol] = type;

      /* If we don't know this yet, give it a new name. */
      if (!result.typeToField.count(type)) {
        result.typeToField.insert({ type, "field" + to_string(result.typeToField.size())});
      }
    }

    /* Add in a nonterminal type entry for the start symbol. */
    if (!result.nonterminalTypes.count(startSymbol)) {
      throw runtime_error("No type associated with start symbol.");
    }
    result.nonterminalTypes[kStartSymbol] = result.nonterminalTypes.at(startSymbol);

    /* Header extras. */
    for (const auto& line: data["header-extras"]) {
      result.headerExtras.push_back(line.asString());
    }

    /* Verbose mode. */
    result.verbose = data["verbose"].asBoolean();

    /* Parser name. */
    result.name = data["parser-name"].asString();

    return result;
  }
}

void generateParser(istream& input) {
  Grammar g = parseGrammar(input);

  pair<SetToIndex, IndexToSet> sets = generateConfiguratingSets(g);
  printConfiguratingSets(sets.second);
  
  outputReplaced(g, "res/Parser.cpp.template", g.name + "Parser.cpp", sets.first, sets.second);
  outputReplaced(g, "res/Parser.h.template",   g.name + "Parser.h",   sets.first, sets.second);
}

void generateParser(const string& filename) {
  ifstream input(filename);
  if (!input) throw runtime_error("Cannot open configuration file " + filename);

  return generateParser(input);
}
