#pragma once

#include <QObject>
#include <QString>

namespace BreezeDesk {

class NormalizationOperation : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString error READ error NOTIFY finished)

  public:
    explicit NormalizationOperation(QObject* parent = nullptr);
    [[nodiscard]] double progress() const;
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString error() const;

  public slots:
    virtual void cancel() = 0;

  signals:
    void progressChanged();
    void runningChanged();
    void finished(bool success, const QString& outputPath);

  protected:
    void setProgress(double value);
    void setRunning(bool value);
    void setError(const QString& value);

  private:
    double m_progress = 0.0;
    bool m_running = false;
    QString m_error;
};

class IAudioNormalizer {
  public:
    virtual ~IAudioNormalizer() = default;
    virtual NormalizationOperation* normalize(const QString& sourcePath, const QString& outputPath,
                                              qint64 durationMs, QObject* parent = nullptr) = 0;
};

} // namespace BreezeDesk
