/* Ideas and concepts common to formal language theory. */
#pragma once
#include <string>
#include <set>

namespace Languages {
    /* An alphabet is a set of characters. Stored ordered for convenience. */
    using Alphabet = std::set<char32_t>;

    bool isSubsetOf(const Alphabet& lhs, const Alphabet& rhs);
    Alphabet toAlphabet(const std::string& alphaChars);
    std::string toString(const Alphabet& alphabet);
}
