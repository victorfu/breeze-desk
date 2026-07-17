#include "breezedesk/ui/PlayerViewModel.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QMediaPlayer>

#include <algorithm>

namespace BreezeDesk {

PlayerViewModel::PlayerViewModel(QObject* parent)
    : QObject(parent), m_player(new QMediaPlayer(this)), m_audioOutput(new QAudioOutput(this)) {
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(1.0F);
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (m_loopSelection && m_selectionEnd > m_selectionStart && position >= m_selectionEnd) {
            m_player->setPosition(m_selectionStart);
        }
        emit positionChanged();
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, &PlayerViewModel::durationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &PlayerViewModel::playingChanged);
    connect(m_player, &QMediaPlayer::playbackRateChanged, this, &PlayerViewModel::playbackRateChanged);
    connect(m_player, &QMediaPlayer::sourceChanged, this, &PlayerViewModel::sourceChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& message) { emit playbackError(message); });
}

PlayerViewModel::~PlayerViewModel() = default;

QUrl PlayerViewModel::source() const {
    return m_player->source();
}
qint64 PlayerViewModel::position() const {
    return m_player->position();
}
qint64 PlayerViewModel::duration() const {
    return m_player->duration();
}
bool PlayerViewModel::playing() const {
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}
qreal PlayerViewModel::playbackRate() const {
    return m_player->playbackRate();
}
qreal PlayerViewModel::volume() const {
    return m_audioOutput->volume();
}
bool PlayerViewModel::muted() const {
    return m_audioOutput->isMuted();
}
QString PlayerViewModel::outputDeviceId() const {
    return m_outputDeviceId;
}
bool PlayerViewModel::autoScroll() const noexcept {
    return m_autoScroll;
}
bool PlayerViewModel::loopSelection() const noexcept {
    return m_loopSelection;
}
qint64 PlayerViewModel::selectionStart() const noexcept {
    return m_selectionStart;
}
qint64 PlayerViewModel::selectionEnd() const noexcept {
    return m_selectionEnd;
}
QVariantList PlayerViewModel::waveformPeaks() const {
    return m_waveformPeaks;
}

void PlayerViewModel::playPause() {
    playing() ? pause() : play();
}
void PlayerViewModel::play() {
    m_player->play();
}
void PlayerViewModel::pause() {
    m_player->pause();
}
void PlayerViewModel::stop() {
    m_player->stop();
}
void PlayerViewModel::skipBackward() {
    setPosition(position() - 5000);
}
void PlayerViewModel::skipForward() {
    setPosition(position() + 5000);
}

void PlayerViewModel::setSource(const QUrl& source) {
    if (m_player->source() != source) {
        m_player->setSource(source);
    }
}

void PlayerViewModel::setPosition(qint64 position) {
    m_player->setPosition(qBound<qint64>(0, position, duration()));
}

void PlayerViewModel::setPlaybackRate(qreal rate) {
    static constexpr qreal rates[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    for (qreal allowed : rates) {
        if (qFuzzyCompare(rate, allowed)) {
            m_player->setPlaybackRate(rate);
            return;
        }
    }
}

void PlayerViewModel::setVolume(qreal volume) {
    const qreal bounded = qBound(0.0, volume, 1.0);
    if (!qFuzzyCompare(static_cast<qreal>(m_audioOutput->volume()), bounded)) {
        m_audioOutput->setVolume(static_cast<float>(bounded));
        emit volumeChanged();
    }
}

void PlayerViewModel::setMuted(bool muted) {
    if (m_audioOutput->isMuted() != muted) {
        m_audioOutput->setMuted(muted);
        emit mutedChanged();
    }
}

void PlayerViewModel::setOutputDeviceId(const QString& deviceId) {
    const QString normalized = deviceId.isEmpty() ? QStringLiteral("Default") : deviceId;
    QAudioDevice selected = QMediaDevices::defaultAudioOutput();
    if (normalized != QLatin1String("Default")) {
        const auto outputs = QMediaDevices::audioOutputs();
        const auto match =
            std::find_if(outputs.cbegin(), outputs.cend(), [&normalized](const QAudioDevice& device) {
                return QString::fromUtf8(device.id()) == normalized;
            });
        if (match == outputs.cend()) {
            emit playbackError(tr("The selected playback device is unavailable. Using the system default."));
        } else {
            selected = *match;
        }
    }
    if (!selected.isNull()) {
        m_audioOutput->setDevice(selected);
    }
    const QString effective =
        normalized == QLatin1String("Default") || selected == QMediaDevices::defaultAudioOutput()
            ? QStringLiteral("Default")
            : normalized;
    if (m_outputDeviceId != effective) {
        m_outputDeviceId = effective;
        emit outputDeviceChanged();
    }
}

void PlayerViewModel::setAutoScroll(bool enabled) {
    if (m_autoScroll != enabled) {
        m_autoScroll = enabled;
        emit autoScrollChanged();
    }
}

void PlayerViewModel::setLoopSelection(bool enabled) {
    if (m_loopSelection != enabled) {
        m_loopSelection = enabled;
        emit loopSelectionChanged();
    }
}

void PlayerViewModel::setSelectionStart(qint64 position) {
    const qint64 bounded = qMax<qint64>(0, position);
    if (m_selectionStart != bounded) {
        m_selectionStart = bounded;
        if (m_selectionEnd < m_selectionStart) {
            m_selectionEnd = m_selectionStart;
        }
        emit selectionChanged();
    }
}

void PlayerViewModel::setSelectionEnd(qint64 position) {
    const qint64 bounded = qMax(m_selectionStart, position);
    if (m_selectionEnd != bounded) {
        m_selectionEnd = bounded;
        emit selectionChanged();
    }
}

void PlayerViewModel::setWaveformPeaks(const QVariantList& peaks) {
    if (m_waveformPeaks != peaks) {
        m_waveformPeaks = peaks;
        emit waveformPeaksChanged();
    }
}

} // namespace BreezeDesk
