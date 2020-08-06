#include "RegexParser.h"
#include "RegexScanner.h"
#include <queue>
#include <stack>
#include <vector>
#include <map>
#include <functional>
#include <iostream>
using namespace std;

#define PARSER_IS_VERBOSE (false)

namespace Regex {
    namespace {
      template <typename T> T popFrom(stack<T>& s) {
        if (s.empty()) throw runtime_error("Pop from empty stack.");
        T result = std::move(s.top());
        s.pop();
        return result;
      }

      enum class Nonterminal {
        ATOMEXPR,
    CONCATEXPR,
    OREXPR,
    STAREXPR,
    _parserInternalStart,

      };

      /* Type representing an item that can be on the parsing stack (either a
       * terminal or a nonterminal).
       */
      struct Symbol {
        bool isToken;
        union {
          TokenType   tokenType;
          Nonterminal nonterminalType;
        };

        Symbol(TokenType t) {
          isToken = true;
          tokenType = t;
        }
        Symbol(Nonterminal t) {
          isToken = false;
          nonterminalType = t;
        }
      };

      bool operator< (const Symbol& lhs, const Symbol& rhs) {
        if (lhs.isToken != rhs.isToken) return lhs.isToken;
        if (lhs.isToken) return lhs.tokenType < rhs.tokenType;
        return lhs.nonterminalType < rhs.nonterminalType;
      }

      /* Type representing auxiliary data to store in each stack entry. */
      struct AuxData {
        std::shared_ptr<ASTNode> field0;

      };

      /* Type representing data aggregated so far. */
      struct StackData {
        Token     token;  // Only active if the item is a terminal
        AuxData   data;   // Only active if the item is a nonterminal
      };

      /* Type representing an item on the stack. */
      struct StackItem {
        size_t    state;
        StackData data;
      };

      /* Base type for actions. */
      struct Action {
        virtual ~Action() = default;
      };

      struct ShiftAction: Action {
        size_t target;

        ShiftAction(int target) : target(target) {}
      };

      struct HaltAction: Action {

      };

      struct ReduceAction: Action {
        Nonterminal reduceTo;

        /* Does the reduction. */
        virtual void reduce(stack<StackItem>& s) = 0;

        ReduceAction(Nonterminal t) : reduceTo(t) {}
      };

      /* Helper template functions that fire off callbacks with the arguments
       * expanded out into a list.
       */
      template <size_t N> struct DoReduction {
        template <typename Callback, typename... Args>
        static AuxData invoke(stack<StackItem>& s, Callback c, const Args&... args) {
          /* Build arguments up in reverse order, since items are coming off of the stack! */
          auto nextArg = popFrom(s);
          return DoReduction<N - 1>::invoke(s, c, nextArg.data, args...);
        }
      };
      template <> struct DoReduction<0> {
        template <typename Callback, typename... Args>
        static AuxData invoke(stack<StackItem>&, Callback c, const Args&... args) {
          return c(args...);
        }
      };

      /* Utility metafunction that maps from a number N to a callback type that accepts
       * N arguments.
       */
      template <size_t M> struct CallbackType {
        template <size_t N, typename... Args> struct Helper {
          using type = typename Helper<N - 1, Args..., StackData>::type;
        };
        template <typename... Args> struct Helper<0, Args...> {
          using type = std::function<AuxData (Args...)>;
        };
        using type = typename Helper<M>::type;
      };

      /* Use template system so we know how many arguments to forward. */
      template <size_t N> struct ReduceActionN: ReduceAction {
        typename CallbackType<N>::type callback; // Function to invoke with reduced items

        void reduce(stack<StackItem>& s) override;

        ReduceActionN(Nonterminal n, typename CallbackType<N>::type c) : ReduceAction(n), callback(c) {}
      };

      /* Unused argument type. */
      struct _unused_ {};

