#ifndef VUPRS_CLIENT_PARAMETER_LIMITS_H
#define VUPRS_CLIENT_PARAMETER_LIMITS_H

#include <QJsonObject>

namespace vuprs
{
    namespace client
    {
        /**
         * @brief Limits reported by the server in the ack response.
         *
         * The default values match the current server defaults. When the server
         * does not publish a limits object, these defaults are used and
         * fromServer stays false.
         */
        struct ServerLimits
        {
            double minSamplingFrequencyHz = 10.0;
            double maxSamplingFrequencyHz = 120000.0;
            double minFrequencyHz = 10.0;
            double maxFrequencyHz = 60000.0;
            int fftPoints = 512;
            int dmaBufferPoints = 8192;
            int dmaBufferCount = 20;
            int firLength = 256;
            double inferenceFrameTimeMs = 15.0;
            bool inferenceEnabled = false;
            int maxScanPoints = 10000;
            double maxWaveVelocity = 100000.0;
            bool fromServer = false;
        };

        /**
         * @brief Signal-processing values derived from the current parameters.
         */
        struct ParameterMetrics
        {
            double frequencyResolutionHz = 0.0;
            int bandBinCount = 0;
            double snapshotTimeSeconds = 0.0;
            double blockPeriodSeconds = 0.0;
            int inferenceFrameSamples = 0;
            bool inferenceBufferTooShort = false;
            double receiveQueueSeconds = 0.0;
            double recordingQueueSeconds = 0.0;
        };

        ServerLimits parseServerLimits(const QJsonObject &limits);

        ParameterMetrics computeParameterMetrics(const ServerLimits &limits,
                                                 double fs,
                                                 double lowerHz,
                                                 double upperHz,
                                                 double snapshotWindow,
                                                 int beamQueueFrames,
                                                 int recordingQueueFrames);
    }
}

#endif
