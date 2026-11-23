#include "record/recording_naming.h"

#include <algorithm>
#include <QDir>
#include <QStringList>

QString vuprs::client::makeTaskId(qint64 unixSeconds)
{
    const qint64 value = unixSeconds % 10000;
    return QString::number(value).rightJustified(4, QLatin1Char('0'));
}

int vuprs::client::nextRecordingIndex(const QString &directory, const QString &taskId, int minimumIndex)
{
    int index = std::max(1, minimumIndex);
    QDir dir(directory);
    const QString prefix = QStringLiteral("task%1_id").arg(taskId);
    const QStringList entries = dir.entryList({prefix + QStringLiteral("*")}, QDir::Files);
    for (const QString &name : entries)
    {
        const QString suffix = name.mid(prefix.size());
        int digits = 0;
        while (digits < suffix.size() && suffix.at(digits).isDigit())
            ++digits;
        if (digits == 0)
            continue;
        bool ok = false;
        const int existing = suffix.left(digits).toInt(&ok);
        if (ok)
            index = std::max(index, existing + 1);
    }
    return index;
}

QString vuprs::client::recordingFilePath(const QString &directory, const QString &taskId, int index)
{
    return QDir(directory).filePath(QStringLiteral("task%1_id%2.vbf").arg(taskId).arg(index));
}
