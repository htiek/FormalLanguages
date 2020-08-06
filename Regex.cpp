#include "Regex.h"
#include "RegexScanner.h"
#include "Utilities.h"
#include "Utilities/Unicode.h"
#include <typeinfo>
#include <sstream>
#include <unordered_map>
#include <limits>
using namespace std;

namespace Regex {
    /* Visitor implementations. */
    void ASTNode::accept(Visitor &) {
        Utilities::error("Unknown ASTNode type: " + string(typeid(*this).name()));
    }
    void Character::accept(Visitor& v) {
        v.visit(this);
    }
    void Sigma::accept(Visitor& v) {
        v.visit(this);
    }
    void Epsilon::accept(Visitor& v) {
        v.visit(this);
    }
    void EmptySet::accept(Visitor& v) {
        v.visit(this);
    }
    void Union::accept(Visitor& v) {
        v.visit(this);
    }
    void Concat::accept(Visitor& v) {
        v.visit(this);
    }
    void Star::accept(Visitor& v) {
        v.visit(this);
    }
    void Plus::accept(Visitor& v) {
        v.visit(this);
    }
    void Question::accept(Visitor& v) {
        v.visit(this);
    }
    void Power::accept(Visitor& v) {
        v.visit(this);
    }

    /* Walker implementation. */
    void Walker::visit(Character* expr) {
        handle(expr);
    }
    void Walker::visit(Sigma* expr) {
        handle(expr);
    }
    void Walker::visit(Epsilon* expr) {
        handle(expr);
    }
    void Walker::visit(EmptySet* expr) {
        handle(expr);
    }
    void Walker::visit(Union* expr) {
        handle(expr);
        expr->left->accept(*this);
        expr->right->accept(*this);
    }
    void Walker::visit(Concat* expr) {
        handle(expr);
        expr->left->accept(*this);
        expr->right->accept(*this);
    }
    void Walker::visit(Star* expr) {
        handle(expr);
        expr->expr->accept(*this);
    }
    void Walker::visit(Question* expr) {
        handle(expr);
        expr->expr->accept(*this);
    }
    void Walker::visit(Power* expr) {
        handle(expr);
        expr->expr->accept(*this);
    }
    void Walker::visit(Plus* expr) {
        handle(expr);
        expr->expr->accept(*this);
    }

    /* Output. */
    namespace {
        /* Map from numerals to their superscript equivalents. */
        const unordered_map<char, string> kSuperscripts = {
            { '0', "⁰" },
            { '1', "¹" },
            { '2', "²" },
            { '3', "³" },
            { '4', "⁴" },
            { '5', "⁵" },
            { '6', "⁶" },
            { '7', "⁷" },
            { '8', "⁸" },
            { '9', "⁹" },
        };

        /* Utility function to convert a number into a superscript
         * string.
         */
        string toSuperscript(size_t value) {
            string result;
            for (char ch: std::to_string(value)) {
                result += kSuperscripts.at(ch);
            }
            return result;
        }

        /* Returns the operator predecedence of the given expression.
         * Lower numbers mean lower precedence.
         * Atomic expressions have the highest precedence.
         */
        int precedenceOf(ASTNode* r) {
            struct PrecChecker: public Visitor {
                int result = numeric_limits<int>::max();

                void visit(Union *) override {
                    result = 0;
                }
                void visit(Concat *) override {
                    result = 1;
                }
                void visit(Star *) override {
                    result = 2;
                }
                void visit(Plus *) override {
                    result = 2;
                }
                void visit(Question *) override {
                    result = 2;
                }
                void visit(Power *) override {
                    result = 2;
                }
            } calculator;

            r->accept(calculator);
            return calculator.result;
        }
        int precedenceOf(Regex r) {
            return precedenceOf(r.get());
        }
    }

