#include "core/inference_utils.h"

#include <algorithm>

namespace
{
    constexpr double kQ31Scale = 2147483648.0;
}

int vuprs::client::inferenceArgMax(const QVector<qint32> &values)
{
    if (values.isEmpty())
        return -1;
    int best = 0;
    for (int i = 1; i < values.size(); ++i)
    {
        if (values.at(i) > values.at(best))
            best = i;
    }
    return best;
}

QVector<double> vuprs::client::normalizeInferencePercent(const QVector<qint32> &values)
{
    QVector<double> percentages;
    if (values.isEmpty())
        return percentages;

    percentages.resize(values.size());
    if (values.size() == 1)
    {
        percentages[0] = 100.0;
        return percentages;
    }

    const auto range = std::minmax_element(values.cbegin(), values.cend());
    const double minimum = static_cast<double>(*range.first);
    const double maximum = static_cast<double>(*range.second);
    if (maximum <= minimum)
    {
        percentages.fill(0.0);
        return percentages;
    }

    const double span = maximum - minimum;
    for (int i = 0; i < values.size(); ++i)
        percentages[i] = (static_cast<double>(values.at(i)) - minimum) * 100.0 / span;
    return percentages;
}

double vuprs::client::inferenceQ31ToDouble(qint32 value)
{
    return static_cast<double>(value) / kQ31Scale;
}
