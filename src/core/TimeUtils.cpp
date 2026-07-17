#include "breezedesk/core/TimeUtils.h"

namespace BreezeDesk::TimeUtils {

QString toStorageString(const QDateTime& dateTime) {
    return dateTime.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime fromStorageString(const QString& value) {
    return QDateTime::fromString(value, Qt::ISODateWithMs).toUTC();
}

QString nowStorageString() {
    return toStorageString(QDateTime::currentDateTimeUtc());
}

QString formatClock(const qint64 milliseconds, const QChar decimalSeparator) {
    const qint64 bounded = qMax<qint64>(0, milliseconds);
    const qint64 hours = bounded / 3'600'000;
    const qint64 minutes = (bounded / 60'000) % 60;
    const qint64 seconds = (bounded / 1'000) % 60;
    const qint64 millis = bounded % 1'000;
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(decimalSeparator)
        .arg(millis, 3, 10, QLatin1Char('0'));
}

} // namespace BreezeDesk::TimeUtils
