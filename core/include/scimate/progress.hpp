// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <functional>
#include <string>
namespace scimate {
struct OperationProgress {
    std::string phase;
    std::size_t completed = 0, total = 0;
    std::string detail;
};
// Invoked synchronously on the calling/worker thread. Counts are per phase;
// zero total denotes an unmeasured phase.
using ProgressCallback = std::function<void(const OperationProgress&)>;
}
