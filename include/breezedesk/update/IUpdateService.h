#pragma once

#include <QObject>
#include <QString>

namespace BreezeDesk {

class IUpdateService : public QObject {
    Q_OBJECT

  public:
    explicit IUpdateService(QObject* parent = nullptr) : QObject(parent) {}
    ~IUpdateService() override = default;
    [[nodiscard]] virtual bool isAvailable() const = 0;
    virtual void checkForUpdates(bool userInitiated) = 0;

  signals:
    void updateAvailable(const QString& version, const QString& releaseNotes);
    void noUpdateAvailable();
    void updateError(const QString& message);
};

} // namespace BreezeDesk
