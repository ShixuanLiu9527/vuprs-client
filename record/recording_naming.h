#ifndef VUPRS_CLIENT_RECORDING_NAMING_H
#define VUPRS_CLIENT_RECORDING_NAMING_H

#include <QString>

namespace vuprs
{
    namespace client
    {
        /** Four-digit task id from the low digits of the Unix timestamp. */
        QString makeTaskId(qint64 unixSeconds);

        /** Highest existing index for task<taskId>_id<index>.vbf/.part plus one. */
        int nextRecordingIndex(const QString &directory, const QString &taskId, int minimumIndex = 1);

        /** Full path <directory>/task<taskId>_id<index>.vbf. */
        QString recordingFilePath(const QString &directory, const QString &taskId, int index);
    }
}

#endif