      std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_CHARACTER(const std::string& _parserArg1);
  std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_EMPTYSET(const std::string&);
  std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_EPSILON(const std::string&);
  std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN(const std::string&, std::shared_ptr<ASTNode> _parserArg2, const std::string&);
  std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_SIGMA(const std::string&);
  std::shared_ptr<ASTNode> reduce_CONCATEXPR_from_STAREXPR(std::shared_ptr<ASTNode> _parserArg1);
  std::shared_ptr<ASTNode> reduce_CONCATEXPR_from_STAREXPR_CONCATEXPR(std::shared_ptr<ASTNode> _parserArg1, std::shared_ptr<ASTNode> _parserArg2);
  std::shared_ptr<ASTNode> reduce_OREXPR_from_CONCATEXPR(std::shared_ptr<ASTNode> _parserArg1);
  std::shared_ptr<ASTNode> reduce_OREXPR_from_CONCATEXPR_UNION_OREXPR(std::shared_ptr<ASTNode> _parserArg1, const std::string&, std::shared_ptr<ASTNode> _parserArg3);
  std::shared_ptr<ASTNode> reduce_STAREXPR_from_ATOMEXPR(std::shared_ptr<ASTNode> _parserArg1);
  std::shared_ptr<ASTNode> reduce_STAREXPR_from_STAREXPR_PLUS(std::shared_ptr<ASTNode> _parserArg1, const std::string&);
  std::shared_ptr<ASTNode> reduce_STAREXPR_from_STAREXPR_POWER_NUMBER(std::shared_ptr<ASTNode> _parserArg1, const std::string&, const std::string& _parserArg3);
  std::shared_ptr<ASTNode> reduce_STAREXPR_from_STAREXPR_QUESTION(std::shared_ptr<ASTNode> _parserArg1, const std::string&);
  std::shared_ptr<ASTNode> reduce_STAREXPR_from_STAREXPR_STAR(std::shared_ptr<ASTNode> _parserArg1, const std::string&);


      AuxData reduce_ATOMEXPR_from_CHARACTER__thunk(StackData a0) {
    AuxData result;
    result.field0 = reduce_ATOMEXPR_from_CHARACTER(a0.token.data);
    return result;
  }

  AuxData reduce_ATOMEXPR_from_EMPTYSET__thunk(StackData a0) {
    AuxData result;
    result.field0 = reduce_ATOMEXPR_from_EMPTYSET(a0.token.data);
    return result;
  }

  AuxData reduce_ATOMEXPR_from_EPSILON__thunk(StackData a0) {
    AuxData result;
    result.field0 = reduce_ATOMEXPR_from_EPSILON(a0.token.data);
    return result;
  }

  AuxData reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk(StackData a0, StackData a1, StackData a2) {
    AuxData result;
    result.field0 = reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN(a0.token.data, a1.data.field0, a2.token.data);
    return result;
  }

  AuxData reduce_ATOMEXPR_from_SIGMA__thunk(StackData a0) {
    AuxData result;
    result.field0 = reduce_ATOMEXPR_from_SIGMA(a0.token.data);
    return result;
  }

  AuxData reduce_CONCATEXPR_from_STAREXPR_CONCATEXPR__thunk(StackData a0, StackData a1) {
    AuxData result;
    result.field0 = reduce_CONCATEXPR_from_STAREXPR_CONCATEXPR(a0.data.field0, a1.data.field0);
    return result;
  }

  AuxData reduce_CONCATEXPR_from_STAREXPR__thunk(StackData a0) {
    AuxData result;
    result.field0 = reduce_CONCATEXPR_from_STAREXPR(a0.data.field0);
    return result;
  }

  AuxData reduce_OREXPR_from_CONCATEXPR_UNION_OREXPR__thunk(StackData a0, StackData a1, StackData a2) {
    AuxData result;
    result.field0 = reduce_OREXPR_from_CONCATEXPR_UNION_OREXPR(a0.data.field0, a1.token.data, a2.data.field0);
    return result;
  }

  AuxData reduce_OREXPR_from_CONCATEXPR__thunk(StackData a0) {
    AuxData result;
    result.field0 = reduce_OREXPR_from_CONCATEXPR(a0.data.field0);
    return result;
  }

  AuxData reduce_STAREXPR_from_ATOMEXPR__thunk(StackData a0) {
    AuxData result;
    result.field0 = reduce_STAREXPR_from_ATOMEXPR(a0.data.field0);
    return result;
  }

  AuxData reduce_STAREXPR_from_STAREXPR_PLUS__thunk(StackData a0, StackData a1) {
    AuxData result;
    result.field0 = reduce_STAREXPR_from_STAREXPR_PLUS(a0.data.field0, a1.token.data);
    return result;
  }

