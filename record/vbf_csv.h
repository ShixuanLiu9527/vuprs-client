#ifndef VUPRS_CLIENT_VBF_CSV_H
#define VUPRS_CLIENT_VBF_CSV_H

#include <functional>
#include <QString>

namespace vuprs
{
    namespace client
    {
        struct VbfCsvExportResult
        {
            quint64 frames = 0;
            quint64 rows = 0;
            bool truncated = false;
            bool hasFooter = false;
        };

        /**
         * @brief Export a .vbf recording to CSV, one row per BF sample.
         *
         * The progress callback receives processed bytes and total bytes. Returning
         * false cancels the export and removes the partially written file.
         */
        using VbfCsvProgress = std::function<bool(qint64 processedBytes, qint64 totalBytes)>;

        bool exportVbfToCsv(const QString &vbfPath,
                            const QString &csvPath,
                            QString *error = nullptr,
                            const VbfCsvProgress &progress = VbfCsvProgress(),
                            VbfCsvExportResult *result = nullptr);
    }
}

#endif
