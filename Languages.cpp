#include "Languages.h"
#include "Utilities/Unicode.h"
#include <algorithm>
#include <sstream>
using namespace std;

namespace Languages {
    bool isSubsetOf(const Alphabet& lhs, const Alphabet& rhs) {
        return all_of(lhs.begin(), lhs.end(), [&](char32_t ch) {
            return rhs.count(ch);
        });
    }

    Alphabet toAlphabet(const string& chars) {
        istringstream in(chars);
        Languages::Alphabet result;
        while (in.peek() != EOF) {
            result.insert(readChar(in));
        }
        return result;
    }
}
