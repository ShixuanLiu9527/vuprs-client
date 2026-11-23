#ifndef VUPRS_CLIENT_FIBONACCI_H
#define VUPRS_CLIENT_FIBONACCI_H

#include <QVector>

namespace vuprs
{
    namespace client
    {
        struct GridPoint
        {
            double alt = 0.0;
            double az = 0.0;
        };

        /**
         * @brief Same Fibonacci hemisphere grid as vuprs-server.
         */
        QVector<GridPoint> fibonacciGrid(int pointsHalf, double altMinDeg);
    }
}

#endif
