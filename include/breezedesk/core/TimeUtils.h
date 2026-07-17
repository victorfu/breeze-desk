#pragma once

#include <QDateTime>
#include <QString>

namespace BreezeDesk::TimeUtils {

[[nodiscard]] QString toStorageString(const QDateTime& dateTime);
[[nodiscard]] QDateTime fromStorageString(const QString& value);
[[nodiscard]] QString nowStorageString();
[[nodiscard]] QString formatClock(qint64 milliseconds, QChar decimalSeparator = QLatin1Char('.'));

} // namespace BreezeDesk::TimeUtils
