#include "breezedesk/audio/MicrophoneRecorder.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QDataStream>
#include <QMediaDevices>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace BreezeDesk {

class AudioSinkDevice final : public QIODevice {
  public:
    AudioSinkDevice(QString path, QAudioFormat format, std::function<void(double)> levelCallback,
                    QObject* parent)
        : QIODevice(parent), m_file(std::move(path)), m_format(std::move(format)),
          m_levelCallback(std::move(levelCallback)) {}

    bool begin() {
        if (!m_file.open(QIODevice::WriteOnly)) {
            return false;
        }
        if (m_file.write(QByteArray(44, '\0')) != 44) {
            m_file.cancelWriting();
            return false;
        }
        return open(QIODevice::WriteOnly);
    }

    bool finish() {
        close();
        if (!m_file.seek(0)) {
            m_file.cancelWriting();
            return false;
        }
        QDataStream stream(&m_file);
        stream.setByteOrder(QDataStream::LittleEndian);
        const quint16 channels = static_cast<quint16>(m_format.channelCount());
        const quint32 sampleRate = static_cast<quint32>(m_format.sampleRate());
        const quint16 bitsPerSample = 16;
        const quint16 blockAlign = static_cast<quint16>(channels * (bitsPerSample / 8U));
        const quint32 byteRate = sampleRate * static_cast<quint32>(blockAlign);
        const quint32 dataSize = static_cast<quint32>(qMin<qint64>(m_dataBytes, 0xFFFFFFFFLL - 44LL));
        stream.writeRawData("RIFF", 4);
        stream << static_cast<quint32>(36U + dataSize);
        stream.writeRawData("WAVEfmt ", 8);
        stream << static_cast<quint32>(16U) << static_cast<quint16>(1U) << channels << sampleRate << byteRate
               << blockAlign << bitsPerSample;
        stream.writeRawData("data", 4);
        stream << dataSize;
        return stream.status() == QDataStream::Ok && m_file.commit();
    }

  protected:
    qint64 readData(char*, qint64) override { return -1; }

    qint64 writeData(const char* data, qint64 length) override {
        if (m_paused) {
            return length;
        }
        const qint64 written = m_file.write(data, length);
        if (written <= 0) {
            return written;
        }
        m_dataBytes += written;
        if (m_format.sampleFormat() == QAudioFormat::Int16) {
            const auto* samples = reinterpret_cast<const qint16*>(data);
            const qint64 sampleCount = written / static_cast<qint64>(sizeof(qint16));
            qint16 peak = 0;
            for (qint64 index = 0; index < sampleCount; ++index) {
                const int magnitude = std::abs(static_cast<int>(samples[index]));
                peak = qMax(peak, static_cast<qint16>(qMin(magnitude, 32767)));
            }
            m_levelCallback(static_cast<double>(peak) / 32767.0);
        }
        return written;
    }

  public:
    void setPaused(bool paused) { m_paused = paused; }

  private:
    QSaveFile m_file;
    QAudioFormat m_format;
    std::function<void(double)> m_levelCallback;
    qint64 m_dataBytes = 0;
    bool m_paused = false;
};

MicrophoneRecorder::MicrophoneRecorder(QObject* parent)
    : QObject(parent), m_mediaDevices(new QMediaDevices(this)), m_durationTimer(new QTimer(this)) {
    m_durationTimer->setInterval(100);
    connect(m_durationTimer, &QTimer::timeout, this, [this] {
        if (!m_paused) {
            m_durationMs += 100;
            emit durationChanged();
        }
        if (m_level > 0.0) {
            m_level *= 0.82;
            emit levelChanged();
        }
    });
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this,
            &MicrophoneRecorder::inputDevicesChanged);
}

MicrophoneRecorder::~MicrophoneRecorder() {
    if (m_recording) {
        stop();
    }
}

