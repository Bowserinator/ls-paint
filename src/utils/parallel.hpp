#pragma once

#include <cstddef>
#include <functional>

namespace lsp {

// Runs `work(i)` for every i below `count` across the machine's cores and
// returns when all are done. The calling thread takes part, so a call made from
// inside `work` cannot stall waiting for a free worker.
void parallel_for(std::size_t count, const std::function<void(std::size_t)>& work);

}  // namespace lsp
