#ifndef VUPRS_CLIENT_VBF_READER_H
#define VUPRS_CLIENT_VBF_READER_H

#include <QFile>
#include <QJsonObject>
#include <QString>
#include "core/beam_frame.h"
#include "record/vbf_format.h"

namespace vuprs
{
    namespace client
    {
        struct VbfStatistics
        {
            quint64 frameCount = 0;
            quint64 sampleCount = 0;
            quint64 payloadBytes = 0;
            bool hasFooter = false;
            bool truncated = false;
        };

        /**
         * @brief Sequential reader for self-describing .vbf recordings.
         *
         * A truncated final record (for example an unfinished .vbf.part) is treated
         * as the end of the file; statistics().truncated reports that case.
         */
        class VbfReader
        {
        public:
            enum class Result
            {
                Frame,
                End,
                Error
            };

            VbfReader() = default;
            ~VbfReader();

            VbfReader(const VbfReader &) = delete;
            VbfReader &operator=(const VbfReader &) = delete;

            bool open(const QString &path, QString *error = nullptr);
            void close();

            bool isOpen() const { return this->open_; }
            const QJsonObject &metadata() const { return this->metadata_; }
            const QString &path() const { return this->path_; }
            qint64 fileSize() const { return this->fileSize_; }
            qint64 position() const;
            const VbfStatistics &statistics() const { return this->statistics_; }

            Result next(BeamFrame *frame, QString *error = nullptr);

        private:
            QFile file_;
            QJsonObject metadata_;
            QString path_;
            qint64 fileSize_ = 0;
            bool open_ = false;
            bool ended_ = false;
            VbfStatistics statistics_;
        };
    }
}

#endif
