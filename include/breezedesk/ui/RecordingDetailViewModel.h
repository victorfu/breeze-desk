#pragma once

#include <QObject>
#include <QVariantMap>

namespace BreezeDesk {

class RecordingDetailViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id NOTIFY detailsChanged)
    Q_PROPERTY(QString title READ title NOTIFY detailsChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY detailsChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY detailsChanged)
    Q_PROPERTY(QString model READ model NOTIFY detailsChanged)
    Q_PROPERTY(QString status READ status NOTIFY detailsChanged)
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY detailsChanged)

  public:
    explicit RecordingDetailViewModel(QObject* parent = nullptr);

    [[nodiscard]] QString id() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString sourcePath() const;
    [[nodiscard]] qint64 durationMs() const;
    [[nodiscard]] QString model() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] QString notes() const;

    void setDetails(const QVariantMap& details);
    void clear();

  public slots:
    void setNotes(const QString& notes);

  signals:
    void detailsChanged();
    void notesEdited(const QString& recordingId, const QString& notes);

  private:
    QVariantMap m_details;
};

} // namespace BreezeDesk
