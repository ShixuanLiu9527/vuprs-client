#include <QtTest>
#include <cmath>
#include <QDir>
#include <QHostAddress>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QFile>
#include <QJsonObject>
#include "core/app_config.h"
#include "core/beam_frame.h"
#include "core/fibonacci.h"
#include "core/frame_protocol.h"
#include "core/inference_utils.h"
#include "core/parameter_limits.h"
#include "core/waveform_buffer.h"
#include "net/control_client.h"
#include "record/recording_naming.h"
#include "record/vbf_csv.h"
#include "record/vbf_format.h"
#include "record/vbf_reader.h"

namespace
{
    void PutU16(QByteArray &bytes, int offset, quint16 value)
    {
        bytes[offset] = static_cast<char>(value & 0xFF);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    }

    void PutU32(QByteArray &bytes, int offset, quint32 value)
    {
        bytes[offset] = static_cast<char>(value & 0xFF);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
        bytes[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
        bytes[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
    }

    void PutU64(QByteArray &bytes, int offset, quint64 value)
    {
        for (int i = 0; i < 8; i++)
            bytes[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }

    QByteArray MakeFrame(vuprs::client::MessageType type,
                         quint32 requestId,
                         quint64 sequence,
                         quint64 timestampNs,
                         const QByteArray &payload)
    {
        QByteArray frame(40 + payload.size(), Qt::Uninitialized);
        std::memcpy(frame.data(), "VUPR", 4);
        PutU16(frame, 4, vuprs::client::PROTOCOL_VERSION);
        PutU16(frame, 6, static_cast<quint16>(type));
        PutU32(frame, 8, 0);
        PutU32(frame, 12, static_cast<quint32>(payload.size()));
        PutU32(frame, 16, requestId);
        PutU32(frame, 20, 0);
        PutU64(frame, 24, sequence);
        PutU64(frame, 32, timestampNs);
        std::memcpy(frame.data() + 40, payload.constData(), static_cast<size_t>(payload.size()));
        return frame;
    }

    QByteArray MakeBfPayload(quint32 sampleRateMilliHz, const QVector<qint32> &samples)
    {
        QByteArray payload(32 + samples.size() * 4, '\0');
        PutU32(payload, 0, sampleRateMilliHz);
        PutU32(payload, 4, static_cast<quint32>(samples.size()));
        PutU32(payload, 8, 1);
        for (int i = 0; i < samples.size(); i++)
            PutU32(payload, 32 + i * 4, static_cast<quint32>(samples[i]));
        return payload;
    }

    QByteArray MakeInferencePayload(const QVector<qint32> &values)
    {
        QByteArray payload(32 + values.size() * 4, '\0');
        PutU32(payload, 0, static_cast<quint32>(values.size()));
        PutU32(payload, 4, 1);
        for (int i = 0; i < values.size(); i++)
            PutU32(payload, 32 + i * 4, static_cast<quint32>(values[i]));
        return payload;
    }

    QByteArray MakeScanPayload(quint32 pointsHalf, const QVector<quint16> &values)
    {
        QByteArray payload(32 + values.size() * 2, Qt::Uninitialized);
        PutU32(payload, 0, pointsHalf);
        PutU32(payload, 4, static_cast<quint32>(values.size()));
        PutU32(payload, 8, 10000000);
        PutU32(payload, 12, 15000);
        PutU32(payload, 16, static_cast<quint32>(-30000));
        PutU32(payload, 20, 12000);
        PutU32(payload, 24, 1);
        for (int i = 0; i < values.size(); i++)
            PutU16(payload, 32 + i * 2, values[i]);
        return payload;
    }
}

class ClientCoreTest : public QObject
{
    Q_OBJECT

private slots:
    void controlRequestAndParser()
    {
        const QByteArray frame = vuprs::client::encodeControlRequest(42, QStringLiteral("ack"), QJsonObject());
        QCOMPARE(frame.size(), 40 + QJsonDocument(QJsonObject{{"cmd", "ack"}, {"params", QJsonObject()}}).toJson(QJsonDocument::Compact).size());

        int frame_count = 0;
        quint16 type = 0;
        quint32 request_id = 0;
        QByteArray payload;
        QString parser_error;
        vuprs::client::FrameParser parser(
            [&](const vuprs::client::FrameHeader &header, const QByteArray &data)
            {
                ++frame_count;
                type = static_cast<quint16>(header.type);
                request_id = header.requestId;
                payload = data;
            },
            [&](const QString &error)
            { parser_error = error; });

        parser.append(frame.left(7));
        QCOMPARE(frame_count, 0);
        parser.append(frame.mid(7, 11));
        QCOMPARE(frame_count, 0);
        parser.append(frame.mid(18));
        QCOMPARE(frame_count, 1);
        QCOMPARE(type, static_cast<quint16>(vuprs::client::MessageType::ControlRequest));
        QCOMPARE(request_id, 42u);
        QVERIFY(parser_error.isEmpty());
        const QJsonObject request = QJsonDocument::fromJson(payload).object();
        QCOMPARE(request.value("cmd").toString(), QStringLiteral("ack"));
    }

    void controlResponse()
    {
        const QByteArray json = R"({"ok":true,"cmd":"start","error":0,"info":"","params":{}})";
        const QByteArray frame = MakeFrame(vuprs::client::MessageType::ControlResponse, 17, 0, 0, json);
        vuprs::client::FrameHeader header;
        QString error;
        QVERIFY(vuprs::client::decodeFrameHeader(frame.left(40), &header, &error));
        vuprs::client::ControlResponse response;
        QVERIFY(vuprs::client::parseControlResponse(header, frame.mid(40), &response, &error));
        QCOMPARE(response.requestId, 17u);
        QCOMPARE(response.cmd, QStringLiteral("start"));
        QVERIFY(response.ok);
        QCOMPARE(response.error, 0);
    }

    void bfAndScanFrames()
    {
        const QVector<qint32> samples = {0x00010000, -65536, 0, 131072};
        const QByteArray payload = MakeBfPayload(10000000, samples);
        const QByteArray frame = MakeFrame(vuprs::client::MessageType::BfData, 0, 7, 123456789, payload);
        vuprs::client::FrameHeader header;
        QString error;
        QVERIFY(vuprs::client::decodeFrameHeader(frame.left(40), &header, &error));
        vuprs::client::BeamFrame beam;
        QVERIFY(vuprs::client::parseBfFrame(header, frame.mid(40), &beam, &error));
        QCOMPARE(beam.sequence, 7u);
        QCOMPARE(beam.sampleRateMilliHz, 10000000u);
        QCOMPARE(beam.samples.size(), 4);
        QCOMPARE(beam.samples[0], 65536);
        QCOMPARE(beam.samples[1], -65536);
        QCOMPARE(beam.inferenceIdentity, -1);
        QVERIFY(!beam.inferenceValid);

        const QVector<quint16> values = {0, 32768, 65535};
        const QByteArray scan_payload = MakeScanPayload(70, values);
        const QByteArray scan_frame = MakeFrame(vuprs::client::MessageType::ScanData, 0, 8, 123, scan_payload);
        QVERIFY(vuprs::client::decodeFrameHeader(scan_frame.left(40), &header, &error));
        vuprs::client::ScanFrame scan;
        QVERIFY(vuprs::client::parseScanFrame(header, scan_frame.mid(40), &scan, &error));
        QCOMPARE(scan.pointsHalf, 70u);
        QCOMPARE(scan.dataPoints, 3u);
        QCOMPARE(scan.altMinDeg, 15.0);
        QCOMPARE(scan.values[2], 65535);

        const QVector<qint32> tensor = {1, -2, 3, 4};
        const QByteArray inference_payload = MakeInferencePayload(tensor);
        const QByteArray inference_frame = MakeFrame(vuprs::client::MessageType::InferenceData,
                                                     0, 9, 456, inference_payload);
        QVERIFY(vuprs::client::decodeFrameHeader(inference_frame.left(40), &header, &error));
        vuprs::client::InferenceFrame inference;
        QVERIFY(vuprs::client::parseInferenceFrame(header, inference_frame.mid(40), &inference, &error));
        QCOMPARE(inference.sequence, 9u);
        QCOMPARE(inference.values, tensor);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void serverLimitsAndDerivedMetrics()
    {
        const QJsonObject json{
            {"min_fs", "10.0"},
            {"max_fs", "120000.0"},
            {"min_frequency", "10.0"},
            {"max_frequency", "60000.0"},
            {"fft_points", "512"},
            {"dma_buffer_points", "8192"},
            {"fir_length", "256"},
            {"inference_frame_time_ms", "15.0"},
            {"inference_enabled", "1"},
            {"max_scan_points", "10000"},
            {"max_wave_velocity", "8000.0"}};
        const auto limits = vuprs::client::parseServerLimits(json);
        QVERIFY(limits.fromServer);
        QCOMPARE(limits.fftPoints, 512);
        QCOMPARE(limits.dmaBufferPoints, 8192);
        QCOMPARE(limits.firLength, 256);
        QCOMPARE(limits.maxWaveVelocity, 8000.0);
        QVERIFY(limits.inferenceEnabled);

        const auto at_10k = vuprs::client::computeParameterMetrics(
            limits, 10000.0, 500.0, 3000.0, 200.0, 256, 512);
        QVERIFY(std::abs(at_10k.frequencyResolutionHz - 19.53125) < 1e-9);
        QCOMPARE(at_10k.bandBinCount, 128);
        QVERIFY(std::abs(at_10k.snapshotTimeSeconds - 10.24) < 1e-9);
        QVERIFY(std::abs(at_10k.blockPeriodSeconds - 0.8192) < 1e-9);

        const auto at_100k = vuprs::client::computeParameterMetrics(
            limits, 100000.0, 500.0, 3000.0, 200.0, 256, 512);
        QVERIFY(std::abs(at_100k.frequencyResolutionHz - 195.3125) < 1e-9);
        QCOMPARE(at_100k.bandBinCount, 13);
        QCOMPARE(at_100k.inferenceFrameSamples, 1500);
        QVERIFY(!at_100k.inferenceBufferTooShort);
        QVERIFY(std::abs(at_100k.snapshotTimeSeconds - 1.024) < 1e-9);
        QVERIFY(std::abs(at_100k.blockPeriodSeconds - 0.08192) < 1e-9);
        QVERIFY(std::abs(at_100k.receiveQueueSeconds - 20.97152) < 1e-9);
        QVERIFY(std::abs(at_100k.recordingQueueSeconds - 41.94304) < 1e-9);

        auto short_dma = limits;
        short_dma.dmaBufferPoints = 1024;
        const auto short_metrics = vuprs::client::computeParameterMetrics(
            short_dma, 100000.0, 500.0, 3000.0, 200.0, 256, 512);
        QVERIFY(short_metrics.inferenceBufferTooShort);

        const auto defaults = vuprs::client::parseServerLimits(QJsonObject());
        QVERIFY(!defaults.fromServer);
        QCOMPARE(defaults.fftPoints, 512);
        QCOMPARE(defaults.minSamplingFrequencyHz, 10.0);
    }

    void inferenceNormalization()
    {
        const QVector<qint32> values = {-1, 0, 1};
        const QVector<double> percent = vuprs::client::normalizeInferencePercent(values);
        QCOMPARE(percent.size(), 3);
        QVERIFY(std::abs(percent.at(0) - 0.0) < 1e-9);
        QVERIFY(std::abs(percent.at(1) - 50.0) < 1e-9);
        QVERIFY(std::abs(percent.at(2) - 100.0) < 1e-9);
        QCOMPARE(vuprs::client::inferenceArgMax(values), 2);

        const QVector<double> flat = vuprs::client::normalizeInferencePercent({5, 5});
        QCOMPARE(flat, QVector<double>({0.0, 0.0}));

        const QVector<double> single = vuprs::client::normalizeInferencePercent({5});
        QCOMPARE(single, QVector<double>({100.0}));
        QCOMPARE(vuprs::client::inferenceArgMax({}), -1);
        QVERIFY(std::abs(vuprs::client::inferenceQ31ToDouble(0x40000000) - 0.5) < 1e-9);
    }

    void fibonacciGridCounts()
    {
        QCOMPARE(vuprs::client::fibonacciGrid(100, 15.0).size(), 75);
        QCOMPARE(vuprs::client::fibonacciGrid(70, 15.0).size(), 53);
        QCOMPARE(vuprs::client::fibonacciGrid(100, 0.0).size(), 101);
    }

    void waveformRing()
    {
        vuprs::client::WaveformBuffer buffer;
        buffer.configure(1000.0, 1);
        QCOMPARE(buffer.capacity(), 1024);
        vuprs::client::BeamFrame frame;
        frame.sampleRateMilliHz = 1000000;
        frame.samples.resize(1500);
        for (int i = 0; i < frame.samples.size(); i++)
            frame.samples[i] = i;
        QVERIFY(buffer.append(frame));
        QCOMPARE(buffer.count(), 1024);
        QCOMPARE(buffer.firstSampleIndex(), 476);
        QCOMPARE(buffer.lastSampleIndex(), 1500);
        float value = 0.0f;
        QVERIFY(!buffer.sampleAt(475, &value));
        QVERIFY(buffer.sampleAt(476, &value));
        QCOMPARE(value, static_cast<float>(476) / 65536.0f);
        QVERIFY(buffer.sampleAt(1499, &value));
        QCOMPARE(value, static_cast<float>(1499) / 65536.0f);

        vuprs::client::WaveformBuffer playback;
        QVERIFY(playback.configureSamples(10000.0, 4096));
        QCOMPARE(playback.capacity(), 4096);
        QCOMPARE(playback.sampleRateHz(), 10000.0);
        vuprs::client::BeamFrame playback_frame;
        playback_frame.sampleRateMilliHz = 10000000;
        playback_frame.samples = {32768};
        QVERIFY(playback.append(playback_frame));
        QVERIFY(playback.sampleAt(0, &value));
        QCOMPARE(value, 0.5f);
    }



    void recordingWriterWritesFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("test"));
        vuprs::client::RecordingWriter writer;
        QJsonObject metadata;
        metadata["sample_rate_millihz"] = 10000000;
        QVERIFY2(writer.start(path, metadata, 8), qPrintable(writer.lastError()));
        vuprs::client::BeamFrame frame;
        frame.sequence = 1;
        frame.timestampNs = 111;
        frame.sampleRateMilliHz = 10000000;
        frame.samples = {65536, -65536, 0};
        writer.enqueue(frame);
        frame.sequence = 2;
        frame.timestampNs = 222;
        writer.enqueue(frame);
        writer.stop();
        QVERIFY2(writer.lastError().isEmpty(), qPrintable(writer.lastError()));
        QCOMPARE(writer.framesWritten(), quint64(2));
        const QString file_path = path + QStringLiteral(".vbf");
        QVERIFY(QFile::exists(file_path));
        QFile file(file_path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray all = file.readAll();
        QVERIFY(all.size() > 8);
        QCOMPARE(all.left(8), QByteArray("VUPRSBF1"));
        QCOMPARE(static_cast<quint8>(all.at(all.size() - vuprs::client::VBF_FOOTER_SIZE)),
                 vuprs::client::VBF_RECORD_END);
    }

    void recordingNaming()
    {
        QCOMPARE(vuprs::client::makeTaskId(1700001234), QStringLiteral("1234"));
        QCOMPARE(vuprs::client::makeTaskId(1700000000), QStringLiteral("0000"));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(vuprs::client::nextRecordingIndex(dir.path(), QStringLiteral("1234")), 1);
        QCOMPARE(vuprs::client::recordingFilePath(dir.path(), QStringLiteral("1234"), 1),
                 QDir(dir.path()).filePath(QStringLiteral("task1234_id1.vbf")));

        QFile first(dir.filePath(QStringLiteral("task1234_id1.vbf")));
        QVERIFY(first.open(QIODevice::WriteOnly));
        first.close();
        QFile third_part(dir.filePath(QStringLiteral("task1234_id3.vbf.part")));
        QVERIFY(third_part.open(QIODevice::WriteOnly));
        third_part.close();
        QFile other(dir.filePath(QStringLiteral("task9999_id8.vbf")));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.close();

        QCOMPARE(vuprs::client::nextRecordingIndex(dir.path(), QStringLiteral("1234")), 4);
        QCOMPARE(vuprs::client::nextRecordingIndex(dir.path(), QStringLiteral("1234"), 10), 10);
    }

    void configRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("config/client_config.json"));

        vuprs::client::AppConfig config;
        config.host = QStringLiteral("10.1.2.3");
        config.port = 12345;
        config.connectTimeoutMs = 1500;
        config.commandTimeoutMs = 6000;
        config.historySeconds = 120;
        config.defaultWindowSeconds = 20;
        config.beamQueueFrames = 128;
        config.recordingDirectory = dir.filePath(QStringLiteral("recordings"));
        config.recordingQueueFrames = 256;

        QString error;
        QVERIFY2(vuprs::client::saveAppConfigTo(config, path, &error), qPrintable(error));
        QVERIFY(QFile::exists(path));

        bool created = true;
        const vuprs::client::AppConfig loaded = vuprs::client::loadAppConfigFrom(path, &created, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!created);
        QCOMPARE(loaded.host, config.host);
        QCOMPARE(loaded.port, quint16(12345));
        QCOMPARE(loaded.connectTimeoutMs, 1500);
        QCOMPARE(loaded.commandTimeoutMs, 6000);
        QCOMPARE(loaded.historySeconds, 120);
        QCOMPARE(loaded.defaultWindowSeconds, 20);
        QCOMPARE(loaded.beamQueueFrames, 128);
        QCOMPARE(loaded.recordingDirectory, config.recordingDirectory);
        QCOMPARE(loaded.recordingQueueFrames, 256);

        const QString new_path = dir.filePath(QStringLiteral("new/sub/client_config.json"));
        bool created_new = false;
        const vuprs::client::AppConfig created_config =
            vuprs::client::loadAppConfigFrom(new_path, &created_new, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(created_new);
        QVERIFY(QFile::exists(new_path));
        QCOMPARE(created_config.host, vuprs::client::defaultServerHost());
        QCOMPARE(created_config.port, vuprs::client::defaultServerPort());
        QVERIFY(created_config.historySeconds >= 5);
        QVERIFY(created_config.defaultWindowSeconds >= 1);
    }

    void vbfReaderAndCsvExport()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString vbf_path = dir.filePath(QStringLiteral("readback.vbf"));

        QJsonObject metadata;
        metadata["server"] = "127.0.0.1:18080";
        metadata["sample_rate_millihz"] = 10000000;

        vuprs::client::BeamFrame first;
        first.sequence = 4;
        first.timestampNs = 123;
        first.sampleRateMilliHz = 10000000;
        first.inferenceIdentity = 7;
        first.inferenceValid = true;
        first.samples = {65536, -65536, 32768};

        vuprs::client::BeamFrame second;
        second.sequence = 5;
        second.timestampNs = 456;
        second.sampleRateMilliHz = 10000000;
        second.inferenceIdentity = -1;
        second.inferenceValid = false;
        second.samples = {131072};

        QFile file(vbf_path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(vuprs::client::makeVbfFileHeader(metadata));
        file.write(vuprs::client::makeVbfRecord(first));
        file.write(vuprs::client::makeVbfRecord(second));
        file.write(vuprs::client::makeVbfFooter(2, 16, 456));
        file.close();

        vuprs::client::VbfReader reader;
        QString error;
        QVERIFY2(reader.open(vbf_path, &error), qPrintable(error));
        QCOMPARE(reader.metadata().value("server").toString(), QStringLiteral("127.0.0.1:18080"));

        vuprs::client::BeamFrame frame;
        QVERIFY(reader.next(&frame, &error) == vuprs::client::VbfReader::Result::Frame);
        QCOMPARE(frame.sequence, 4u);
        QCOMPARE(frame.timestampNs, quint64(123));
        QCOMPARE(frame.samples.size(), 3);
        QCOMPARE(frame.samples.at(0), 65536);
        QCOMPARE(frame.samples.at(1), -65536);
        QCOMPARE(frame.inferenceIdentity, 7);
        QVERIFY(frame.inferenceValid);

        QVERIFY(reader.next(&frame, &error) == vuprs::client::VbfReader::Result::Frame);
        QCOMPARE(frame.sequence, 5u);
        QCOMPARE(frame.samples.size(), 1);
        QCOMPARE(frame.samples.at(0), 131072);
        QCOMPARE(frame.inferenceIdentity, -1);
        QVERIFY(!frame.inferenceValid);

        QVERIFY(reader.next(&frame, &error) == vuprs::client::VbfReader::Result::End);
        QVERIFY(error.isEmpty());
        QCOMPARE(reader.statistics().frameCount, quint64(2));
        QCOMPARE(reader.statistics().sampleCount, quint64(4));
        QVERIFY(reader.statistics().hasFooter);
        QVERIFY(!reader.statistics().truncated);

        const QString csv_path = dir.filePath(QStringLiteral("readback.csv"));
        vuprs::client::VbfCsvExportResult result;
        QVERIFY2(vuprs::client::exportVbfToCsv(vbf_path, csv_path, &error,
                                               vuprs::client::VbfCsvProgress(), &result),
                 qPrintable(error));
        QCOMPARE(result.frames, quint64(2));
        QCOMPARE(result.rows, quint64(4));
        QVERIFY(!result.truncated);
        QVERIFY(result.hasFooter);

        QFile csv(csv_path);
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const QByteArray all = csv.readAll();
        QVERIFY(all.contains("frame_index,sequence,timestamp_ns"));
        QVERIFY(all.contains("# source="));
        QVERIFY(all.contains("0.500000000"));
        QVERIFY(all.contains("-65536"));
        int data_lines = 0;
        const QList<QByteArray> lines = all.split('\n');
        for (const QByteArray &line : lines)
        {
            if (!line.isEmpty() && !line.startsWith('#'))
                ++data_lines;
        }
        QCOMPARE(data_lines, 5); /* header + 4 rows */

        const QString partial_path = dir.filePath(QStringLiteral("partial.vbf"));
        QFile partial(partial_path);
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write(vuprs::client::makeVbfFileHeader(metadata));
        partial.write(vuprs::client::makeVbfRecord(first));
        const QByteArray second_record = vuprs::client::makeVbfRecord(second);
        partial.write(second_record.left(10));
        partial.close();

        vuprs::client::VbfReader partial_reader;
        QVERIFY2(partial_reader.open(partial_path, &error), qPrintable(error));
        QVERIFY(partial_reader.next(&frame, &error) == vuprs::client::VbfReader::Result::Frame);
        QVERIFY(partial_reader.next(&frame, &error) == vuprs::client::VbfReader::Result::End);
        QVERIFY(partial_reader.statistics().truncated);
        QVERIFY(!partial_reader.statistics().hasFooter);
        QCOMPARE(partial_reader.statistics().frameCount, quint64(1));
    }

    void controlClientLoopback()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket *peer = nullptr;
        QByteArray receive_buffer;
        QObject::connect(&server, &QTcpServer::newConnection, [&]()
                         {
                             peer = server.nextPendingConnection();
                             QVERIFY(peer != nullptr);
                             QObject::connect(peer, &QTcpSocket::readyRead, [&]()
                                              {
                                                  receive_buffer.append(peer->readAll());
                                                  while (true)
                                                  {
                                                      vuprs::client::FrameHeader header;
                                                      QString error;
                                                      if (receive_buffer.size() < 40 ||
                                                          !vuprs::client::decodeFrameHeader(receive_buffer.left(40), &header, &error))
                                                          return;
                                                      const int total = 40 + static_cast<int>(header.payloadBytes);
                                                      if (receive_buffer.size() < total)
                                                          return;
                                                      const QByteArray payload = receive_buffer.mid(40, static_cast<int>(header.payloadBytes));
                                                      receive_buffer.remove(0, total);
                                                      if (header.type != vuprs::client::MessageType::ControlRequest)
                                                          continue;
                                                      const QJsonObject request = QJsonDocument::fromJson(payload).object();
                                                      const QString cmd = request.value("cmd").toString();
                                                      const QJsonObject response{{"ok", true}, {"cmd", cmd}, {"error", 0},
                                                                                 {"info", ""}, {"params", QJsonObject()}};
                                                      const QByteArray response_payload =
                                                          QJsonDocument(response).toJson(QJsonDocument::Compact);
                                                      peer->write(MakeFrame(vuprs::client::MessageType::ControlResponse,
                                                                            header.requestId, 0, 0, response_payload));
                                                  }
                                              });
                         });

        auto beam_queue = std::make_shared<vuprs::client::FrameQueue<vuprs::client::BeamFrame>>(8);
        auto scan_queue = std::make_shared<vuprs::client::FrameQueue<vuprs::client::ScanFrame>>(8);
        auto inference_queue = std::make_shared<vuprs::client::FrameQueue<vuprs::client::InferenceFrame>>(8);
        QThread thread;
        auto *client = new vuprs::client::ControlClient(beam_queue, scan_queue, inference_queue, 1000);
        client->moveToThread(&thread);
        QObject::connect(&thread, &QThread::started, client, &vuprs::client::ControlClient::initialize);
        QSignalSpy responses(client, &vuprs::client::ControlClient::commandFinished);
        struct ThreadCleanup
        {
            QThread &thread;
            vuprs::client::ControlClient *client;
            ~ThreadCleanup()
            {
                if (thread.isRunning() && client != nullptr)
                {
                    QMetaObject::invokeMethod(client, "doShutdown", Qt::BlockingQueuedConnection);
                    thread.quit();
                    thread.wait(3000);
                }
                delete client;
            }
        } cleanup{thread, client};
        thread.start();

        client->connectToServer(QStringLiteral("127.0.0.1"), server.serverPort(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(client->isConnected(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(peer != nullptr, 3000);

        client->sendCommand(QStringLiteral("ack"));
        QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 3000);
        const QList<QVariant> response_args = responses.takeFirst();
        QCOMPARE(response_args.at(1).toString(), QStringLiteral("ack"));
        QVERIFY(response_args.at(2).toBool());

        vuprs::client::BfDataHeader data;
        data.sampleRateMilliHz = 10000000;
        data.sampleCount = 2;
        auto samples = std::make_shared<QVector<qint32>>();
        samples->append(65536);
        samples->append(-65536);
        const QByteArray bf_payload = MakeBfPayload(data.sampleRateMilliHz, *samples);
        peer->write(MakeFrame(vuprs::client::MessageType::BfData, 0, 1, 123, bf_payload));
        QTRY_VERIFY_WITH_TIMEOUT(beam_queue->size() >= 1, 3000);
        vuprs::client::BeamFrame received;
        QVERIFY(beam_queue->tryPop(&received));
        QCOMPARE(received.sequence, 1u);
        QCOMPARE(received.samples.size(), 2);
        QCOMPARE(received.inferenceIdentity, -1);
        QVERIFY(!received.inferenceValid);

        const QVector<qint32> tensor = {10, 20, 30};
        peer->write(MakeFrame(vuprs::client::MessageType::InferenceData, 0, 2, 456,
                              MakeInferencePayload(tensor)));
        QTRY_VERIFY_WITH_TIMEOUT(inference_queue->size() >= 1, 3000);
        vuprs::client::InferenceFrame inference_received;
        QVERIFY(inference_queue->tryPop(&inference_received));
        QCOMPARE(inference_received.sequence, 2u);
        QCOMPARE(inference_received.values, tensor);

        server.close();
    }

    void vbfWriteFormat()
    {
        vuprs::client::BeamFrame frame;
        frame.sequence = 3;
        frame.timestampNs = 123456789;
        frame.sampleRateMilliHz = 10000000;
        frame.inferenceIdentity = 5;
        frame.inferenceValid = true;
        frame.samples = {0x00010000, -65536};

        QJsonObject metadata;
        metadata["server"] = "127.0.0.1:18080";
        const QByteArray header = vuprs::client::makeVbfFileHeader(metadata);
        QCOMPARE(header.left(8), QByteArray("VUPRSBF1"));
        const QByteArray record = vuprs::client::makeVbfRecord(frame);
        QCOMPARE(record.size(), vuprs::client::VBF_RECORD_HEADER_SIZE + frame.samples.size() * 4);
        QCOMPARE(static_cast<quint8>(record[0]), vuprs::client::VBF_RECORD_BF);
        QCOMPARE(static_cast<quint8>(record[1]), quint8(1));
        QCOMPARE(record.size() - vuprs::client::VBF_RECORD_HEADER_SIZE, 8);
        const QByteArray footer = vuprs::client::makeVbfFooter(1, 8, 123456789);
        QCOMPARE(footer.size(), vuprs::client::VBF_FOOTER_SIZE);
        QCOMPARE(static_cast<quint8>(footer[0]), vuprs::client::VBF_RECORD_END);
    }
};

QTEST_MAIN(ClientCoreTest)
#include "client_tests.moc"
