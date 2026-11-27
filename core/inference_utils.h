#ifndef VUPRS_CLIENT_INFERENCE_UTILS_H
#define VUPRS_CLIENT_INFERENCE_UTILS_H

#include <QtGlobal>
#include <QVector>

namespace vuprs
{
    namespace client
    {
        /**
         * @brief Index of the maximum Q31 tensor value, or -1 for an empty tensor.
         */
        int inferenceArgMax(const QVector<qint32> &values);

        /**
         * @brief Map a Q31 tensor to display percentages in [0, 100].
         *
         * The mapping is per-frame min-max normalization, so the smallest value is
         * 0% and the largest value is 100%. A single-element tensor is 100%. If all
         * values are equal, every bar is 0%.
         */
        QVector<double> normalizeInferencePercent(const QVector<qint32> &values);

        /**
         * @brief Decode one Q31 value to its normalized [-1, 1] range.
         */
        double inferenceQ31ToDouble(qint32 value);
    }
}

#endif
