#pragma once

#include <QString>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace mn::bench {

inline double median(std::vector<double> v)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

inline double percentile(std::vector<double> v, double p)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const std::size_t i =
        std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size() - 1) + 0.5));
    return v[i];
}

// One result line: "<metric> <value> <unit>  (target T, hard limit H)".
inline void report(const char* metric, double value, const char* unit, double target = -1, double limit = -1)
{
    if (target >= 0) {
        const char* verdict = value <= target ? "ok" : (value <= limit ? "over target" : "OVER LIMIT");
        std::printf("%-40s %10.2f %-3s  (target %g, limit %g: %s)\n", metric, value, unit, target, limit,
                    verdict);
    } else {
        std::printf("%-40s %10.2f %s\n", metric, value, unit);
    }
    std::fflush(stdout);
}

} // namespace mn::bench
