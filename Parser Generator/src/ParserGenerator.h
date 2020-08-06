#ifndef ParserGenerator_Included
#define ParserGenerator_Included

#include <istream>
#include <string>

/* Generates a parser given an input stream containing the JSON configuration
 * defining the grammar.
 *
 * To define a grammar, you'll need to create a JSON object with the following
 * fields:
 *
 * grammar: the value should be an object with one key per nonterminal. Each
 *          nonterminal should then be associated with a list of the productions
 *          associated with that nonterminal. Each production is an object with
 *          two fields: "production," which contains a comma-separated list of
 *          strings, one per symbol in the production, and "code", which contains
 *          the semantic action to execute when that production is used. That
 *          code should use bison-style actions; for example, to set the value
 *          associated with the nonterminal, use an expression like "$$ = ...",
 *          with $1 referring to the first symbol, $2 the second, etc. Each
 *          nonterminal's associated data will have a type specified later in
 *          the JSON file, and each terminal's associated data will be the string
 *          associated with that token.
 *
 * start-symbol: The start symbol for the grammar.
 *
 * priorities: List of terminal symbols in their order of priority. Earlier symbols
 *             have higher priority than later symbols. We assume right-associativity
 *             for all operators; we'll fix that when we need to. :-)
 *
 * nonterminal-types: The C++ variable types to be associated with each nonterminal. This
 *                    should be an object where each key is a nonterminal and each value
 *                    is the C++ type of to associate with that terminal.
 *
 * header-extras: List of extra lines of C++ code to include at the top of the generated
 *                parser's .h file. Useful for #include-ing files defining types that
 *                you'll need.
 *
 * verbose: Whether the parser should be in verbose mode (show every shift / reduce). Set to
 *          true when testing, false for release.
 *
 * parser-name: An affix to attach to the name of the generated .cpp and .h files and the
 *              name of the parser routine itself. For example, setting this to "FOL" would
 *              generate FOLParser.h and FOLParser.cpp, with functions named parseFOL defined
 *              in those files.
 */
void generateParser(std::istream& input);

/* Generates a parser given the name of a configuration file. */
void generateParser(const std::string& filename);

#endif
