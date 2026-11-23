#include "record/vbf_csv.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include "record/vbf_reader.h"

bool vuprs::client::exportVbfToCsv(const QString &vbfPath,
                                   const QString &csvPath,
                                   QString *error,
                                   const VbfCsvProgress &progress,
                                   VbfCsvExportResult *result)
{
    if (error != nullptr)
        error->clear();

    VbfReader reader;
    if (!reader.open(vbfPath, error))
        return false;

    QSaveFile file(csvPath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error != nullptr)
            *error = QStringLiteral("无法写入 %1: %2").arg(csvPath, file.errorString());
        return false;
    }

    const auto fail = [&file, error](const QString &message)
    {
        file.cancelWriting();
        if (error != nullptr)
            *error = message;
        return false;
    };

    QByteArray header;
    header += "# VUPRS BF CSV\n";
    header += "# source=" + QFileInfo(vbfPath).absoluteFilePath().toUtf8() + "\n";
    header += "# exported_utc=" + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + "\n";
    header += "# metadata=" + QJsonDocument(reader.metadata()).toJson(QJsonDocument::Compact) + "\n";
    header += "frame_index,sequence,timestamp_ns,arrival_ms,sample_rate_hz,sample_index,global_sample,"
              "time_s,global_time_s,amplitude,raw_q16_16,inference_identity,inference_valid\n";
    if (file.write(header) != header.size())
        return fail(QStringLiteral("写入 CSV 失败: %1").arg(file.errorString()));
    if (progress && !progress(0, reader.fileSize()))
        return fail(QStringLiteral("已取消"));

    QByteArray block;
    block.reserve(1024 * 1024);
    quint64 frame_index = 0;
    quint64 global_sample = 0;
    double global_time_s = 0.0;

    const auto flush = [&]()
    {
        if (block.isEmpty())
            return true;
        if (file.write(block) != block.size())
        {
            block.clear();
            return fail(QStringLiteral("写入 CSV 失败: %1").arg(file.errorString()));
        }
        block.clear();
        if (progress && !progress(reader.position(), reader.fileSize()))
            return fail(QStringLiteral("已取消"));
        return true;
    };

    BeamFrame frame;
    VbfReader::Result read_result = VbfReader::Result::End;
    while ((read_result = reader.next(&frame, error)) == VbfReader::Result::Frame)
    {
        const double sample_rate_hz = frame.sampleRateHz();
        const double sample_period_s = sample_rate_hz > 0.0 ? 1.0 / sample_rate_hz : 0.0;
        const QByteArray rate_text = QByteArray::number(sample_rate_hz, 'f', 3);
        const QByteArray sequence_text = QByteArray::number(static_cast<qulonglong>(frame.sequence));
        const QByteArray timestamp_text = QByteArray::number(static_cast<qulonglong>(frame.timestampNs));
        const QByteArray arrival_text = QByteArray::number(static_cast<qlonglong>(frame.arrivalMs));
        const QByteArray identity_text = QByteArray::number(static_cast<qlonglong>(frame.inferenceIdentity));
        const char valid_text = frame.inferenceValid ? '1' : '0';

        for (int i = 0; i < frame.samples.size(); ++i)
        {
            const qint32 raw = frame.samples.at(i);
            block += QByteArray::number(static_cast<qulonglong>(frame_index));
            block += ',';
            block += sequence_text;
            block += ',';
            block += timestamp_text;
            block += ',';
            block += arrival_text;
            block += ',';
            block += rate_text;
            block += ',';
            block += QByteArray::number(i);
            block += ',';
            block += QByteArray::number(static_cast<qulonglong>(global_sample));
            block += ',';
            block += QByteArray::number(static_cast<double>(i) * sample_period_s, 'f', 9);
            block += ',';
            block += QByteArray::number(global_time_s + static_cast<double>(i) * sample_period_s, 'f', 9);
            block += ',';
            block += QByteArray::number(static_cast<double>(raw) / 65536.0, 'f', 9);
            block += ',';
            block += QByteArray::number(static_cast<qlonglong>(raw));
            block += ',';
            block += identity_text;
            block += ',';
            block += valid_text;
            block += '\n';
            ++global_sample;
            if (block.size() >= 1024 * 1024 && !flush())
                return false;
        }
        global_time_s += static_cast<double>(frame.samples.size()) * sample_period_s;
        ++frame_index;
    }

    if (read_result == VbfReader::Result::Error)
    {
        if (error != nullptr && error->isEmpty())
            *error = QStringLiteral("读取 .vbf 失败");
        file.cancelWriting();
        return false;
    }
    if (!flush())
        return false;
    if (progress && !progress(reader.fileSize(), reader.fileSize()))
        return fail(QStringLiteral("已取消"));
    if (!file.commit())
    {
        if (error != nullptr)
            *error = QStringLiteral("保存 CSV 失败: %1").arg(file.errorString());
        return false;
    }

    if (result != nullptr)
    {
        result->frames = reader.statistics().frameCount;
        result->rows = global_sample;
        result->truncated = reader.statistics().truncated;
        result->hasFooter = reader.statistics().hasFooter;
    }
    return true;
}
