#pragma once

#include <string>

namespace coord {

/// Read the ordinary Coo source of the standard annotation library. The source
/// lives in `lib/standard/annotations.coo`; `COORD_STDLIB_DIR` (environment or
/// build-time define) selects the directory. Returns an empty string and fills
/// `error` when the file cannot be read.
[[nodiscard]] std::string standardAnnotationsSource(
    std::string* error = nullptr);

/// Read the ordinary Coo source of the standard utility library
/// (`lib/standard/utility.coo`): the stable utility entry point. Callable
/// interfaces live in `lambda.coo`.
[[nodiscard]] std::string standardUtilitySource(std::string* error = nullptr);

/// Read the ordinary Coo source of the standard callable-interface library
/// (`lib/standard/lambda.coo`): Function, Consumer, Predicate, Supplier,
/// BiFunction, Action and ElseAction, all declared with abstract `operator()`.
[[nodiscard]] std::string standardLambdaSource(std::string* error = nullptr);

}  // namespace coord
