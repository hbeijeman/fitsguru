#pragma once

#include <vector>

#include "stretch.h"

namespace fg {

// Level 0 is the input; each further level is a 2x2 box-filtered half-size
// copy, down to the first level whose larger side is <= minSize.
std::vector<DisplayBuffer> buildPyramid(DisplayBuffer base, int minSize = 128);

}  // namespace fg