    ostream& operator<< (ostream& out, const Regex& r) {
        struct Printer: public Visitor {
            ostream& out;
            Printer(ostream& out) : out(out) {}

            void visit(Character* node) override {
                if (isSpecialChar(node->ch)) out << "\\";
                out << toUTF8(node->ch);
            }
            void visit(Sigma *) override {
                out << "Σ";
            }
            void visit(Epsilon *) override {
                out << "ε";
            }
            void visit(EmptySet *) override {
                out << "Ø";
            }
            void visit(Union* node) override {
                /* We never need parentheses, because union
                 * is the lowest-level operator.
                 */
                node->left->accept(*this);
                out << " ∪ ";
                node->right->accept(*this);
            }
            void visit(Concat* node) override {
                bool shouldParenthesizeLeft  = precedenceOf(node->left)  < precedenceOf(node);
                bool shouldParenthesizeRight = precedenceOf(node->right) < precedenceOf(node);

                if (shouldParenthesizeLeft) out << "(";
                node->left->accept(*this);
                if (shouldParenthesizeLeft) out << ")";
                if (shouldParenthesizeRight) out << "(";
                node->right->accept(*this);
                if (shouldParenthesizeRight) out << ")";
            }
            void visit(Star* node) override {
                bool shouldParenthesize = precedenceOf(node->expr) < precedenceOf(node);

                if (shouldParenthesize) out << "(";
                node->expr->accept(*this);
                if (shouldParenthesize) out << ")";
                out << "*";
            }
            void visit(Plus* node) override {
                bool shouldParenthesize = precedenceOf(node->expr) < precedenceOf(node);

                if (shouldParenthesize) out << "(";
                node->expr->accept(*this);
                if (shouldParenthesize) out << ")";
                out << "+";
            }
            void visit(Question* node) override {
                bool shouldParenthesize = precedenceOf(node->expr) < precedenceOf(node);

                if (shouldParenthesize) out << "(";
                node->expr->accept(*this);
                if (shouldParenthesize) out << ")";
                out << "?";
            }
            void visit(Power* node) override {
                bool shouldParenthesize = precedenceOf(node->expr) < precedenceOf(node);

                if (shouldParenthesize) out << "(";
                node->expr->accept(*this);
                if (shouldParenthesize) out << ")";
                out << toSuperscript(node->repeats);
            }
        };

        ostringstream builder;
        Printer p(builder);
        r->accept(p);
        return out << builder.str();
    }

    /* Alphabet checking. */
    Languages::Alphabet coreAlphabetOf(Regex r) {
        struct Checker: public Walker {
            Languages::Alphabet used;

            void handle(Character* expr) override {
                used.insert(expr->ch);
            }
        };

        Checker c;
        r->accept(c);
        return c.used;
    }

    /* "Desugars" a regex into one that uses just the basic core operators. */
    Regex desugar(Regex regex, const Languages::Alphabet& alphabet) {
        struct Desugarer: public Calculator<Regex> {
            Languages::Alphabet alphabet;
            Desugarer(Languages::Alphabet alphabet) : alphabet(alphabet) {}

            Regex handle(Character* c) override {
                return make_shared<Character>(c->ch);
            }
            Regex handle(Epsilon*) override {
                return make_shared<Epsilon>();
            }
            Regex handle(EmptySet*) override {
                return make_shared<EmptySet>();
            }
            Regex handle(Sigma*) override {
                /* Return a union of many possible characters. */
                Regex result = make_shared<EmptySet>();
                for (char32_t ch: alphabet) {
                    result = make_shared<Union>(result, make_shared<Character>(ch));
                }
                return result;
            }
            Regex handle(Union*, Regex left, Regex right) override {
                return make_shared<Union>(left, right);
            }
            Regex handle(Concat*, Regex left, Regex right) override {
                return make_shared<Concat>(left, right);
            }
            Regex handle(Star*, Regex child) override {
                return make_shared<Star>(child);
            }
            Regex handle(Plus*, Regex child) override {
                return make_shared<Concat>(child, make_shared<Star>(child));
            }
            Regex handle(Question*, Regex child) override {
                return make_shared<Union>(child, make_shared<Epsilon>());
            }
            Regex handle(Power* p, Regex child) override {
                Regex result = make_shared<Epsilon>();
                for (size_t i = 0; i < p->repeats; i++) {
                    result = make_shared<Concat>(result, child);
                }
                return result;
            }
        };

        return Desugarer(alphabet).calculate(regex);
    }
}
