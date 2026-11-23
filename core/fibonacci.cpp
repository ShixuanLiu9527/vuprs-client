#include "core/fibonacci.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double PI = 3.14159265358979323846;

    double Clamp(double value, double lower, double upper)
    {
        return value < lower ? lower : (value > upper ? upper : value);
    }
}

QVector<vuprs::client::GridPoint> vuprs::client::fibonacciGrid(int pointsHalf, double altMinDeg)
{
    QVector<GridPoint> result;
    if (pointsHalf <= 0 || !std::isfinite(altMinDeg))
        return result;

    const double phi = (std::sqrt(5.0) - 1.0) / 2.0;
    const int n_total = 2 * pointsHalf + 1;
    const int start_n = (n_total + 1) / 2;
    const int result_size = n_total - start_n + 1;
    result.reserve(result_size);

    for (int i = 0; i < result_size; i++)
    {
        const int n = start_n + i;
        const double z = 2.0 * static_cast<double>(n) / static_cast<double>(n_total) - 1.0;
        const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double x = radius * std::cos(2.0 * PI * static_cast<double>(n) * phi);
        const double y = radius * std::sin(2.0 * PI * static_cast<double>(n) * phi);
        const double alt = std::asin(Clamp(z, -1.0, 1.0)) * 180.0 / PI;
        if (alt < altMinDeg)
            continue;
        const double az = std::atan2(x, y) * 180.0 / PI;
        result.push_back(GridPoint{alt, az});
    }
    return result;
}
