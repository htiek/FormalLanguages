#pragma once

#include "Languages.h"
#include <string>
#include <memory>
#include <ostream>

namespace Regex {
    /* Visitor type. */
    class Visitor;

    /* Base AST type. */
    class ASTNode {
    public:
        virtual ~ASTNode() = default;
        virtual void accept(Visitor&);

    protected:
        ASTNode() = default;
    };

    /* Convenience type. */
    using Regex = std::shared_ptr<ASTNode>;

    /* Atomic expressions. */
    class Character: public ASTNode {
    public:
        Character(char32_t ch) : ch(ch) {}
        virtual void accept(Visitor &) override;

        char32_t ch;
    };

    class Sigma: public ASTNode {
    public:
        virtual void accept(Visitor &) override;
    };

    class Epsilon: public ASTNode {
    public:
        virtual void accept(Visitor &) override;
    };

    class EmptySet: public ASTNode {
    public:
        virtual void accept(Visitor &) override;
    };

    /* Compound expressions. */
    class Union: public ASTNode {
    public:
        Union(std::shared_ptr<ASTNode> left, std::shared_ptr<ASTNode> right) : left(left), right(right) {}
        virtual void accept(Visitor &) override;

        std::shared_ptr<ASTNode> left, right;
    };

    class Concat: public ASTNode {
    public:
        Concat(std::shared_ptr<ASTNode> left, std::shared_ptr<ASTNode> right) : left(left), right(right) {}
        virtual void accept(Visitor &) override;

        std::shared_ptr<ASTNode> left, right;
    };

    class Star: public ASTNode {
    public:
        Star(std::shared_ptr<ASTNode> expr) : expr(expr) {}
        virtual void accept(Visitor &) override;

        std::shared_ptr<ASTNode> expr;
    };

    class Plus: public ASTNode {
    public:
        Plus(std::shared_ptr<ASTNode> expr) : expr(expr) {}
        virtual void accept(Visitor &) override;

        std::shared_ptr<ASTNode> expr;
    };

    class Question: public ASTNode {
    public:
        Question(std::shared_ptr<ASTNode> expr) : expr(expr) {}
        virtual void accept(Visitor &) override;

        std::shared_ptr<ASTNode> expr;
    };

    class Power: public ASTNode {
    public:
        Power(std::shared_ptr<ASTNode> expr, size_t repeats) : expr(expr), repeats(repeats) {}
        virtual void accept(Visitor &) override;

        std::shared_ptr<ASTNode> expr;
        size_t repeats;
    };

    /* Visitor and walker types. */
    class Visitor {
    public:
        virtual ~Visitor() = default;

        virtual void visit(Character *) {}
        virtual void visit(Sigma *) {}  // Hehehe, sigma star. :-)
        virtual void visit(Epsilon *) {}
        virtual void visit(EmptySet *) {}
        virtual void visit(Union *) {}
        virtual void visit(Concat *) {}
        virtual void visit(Star *) {}   // Hahaha, star star. :-)
        virtual void visit(Question *) {}
        virtual void visit(Plus *) {}
        virtual void visit(Power *) {}
    };

    /* Convenience type that does a preorder walk of an AST. */
    class Walker: public Visitor {
    public:
        virtual void visit(Character *) override final;
        virtual void visit(Sigma *) override final;
        virtual void visit(Epsilon *) override final;
        virtual void visit(EmptySet *) override final;
        virtual void visit(Union *) override final;
        virtual void visit(Concat *) override final;
        virtual void visit(Star *) override final;
        virtual void visit(Question *) override final;
        virtual void visit(Plus *) override final;
        virtual void visit(Power *) override final;

    protected:
        virtual void handle(ASTNode *) {}
        virtual void handle(Character *) {}
        virtual void handle(Sigma *) {}
        virtual void handle(Epsilon *) {}
        virtual void handle(EmptySet *) {}
        virtual void handle(Union *) {}
        virtual void handle(Concat *) {}
        virtual void handle(Star *) {}
        virtual void handle(Plus *) {}
        virtual void handle(Question *) {}
        virtual void handle(Power *) {}
    };