  AuxData reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk(StackData a0, StackData a1, StackData a2) {
    AuxData result;
    result.field0 = reduce_STAREXPR_from_STAREXPR_POWER_NUMBER(a0.data.field0, a1.token.data, a2.token.data);
    return result;
  }

  AuxData reduce_STAREXPR_from_STAREXPR_QUESTION__thunk(StackData a0, StackData a1) {
    AuxData result;
    result.field0 = reduce_STAREXPR_from_STAREXPR_QUESTION(a0.data.field0, a1.token.data);
    return result;
  }

  AuxData reduce_STAREXPR_from_STAREXPR_STAR__thunk(StackData a0, StackData a1) {
    AuxData result;
    result.field0 = reduce_STAREXPR_from_STAREXPR_STAR(a0.data.field0, a1.token.data);
    return result;
  }



      /* Action table. */
      const vector<map<Symbol, Action*>> kActionTable = {
      {
  {    Nonterminal::ATOMEXPR, new ShiftAction{17} },
  {    TokenType::CHARACTER, new ShiftAction{16} },
  {    Nonterminal::CONCATEXPR, new ShiftAction{13} },
  {    TokenType::EMPTYSET, new ShiftAction{12} },
  {    TokenType::EPSILON, new ShiftAction{11} },
  {    TokenType::LPAREN, new ShiftAction{8} },
  {    Nonterminal::OREXPR, new ShiftAction{19} },
  {    TokenType::SIGMA, new ShiftAction{3} },
  {    Nonterminal::STAREXPR, new ShiftAction{1} },
},
{
  {    Nonterminal::ATOMEXPR, new ShiftAction{17} },
  {    TokenType::CHARACTER, new ShiftAction{16} },
  {    Nonterminal::CONCATEXPR, new ShiftAction{18} },
  {    TokenType::EMPTYSET, new ShiftAction{12} },
  {    TokenType::EPSILON, new ShiftAction{11} },
  {    TokenType::LPAREN, new ShiftAction{8} },
  {    TokenType::PLUS, new ShiftAction{7} },
  {    TokenType::POWER, new ShiftAction{5} },
  {    TokenType::QUESTION, new ShiftAction{4} },
  {    TokenType::RPAREN, new ReduceActionN<1>(Nonterminal::CONCATEXPR, reduce_CONCATEXPR_from_STAREXPR__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<1>(Nonterminal::CONCATEXPR, reduce_CONCATEXPR_from_STAREXPR__thunk) },
  {    TokenType::SIGMA, new ShiftAction{3} },
  {    TokenType::STAR, new ShiftAction{2} },
  {    Nonterminal::STAREXPR, new ShiftAction{1} },
  {    TokenType::UNION, new ReduceActionN<1>(Nonterminal::CONCATEXPR, reduce_CONCATEXPR_from_STAREXPR__thunk) },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::PLUS, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::POWER, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::STAR, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
  {    TokenType::UNION, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_STAR__thunk) },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::PLUS, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::POWER, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::STAR, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
  {    TokenType::UNION, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_SIGMA__thunk) },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::PLUS, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::POWER, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::STAR, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
  {    TokenType::UNION, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_QUESTION__thunk) },
},
{
  {    TokenType::NUMBER, new ShiftAction{6} },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::PLUS, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::POWER, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::STAR, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
  {    TokenType::UNION, new ReduceActionN<3>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_POWER_NUMBER__thunk) },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::PLUS, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::POWER, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::STAR, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
  {    TokenType::UNION, new ReduceActionN<2>(Nonterminal::STAREXPR, reduce_STAREXPR_from_STAREXPR_PLUS__thunk) },
},
{
  {    Nonterminal::ATOMEXPR, new ShiftAction{17} },
  {    TokenType::CHARACTER, new ShiftAction{16} },
  {    Nonterminal::CONCATEXPR, new ShiftAction{13} },
  {    TokenType::EMPTYSET, new ShiftAction{12} },
  {    TokenType::EPSILON, new ShiftAction{11} },
  {    TokenType::LPAREN, new ShiftAction{8} },
  {    Nonterminal::OREXPR, new ShiftAction{9} },
  {    TokenType::SIGMA, new ShiftAction{3} },
  {    Nonterminal::STAREXPR, new ShiftAction{1} },
},
{
  {    TokenType::RPAREN, new ShiftAction{10} },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::PLUS, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::POWER, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::STAR, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
  {    TokenType::UNION, new ReduceActionN<3>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN__thunk) },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::PLUS, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::POWER, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::STAR, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
  {    TokenType::UNION, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EPSILON__thunk) },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::PLUS, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::POWER, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::STAR, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
  {    TokenType::UNION, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_EMPTYSET__thunk) },
},
{
  {    TokenType::RPAREN, new ReduceActionN<1>(Nonterminal::OREXPR, reduce_OREXPR_from_CONCATEXPR__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<1>(Nonterminal::OREXPR, reduce_OREXPR_from_CONCATEXPR__thunk) },
  {    TokenType::UNION, new ShiftAction{14} },
},
{
  {    Nonterminal::ATOMEXPR, new ShiftAction{17} },
  {    TokenType::CHARACTER, new ShiftAction{16} },
  {    Nonterminal::CONCATEXPR, new ShiftAction{13} },
  {    TokenType::EMPTYSET, new ShiftAction{12} },
  {    TokenType::EPSILON, new ShiftAction{11} },
  {    TokenType::LPAREN, new ShiftAction{8} },
  {    Nonterminal::OREXPR, new ShiftAction{15} },
  {    TokenType::SIGMA, new ShiftAction{3} },
  {    Nonterminal::STAREXPR, new ShiftAction{1} },
},
{
  {    TokenType::RPAREN, new ReduceActionN<3>(Nonterminal::OREXPR, reduce_OREXPR_from_CONCATEXPR_UNION_OREXPR__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<3>(Nonterminal::OREXPR, reduce_OREXPR_from_CONCATEXPR_UNION_OREXPR__thunk) },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::PLUS, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::POWER, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::STAR, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
  {    TokenType::UNION, new ReduceActionN<1>(Nonterminal::ATOMEXPR, reduce_ATOMEXPR_from_CHARACTER__thunk) },
},
{
  {    TokenType::CHARACTER, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::EMPTYSET, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::EPSILON, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::LPAREN, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::PLUS, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::POWER, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::QUESTION, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::RPAREN, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::SIGMA, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::STAR, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
  {    TokenType::UNION, new ReduceActionN<1>(Nonterminal::STAREXPR, reduce_STAREXPR_from_ATOMEXPR__thunk) },
},
{
  {    TokenType::RPAREN, new ReduceActionN<2>(Nonterminal::CONCATEXPR, reduce_CONCATEXPR_from_STAREXPR_CONCATEXPR__thunk) },
  {    TokenType::SCAN_EOF, new ReduceActionN<2>(Nonterminal::CONCATEXPR, reduce_CONCATEXPR_from_STAREXPR_CONCATEXPR__thunk) },
  {    TokenType::UNION, new ReduceActionN<2>(Nonterminal::CONCATEXPR, reduce_CONCATEXPR_from_STAREXPR_CONCATEXPR__thunk) },
},
{
  {    TokenType::SCAN_EOF, new HaltAction() },
},

      };

      template <size_t N>
        void ReduceActionN<N>::reduce(stack<StackItem>& s) {
          #if PARSER_IS_VERBOSE
            cout << "  Should pop " << N << " items from the stack." << endl;
            cout << "    Before: " << s.size() << endl;
          #endif
          /* Invoke the callback on the proper arguments, getting the AuxData
           * to push onto the stack.
           */
          auto nextItem = DoReduction<N>::invoke(s, callback);
          #if PARSER_IS_VERBOSE
            cout << "    After: " << s.size() << endl;
          #endif

          int state = s.top().state;
          #if PARSER_IS_VERBOSE
            cout << "  Top state is " << state << endl;
          #endif

          /* Look up the shift destination for this nonterminal. */
          auto* action = static_cast<ShiftAction*>(kActionTable[state].at(reduceTo));
          #if PARSER_IS_VERBOSE
            cout << "  Should shift to state " << action->target << endl;
          #endif

          /* Push this onto the stack. */
          s.push({ action->target, { { }, nextItem } });
      }

      /* Internal parsing routine */
      AuxData parseInternal(queue<Token>& tokens) {
        stack<StackItem> s;

        /* Seed the stack with the initial state. */
        s.push({ 0, {} });

        /* Run the parser! */
        while (!tokens.empty()) {
          /* Look at the next token. We only consume it in a shift. */
          auto curr  = tokens.front();
          int  state = s.top().state;

          #if PARSER_IS_VERBOSE
            cout << "Current state: " << state << endl;
            cout << "  Symbol: " << curr.data << endl;
            cout << endl;
          #endif

          /* Look up the action to take; if there is no action, it's an error. */
          if (!kActionTable[state].count(curr.type)) {
            if (curr.type == TokenType::SCAN_EOF) {
              throw runtime_error("End of formula encountered unexpectedly. (Are you missing a close parenthesis?)");
            }
            throw runtime_error("Found \"" + to_string(curr) + "\" where it wasn't expected.");
          }

          /* See what action to take. */
          auto action = kActionTable[state].at(curr.type);
          if (auto* shift = dynamic_cast<ShiftAction*>(action)) {
            #if PARSER_IS_VERBOSE
              cout << "  Action: Shift to " << shift->target << endl;
            #endif
            s.push( { shift->target, {curr, {}} } ); // No special data.
            tokens.pop();
          } else if (auto* reduce = dynamic_cast<ReduceAction*>(action)) {
            #if PARSER_IS_VERBOSE
              cout << "  Action: Reduce" << endl;
            #endif
            reduce->reduce(s);
          } else if (dynamic_cast<HaltAction*>(action)) {
            #if PARSER_IS_VERBOSE
              cout << "  Action: Halt" << endl;
            #endif
            return s.top().data.data;
          } else {
            throw runtime_error("Unknown action.");
          }
        }

        throw runtime_error("Out of tokens, but parser hasn't finished.");
      }

      std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_CHARACTER(const std::string& _parserArg1) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<Character>(fromUTF8(_parserArg1));
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_EMPTYSET(const std::string&) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<EmptySet>();
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_EPSILON(const std::string&) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<Epsilon>();
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_LPAREN_OREXPR_RPAREN(const std::string&, std::shared_ptr<ASTNode> _parserArg2, const std::string&) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = _parserArg2;
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_ATOMEXPR_from_SIGMA(const std::string&) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<Sigma>();
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_CONCATEXPR_from_STAREXPR(std::shared_ptr<ASTNode> _parserArg1) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = _parserArg1;
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_CONCATEXPR_from_STAREXPR_CONCATEXPR(std::shared_ptr<ASTNode> _parserArg1, std::shared_ptr<ASTNode> _parserArg2) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<Concat>(_parserArg1, _parserArg2);
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_OREXPR_from_CONCATEXPR(std::shared_ptr<ASTNode> _parserArg1) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = _parserArg1;
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_OREXPR_from_CONCATEXPR_UNION_OREXPR(std::shared_ptr<ASTNode> _parserArg1, const std::string&, std::shared_ptr<ASTNode> _parserArg3) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<Union>(_parserArg1, _parserArg3);
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_STAREXPR_from_ATOMEXPR(std::shared_ptr<ASTNode> _parserArg1) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = _parserArg1;
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_STAREXPR_from_STAREXPR_PLUS(std::shared_ptr<ASTNode> _parserArg1, const std::string&) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<Plus>(_parserArg1);
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_STAREXPR_from_STAREXPR_POWER_NUMBER(std::shared_ptr<ASTNode> _parserArg1, const std::string&, const std::string& _parserArg3) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<Power>(_parserArg1, stoi(_parserArg3));
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_STAREXPR_from_STAREXPR_QUESTION(std::shared_ptr<ASTNode> _parserArg1, const std::string&) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<Question>(_parserArg1);
    return _parserArg0;
  }

  std::shared_ptr<ASTNode> reduce_STAREXPR_from_STAREXPR_STAR(std::shared_ptr<ASTNode> _parserArg1, const std::string&) {
    std::shared_ptr<ASTNode> _parserArg0;
    _parserArg0 = make_shared<Star>(_parserArg1);
    return _parserArg0;
  }


    }

    /* Public parsing routine. */
    std::shared_ptr<ASTNode> parse(queue<Token>& q) {
      return parseInternal(q).field0;
    }
    std::shared_ptr<ASTNode> parse(queue<Token>&& q) {
      return parseInternal(q).field0;
    }
}
