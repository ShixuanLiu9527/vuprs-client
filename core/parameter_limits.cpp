#include "core/parameter_limits.h"

#include <algorithm>
#include <cmath>
#include <QJsonValue>

namespace
{
    double ReadDouble(const QJsonObject &object, const char *key, double fallback)
    {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (value.isDouble() && std::isfinite(value.toDouble()))
            return value.toDouble();
        if (value.isString())
        {
            bool ok = false;
            const double parsed = value.toString().toDouble(&ok);
            if (ok && std::isfinite(parsed))
                return parsed;
        }
        return fallback;
    }

    int ReadInt(const QJsonObject &object, const char *key, int fallback)
    {
        const double value = ReadDouble(object, key, static_cast<double>(fallback));
        if (!std::isfinite(value) || value < 0.0 || value > 2147483647.0)
            return fallback;
        return static_cast<int>(std::llround(value));
    }

    bool ReadBool(const QJsonObject &object, const char *key, bool fallback)
    {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (value.isBool())
            return value.toBool();
        if (value.isDouble())
            return value.toDouble() != 0.0;
        if (value.isString())
        {
            const QString text = value.toString().trimmed().toLower();
            if (text == QStringLiteral("1") || text == QStringLiteral("true") ||
                text == QStringLiteral("yes") || text == QStringLiteral("on"))
                return true;
            if (text == QStringLiteral("0") || text == QStringLiteral("false") ||
                text == QStringLiteral("no") || text == QStringLiteral("off"))
                return false;
        }
        return fallback;
    }
}

vuprs::client::ServerLimits vuprs::client::parseServerLimits(const QJsonObject &limits)
{
    ServerLimits parsed;
    if (limits.isEmpty())
        return parsed;

    parsed.fromServer = true;
    parsed.minSamplingFrequencyHz = ReadDouble(limits, "min_fs", parsed.minSamplingFrequencyHz);
    parsed.maxSamplingFrequencyHz = ReadDouble(limits, "max_fs", parsed.maxSamplingFrequencyHz);
    parsed.minFrequencyHz = ReadDouble(limits, "min_frequency", parsed.minFrequencyHz);
    parsed.maxFrequencyHz = ReadDouble(limits, "max_frequency", parsed.maxFrequencyHz);
    parsed.fftPoints = ReadInt(limits, "fft_points", parsed.fftPoints);
    parsed.dmaBufferPoints = ReadInt(limits, "dma_buffer_points", parsed.dmaBufferPoints);
    parsed.dmaBufferCount = ReadInt(limits, "dma_buffer_count", parsed.dmaBufferCount);
    parsed.firLength = ReadInt(limits, "fir_length", parsed.firLength);
    parsed.inferenceFrameTimeMs = ReadDouble(limits, "inference_frame_time_ms", parsed.inferenceFrameTimeMs);
    parsed.inferenceEnabled = ReadBool(limits, "inference_enabled", parsed.inferenceEnabled);
    parsed.maxScanPoints = ReadInt(limits, "max_scan_points", parsed.maxScanPoints);
    parsed.maxWaveVelocity = ReadDouble(limits, "max_wave_velocity", parsed.maxWaveVelocity);

    if (!(parsed.minSamplingFrequencyHz > 0.0) ||
        !(parsed.maxSamplingFrequencyHz > parsed.minSamplingFrequencyHz))
    {
        parsed.minSamplingFrequencyHz = 10.0;
        parsed.maxSamplingFrequencyHz = 120000.0;
    }
    if (!(parsed.minFrequencyHz > 0.0) || !(parsed.maxFrequencyHz > parsed.minFrequencyHz))
    {
        parsed.minFrequencyHz = 10.0;
        parsed.maxFrequencyHz = parsed.maxSamplingFrequencyHz / 2.0;
    }
    if (parsed.fftPoints <= 0)
        parsed.fftPoints = 512;
    if (parsed.dmaBufferPoints <= 0)
        parsed.dmaBufferPoints = 8192;
    if (parsed.dmaBufferCount <= 0)
        parsed.dmaBufferCount = 20;
    if (parsed.firLength <= 0)
        parsed.firLength = 256;
    if (!(parsed.inferenceFrameTimeMs >= 0.0))
        parsed.inferenceFrameTimeMs = 15.0;
    if (parsed.maxScanPoints <= 0)
        parsed.maxScanPoints = 10000;
    if (!(parsed.maxWaveVelocity > 0.0))
        parsed.maxWaveVelocity = 100000.0;

    return parsed;
}

vuprs::client::ParameterMetrics vuprs::client::computeParameterMetrics(
    const ServerLimits &limits,
    double fs,
    double lowerHz,
    double upperHz,
    double snapshotWindow,
    int beamQueueFrames,
    int recordingQueueFrames)
{
    ParameterMetrics metrics;
    if (!(fs > 0.0) || !(limits.fftPoints > 0))
        return metrics;

    metrics.frequencyResolutionHz = fs / static_cast<double>(limits.fftPoints);
    if (metrics.frequencyResolutionHz > 0.0 && upperHz > lowerHz)
    {
        const double low_index = std::ceil(lowerHz / metrics.frequencyResolutionHz);
        const double high_index = std::floor(upperHz / metrics.frequencyResolutionHz);
        const double count = high_index - low_index + 1.0;
        metrics.bandBinCount = count > 0.0
                                   ? static_cast<int>(std::min(count, 2147483647.0))
                                   : 0;
    }

    metrics.snapshotTimeSeconds = snapshotWindow * static_cast<double>(limits.fftPoints) / fs;
    metrics.blockPeriodSeconds = static_cast<double>(limits.dmaBufferPoints) / fs;

    if (limits.inferenceEnabled && limits.inferenceFrameTimeMs > 0.0)
    {
        metrics.inferenceFrameSamples = static_cast<int>(2.0 * std::floor(
            0.5 * (limits.inferenceFrameTimeMs / 1000.0) * fs));
        metrics.inferenceBufferTooShort =
            metrics.inferenceFrameSamples > 0 &&
            limits.dmaBufferPoints < metrics.inferenceFrameSamples;
    }

    metrics.receiveQueueSeconds =
        static_cast<double>(std::max(0, beamQueueFrames)) * metrics.blockPeriodSeconds;
    metrics.recordingQueueSeconds =
        static_cast<double>(std::max(0, recordingQueueFrames)) * metrics.blockPeriodSeconds;
    return metrics;
}
