#pragma once

#include <QObject>
#include <QVariantList>

#include <memory>

class QAudioSource;
class QMediaDevices;
class QTimer;

namespace BreezeDesk {

class AudioSinkDevice;

class MicrophoneRecorder final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY inputDevicesChanged)
    Q_PROPERTY(QString selectedDeviceId READ selectedDeviceId WRITE setSelectedDeviceId NOTIFY
                   selectedDeviceIdChanged)

  public:
    explicit MicrophoneRecorder(QObject* parent = nullptr);
    ~MicrophoneRecorder() override;

    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] bool isPaused() const;
    [[nodiscard]] qint64 durationMs() const;
    [[nodiscard]] double level() const;
    [[nodiscard]] QVariantList inputDevices() const;
    [[nodiscard]] QString selectedDeviceId() const;
    void setSelectedDeviceId(const QString& id);

    Q_INVOKABLE bool start(const QString& outputPath);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();

  signals:
    void recordingChanged();
    void pausedChanged();
    void durationChanged();
    void levelChanged();
    void inputDevicesChanged();
    void selectedDeviceIdChanged();
    void recordingFinished(const QString& path);
    void recordingError(const QString& message);

  private:
    QMediaDevices* m_mediaDevices = nullptr;
    QAudioSource* m_audioSource = nullptr;
    AudioSinkDevice* m_sink = nullptr;
    QTimer* m_durationTimer = nullptr;
    QString m_outputPath;
    QString m_selectedDeviceId;
    qint64 m_durationMs = 0;
    double m_level = 0.0;
    bool m_recording = false;
    bool m_paused = false;
};

} // namespace BreezeDesk