    /* Convenience types to calculate values over regexes. */
    template <typename Result> class Calculator: public Visitor {
    public:
        virtual void visit(Character *) override final;
        virtual void visit(Sigma *) override final;
        virtual void visit(Epsilon *) override final;
        virtual void visit(EmptySet *) override final;
        virtual void visit(Union *) override final;
        virtual void visit(Concat *) override final;
        virtual void visit(Star *) override final;
        virtual void visit(Question *) override final;
        virtual void visit(Plus *) override final;
        virtual void visit(Power *) override final;

        Result calculate(Regex regex);

    protected:
        virtual Result handle(Character *) = 0;
        virtual Result handle(Sigma *) = 0;
        virtual Result handle(Epsilon *) = 0;
        virtual Result handle(EmptySet *) = 0;
        virtual Result handle(Union *, Result left, Result right) = 0;
        virtual Result handle(Concat *, Result left, Result right) = 0;
        virtual Result handle(Star *, Result child) = 0;
        virtual Result handle(Question *, Result child) = 0;
        virtual Result handle(Plus *, Result child) = 0;
        virtual Result handle(Power *, Result child) = 0;

    private:
        Result last;
    };

    /* Utility functions on regexes. */
    std::ostream& operator<< (std::ostream& out, const Regex& regex);

    /* The "core" alphabet of a regex is the set of characters explicitly
     * used in a regex. These are characters that absolutely must be in
     * the alphabet of the regex.
     */
    Languages::Alphabet coreAlphabetOf(Regex);

    /* "Desugars" a regex by replacing all syntax sugars (sigma, ?, +, and repeats)
     * with simpler basic regexes.
     */
    Regex desugar(Regex regex, const Languages::Alphabet& alphabet);



    /* * * * * Implementation Below This Point * * * * */
    template <typename Result> void Calculator<Result>::visit(Character* expr) {
        last = handle(expr);
    }
    template <typename Result> void Calculator<Result>::visit(Sigma* expr) {
        last = handle(expr);
    }
    template <typename Result> void Calculator<Result>::visit(Epsilon* expr) {
        last = handle(expr);
    }
    template <typename Result> void Calculator<Result>::visit(EmptySet* expr) {
        last = handle(expr);
    }
    template <typename Result> void Calculator<Result>::visit(Union* expr) {
        /* Explore the left side; this populates last. */
        expr->left->accept(*this);
        Result left = std::move(last);

        /* Explore the right side; this populates last. */
        expr->right->accept(*this);
        Result right = std::move(last);

        /* Compute the result. */
        last = handle(expr, left, right);
    }
    template <typename Result> void Calculator<Result>::visit(Concat* expr) {
        /* Explore the left side; this populates last. */
        expr->left->accept(*this);
        Result left = std::move(last);

        /* Explore the right side; this populates last. */
        expr->right->accept(*this);
        Result right = std::move(last);

        /* Compute the result. */
        last = handle(expr, left, right);
    }
    template <typename Result> void Calculator<Result>::visit(Star* expr) {
        /* Explore the child; this populates last. */
        expr->expr->accept(*this);
        Result child = std::move(last);

        /* Compute the result. */
        last = handle(expr, child);
    }
    template <typename Result> void Calculator<Result>::visit(Plus* expr) {
        /* Explore the child; this populates last. */
        expr->expr->accept(*this);
        Result child = std::move(last);

        /* Compute the result. */
        last = handle(expr, child);
    }
    template <typename Result> void Calculator<Result>::visit(Question* expr) {
        /* Explore the child; this populates last. */
        expr->expr->accept(*this);
        Result child = std::move(last);

        /* Compute the result. */
        last = handle(expr, child);
    }
    template <typename Result> void Calculator<Result>::visit(Power* expr) {
        /* Explore the child; this populates last. */
        expr->expr->accept(*this);
        Result child = std::move(last);

        /* Compute the result. */
        last = handle(expr, child);
    }
    template <typename Result> Result Calculator<Result>::calculate(Regex regex) {
        regex->accept(*this);
        return std::move(last);
    }
}