bool MicrophoneRecorder::isRecording() const {
    return m_recording;
}
bool MicrophoneRecorder::isPaused() const {
    return m_paused;
}
qint64 MicrophoneRecorder::durationMs() const {
    return m_durationMs;
}
double MicrophoneRecorder::level() const {
    return m_level;
}

QVariantList MicrophoneRecorder::inputDevices() const {
    QVariantList result;
    for (const QAudioDevice& device : QMediaDevices::audioInputs()) {
        QVariantMap value;
        value.insert(QStringLiteral("id"), QString::fromUtf8(device.id()));
        value.insert(QStringLiteral("description"), device.description());
        value.insert(QStringLiteral("default"), device == QMediaDevices::defaultAudioInput());
        result.push_back(value);
    }
    return result;
}

QString MicrophoneRecorder::selectedDeviceId() const {
    return m_selectedDeviceId;
}

void MicrophoneRecorder::setSelectedDeviceId(const QString& id) {
    if (!m_recording && m_selectedDeviceId != id) {
        m_selectedDeviceId = id;
        emit selectedDeviceIdChanged();
    }
}

bool MicrophoneRecorder::start(const QString& outputPath) {
    if (m_recording || outputPath.isEmpty()) {
        return false;
    }
    QAudioDevice selected = QMediaDevices::defaultAudioInput();
    for (const QAudioDevice& device : QMediaDevices::audioInputs()) {
        if (QString::fromUtf8(device.id()) == m_selectedDeviceId) {
            selected = device;
            break;
        }
    }
    if (selected.isNull()) {
        emit recordingError(QStringLiteral("No microphone input device is available."));
        return false;
    }
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!selected.isFormatSupported(format)) {
        format = selected.preferredFormat();
        if (format.sampleFormat() != QAudioFormat::Int16) {
            emit recordingError(
                QStringLiteral("The selected microphone does not provide a supported PCM16 format."));
            return false;
        }
    }
    m_outputPath = outputPath;
    m_sink = new AudioSinkDevice(
        outputPath, format,
        [this](double newLevel) {
            if (newLevel > m_level) {
                m_level = newLevel;
                emit levelChanged();
            }
        },
        this);
    if (!m_sink->begin()) {
        emit recordingError(QStringLiteral("The recording file could not be created."));
        m_sink->deleteLater();
        m_sink = nullptr;
        return false;
    }
    m_audioSource = new QAudioSource(selected, format, this);
    connect(m_audioSource, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::StoppedState && m_recording && m_audioSource->error() != QAudio::NoError) {
            emit recordingError(
                QStringLiteral("Microphone recording stopped because the audio device failed."));
            stop();
        }
    });
    m_durationMs = 0;
    m_paused = false;
    m_recording = true;
    m_audioSource->start(m_sink);
    m_durationTimer->start();
    emit recordingChanged();
    emit durationChanged();
    return true;
}

void MicrophoneRecorder::pause() {
    if (!m_recording || m_paused) {
        return;
    }
    m_paused = true;
    m_sink->setPaused(true);
    m_audioSource->suspend();
    emit pausedChanged();
}

void MicrophoneRecorder::resume() {
    if (!m_recording || !m_paused) {
        return;
    }
    m_paused = false;
    m_sink->setPaused(false);
    m_audioSource->resume();
    emit pausedChanged();
}

void MicrophoneRecorder::stop() {
    if (!m_recording) {
        return;
    }
    m_recording = false;
    m_durationTimer->stop();
    m_audioSource->stop();
    const bool saved = m_sink->finish();
    m_audioSource->deleteLater();
    m_sink->deleteLater();
    m_audioSource = nullptr;
    m_sink = nullptr;
    m_paused = false;
    emit recordingChanged();
    emit pausedChanged();
    if (saved) {
        emit recordingFinished(m_outputPath);
    } else {
        emit recordingError(QStringLiteral("The recording could not be saved atomically."));
    }
}

} // namespace BreezeDesk
