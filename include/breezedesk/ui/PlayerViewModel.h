#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>

class QAudioOutput;
class QMediaPlayer;

namespace BreezeDesk {

class PlayerViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(qreal playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString outputDeviceId READ outputDeviceId WRITE setOutputDeviceId NOTIFY outputDeviceChanged)
    Q_PROPERTY(bool autoScroll READ autoScroll WRITE setAutoScroll NOTIFY autoScrollChanged)
    Q_PROPERTY(bool loopSelection READ loopSelection WRITE setLoopSelection NOTIFY loopSelectionChanged)
    Q_PROPERTY(qint64 selectionStart READ selectionStart WRITE setSelectionStart NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectionEnd READ selectionEnd WRITE setSelectionEnd NOTIFY selectionChanged)
    Q_PROPERTY(
        QVariantList waveformPeaks READ waveformPeaks WRITE setWaveformPeaks NOTIFY waveformPeaksChanged)

  public:
    explicit PlayerViewModel(QObject* parent = nullptr);
    ~PlayerViewModel() override;

    [[nodiscard]] QUrl source() const;
    [[nodiscard]] qint64 position() const;
    [[nodiscard]] qint64 duration() const;
    [[nodiscard]] bool playing() const;
    [[nodiscard]] qreal playbackRate() const;
    [[nodiscard]] qreal volume() const;
    [[nodiscard]] bool muted() const;
    [[nodiscard]] QString outputDeviceId() const;
    [[nodiscard]] bool autoScroll() const noexcept;
    [[nodiscard]] bool loopSelection() const noexcept;
    [[nodiscard]] qint64 selectionStart() const noexcept;
    [[nodiscard]] qint64 selectionEnd() const noexcept;
    [[nodiscard]] QVariantList waveformPeaks() const;

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void skipBackward();
    Q_INVOKABLE void skipForward();

  public slots:
    void setSource(const QUrl& source);
    void setPosition(qint64 position);
    void setPlaybackRate(qreal rate);
    void setVolume(qreal volume);
    void setMuted(bool muted);
    void setOutputDeviceId(const QString& deviceId);
    void setAutoScroll(bool enabled);
    void setLoopSelection(bool enabled);
    void setSelectionStart(qint64 position);
    void setSelectionEnd(qint64 position);
    void setWaveformPeaks(const QVariantList& peaks);

  signals:
    void sourceChanged();
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void playbackRateChanged();
    void volumeChanged();
    void mutedChanged();
    void outputDeviceChanged();
    void autoScrollChanged();
    void loopSelectionChanged();
    void selectionChanged();
    void waveformPeaksChanged();
    void playbackError(const QString& message);

  private:
    QMediaPlayer* m_player{nullptr};
    QAudioOutput* m_audioOutput{nullptr};
    bool m_autoScroll{true};
    bool m_loopSelection{false};
    qint64 m_selectionStart{0};
    qint64 m_selectionEnd{0};
    QVariantList m_waveformPeaks;
    QString m_outputDeviceId{"Default"};
};

} // namespace BreezeDesk
