#pragma once

#include <string>

namespace coord {

/// Read the ordinary Coo source of the standard annotation library. The source
/// lives in `lib/standard/annotations.coo`; `COORD_STDLIB_DIR` (environment or
/// build-time define) selects the directory. Returns an empty string and fills
/// `error` when the file cannot be read.
[[nodiscard]] std::string standardAnnotationsSource(
    std::string* error = nullptr);

}  // namespace coord
