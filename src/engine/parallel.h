#pragma once

#include <algorithm>
#include <thread>
#include <vector>

namespace fg {

// Runs fn(y0, y1) over [0, rows) split across all cores.
template <typename Fn>
void parallelRows(int rows, Fn fn) {
    const int nThreads = std::max(1u, std::thread::hardware_concurrency());
    const int chunk = (rows + nThreads - 1) / nThreads;
    std::vector<std::thread> pool;
    for (int i = 0; i < nThreads; ++i) {
        const int y0 = i * chunk, y1 = std::min(rows, y0 + chunk);
        if (y0 < y1) pool.emplace_back(fn, y0, y1);
    }
    for (auto& t : pool) t.join();
}

}  // namespace fg
