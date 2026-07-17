#pragma once

#include <QObject>
#include <QVariantList>

namespace BreezeDesk {

class DiagnosticsViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString osDescription READ osDescription CONSTANT)
    Q_PROPERTY(QString cpuArchitecture READ cpuArchitecture CONSTANT)
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    Q_PROPERTY(QString modelPath READ modelPath CONSTANT)
    Q_PROPERTY(QString cachePath READ cachePath CONSTANT)
    Q_PROPERTY(QString logPath READ logPath CONSTANT)
    Q_PROPERTY(QString ffmpegVersion READ ffmpegVersion WRITE setFfmpegVersion NOTIFY runtimeChanged)
    Q_PROPERTY(QString whisperVersion READ whisperVersion WRITE setWhisperVersion NOTIFY runtimeChanged)
    Q_PROPERTY(QString workerProtocolVersion READ workerProtocolVersion CONSTANT)
    Q_PROPERTY(QString selectedBackend READ selectedBackend WRITE setSelectedBackend NOTIFY runtimeChanged)
    Q_PROPERTY(QString actualBackend READ actualBackend WRITE setActualBackend NOTIFY runtimeChanged)

  public:
    explicit DiagnosticsViewModel(QObject* parent = nullptr);

    [[nodiscard]] QString appVersion() const;
    [[nodiscard]] QString qtVersion() const;
    [[nodiscard]] QString osDescription() const;
    [[nodiscard]] QString cpuArchitecture() const;
    [[nodiscard]] QString databasePath() const;
    [[nodiscard]] QString modelPath() const;
    [[nodiscard]] QString cachePath() const;
    [[nodiscard]] QString logPath() const;
    [[nodiscard]] QString ffmpegVersion() const;
    [[nodiscard]] QString whisperVersion() const;
    [[nodiscard]] QString workerProtocolVersion() const;
    [[nodiscard]] QString selectedBackend() const;
    [[nodiscard]] QString actualBackend() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void exportDiagnostics(bool includePaths);

  public slots:
    void setFfmpegVersion(const QString& value);
    void setWhisperVersion(const QString& value);
    void setSelectedBackend(const QString& value);
    void setActualBackend(const QString& value);

  signals:
    void runtimeChanged();
    void refreshRequested();
    void exportRequested(bool includePaths);

  private:
    QString m_ffmpegVersion{"Not detected"};
    QString m_whisperVersion{"Not loaded"};
    QString m_selectedBackend{"Auto"};
    QString m_actualBackend{"Not loaded"};
};

} // namespace BreezeDesk
