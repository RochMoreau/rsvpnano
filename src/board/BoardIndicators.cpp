#include "board/BoardIndicators.h"

// Default for boards without status lights. Boards that have them provide their own definition
// and leave this file out of their build (see ports/nextui/CMakeLists.txt).
namespace Board::Indicators {

    void show(const State& state) {
        (void) state;
    }

} // namespace Board::Indicators
