#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>

namespace BreezeDesk {

class ModelDownloadOperation;
class ModelManager;
class ModelSettingsManager;

class ModelListModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        DisplayNameRole,
        DescriptionRole,
        QuantizationRole,
        FileSizeRole,
        Sha256Role,
        LicenseNameRole,
        LicenseUrlRole,
        SourceUrlRole,
        RecommendedRole,
        DefaultCandidateRole,
        InstalledRole,
        StateRole,
        ProgressRole,
        DownloadSpeedRole,
        RemainingSecondsRole,
        ChecksumValidRole,
        LoadedRole,
        BackendRole,
        DefaultRole
    };
    Q_ENUM(Role)

    struct ModelItem {
        QString id;
        QString displayName;
        QString description;
        QString quantization;
        qint64 fileSize{0};
        QString sha256;
        QString licenseName;
        QUrl licenseUrl;
        QUrl sourceUrl;
        bool recommended{false};
        bool defaultCandidate{false};
        bool installed{false};
        QString state{"NotInstalled"};
        qreal progress{0.0};
        qint64 downloadSpeed{0};
        qint64 remainingSeconds{-1};
        bool checksumValid{false};
        bool loaded{false};
        QString backend;
        bool isDefault{false};
    };

    explicit ModelListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    bool setDefaultModel(const QString& id);
    bool setState(const QString& id, const QString& state, qreal progress);
    bool setDownloadMetrics(const QString& id, const QString& state, qreal progress, qint64 bytesPerSecond,
                            qint64 remainingSeconds);
    bool setInstalled(const QString& id, bool installed, bool checksumValid);
    bool setLoaded(const QString& id, bool loaded, const QString& backend);
    void replaceModels(QList<ModelItem> models);
    bool appendModel(ModelItem model);
    [[nodiscard]] bool contains(const QString& id) const;
    [[nodiscard]] bool isDefaultCandidate(const QString& id) const;
    [[nodiscard]] bool isLoaded(const QString& id) const;
    [[nodiscard]] bool isInstalled(const QString& id) const;

  private:
    [[nodiscard]] int indexOf(const QString& id) const;
    void emitRowChanged(int row);

    QList<ModelItem> m_models;
};

class ModelManagerViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* models READ models CONSTANT)
    Q_PROPERTY(QString defaultModelId READ defaultModelId NOTIFY defaultModelChanged)
    Q_PROPERTY(bool defaultModelReady READ defaultModelReady NOTIFY defaultModelReadyChanged)
    Q_PROPERTY(bool defaultModelDownloadActive READ defaultModelDownloadActive NOTIFY
                   defaultModelDownloadChanged)
    Q_PROPERTY(qreal defaultModelDownloadProgress READ defaultModelDownloadProgress NOTIFY
                   defaultModelDownloadChanged)
    Q_PROPERTY(QString selectedBackend READ selectedBackend NOTIFY backendChanged)
    Q_PROPERTY(QString actualBackend READ actualBackend NOTIFY backendChanged)
    Q_PROPERTY(QString runtimeVersion READ runtimeVersion NOTIFY backendChanged)

  public:
    explicit ModelManagerViewModel(QObject* parent = nullptr);

    // Dependencies remain owned by the application composition root and must
    // outlive this view model. Omitting them keeps the QML smoke-test mode.
    void installServices(ModelManager* modelManager, ModelSettingsManager* settingsManager = nullptr);

    [[nodiscard]] QAbstractItemModel* models() noexcept;
    [[nodiscard]] QString defaultModelId() const;
    [[nodiscard]] bool defaultModelReady() const;
    [[nodiscard]] bool defaultModelDownloadActive() const noexcept;
    [[nodiscard]] qreal defaultModelDownloadProgress() const noexcept;
    [[nodiscard]] QString selectedBackend() const;
    [[nodiscard]] QString actualBackend() const;
    [[nodiscard]] QString runtimeVersion() const;

    Q_INVOKABLE void download(const QString& id);
    Q_INVOKABLE void pause(const QString& id);
    Q_INVOKABLE void resume(const QString& id);
    Q_INVOKABLE void cancel(const QString& id);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void verify(const QString& id);
    Q_INVOKABLE void testModel(const QString& id);
    Q_INVOKABLE void importCustom(const QUrl& file);
    Q_INVOKABLE void setDefaultModel(const QString& id);

    void updateDownload(const QString& id, const QString& state, qreal progress);
    void updateInstalled(const QString& id, bool installed, bool checksumValid);
    void updateLoaded(const QString& id, bool loaded, const QString& backend);
    void updateBackend(const QString& selected, const QString& actual);
    void updateRuntimeInfo(const QString& selected, const QString& actual, const QString& runtimeVersion);

  signals:
    void defaultModelChanged();
    void defaultModelReadyChanged();
    void defaultModelDownloadChanged();
    void backendChanged();
    void downloadRequested(const QString& id);
    void pauseRequested(const QString& id);
    void resumeRequested(const QString& id);
    void cancelRequested(const QString& id);
    void deleteRequested(const QString& id);
    void verifyRequested(const QString& id);
    void testRequested(const QString& id);
    void customImportRequested(const QUrl& file);
    void commandRejected(const QString& message);
    void operationSucceeded(const QString& message);
    void downloadFinished(const QString& id, bool success, const QString& error);

  private:
    void refreshFromService();
    void refreshDefaultModelReady();
    void refreshDefaultModelDownload();
    void setDefaultModelDownload(const QString& id, bool active, qreal progress);
    void attachDownload(ModelDownloadOperation* operation);
    void persistDefaultModel();

    ModelListModel m_models;
    QString m_defaultModelId{"breeze-asr-25-q5"};
    bool m_defaultModelReady{false};
    bool m_defaultModelDownloadActive{false};
    qreal m_defaultModelDownloadProgress{0.0};
    QString m_selectedBackend{"Auto"};
    QString m_actualBackend{"Not loaded"};
    QString m_runtimeVersion{"Not loaded"};
    QPointer<ModelManager> m_modelManager;
    ModelSettingsManager* m_settingsManager{nullptr};
    QHash<QString, QPointer<ModelDownloadOperation>> m_downloads;
};

} // namespace BreezeDesk
