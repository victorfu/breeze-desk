#include "UpdateServiceFactory.h"

#include "breezedesk/update_config.h"

#include <QByteArray>
#include <QLibrary>

namespace BreezeDesk {

class WinSparkleUpdateService final : public IUpdateService {
  public:
    explicit WinSparkleUpdateService(QObject* parent)
        : IUpdateService(parent), m_library(QStringLiteral("WinSparkle.dll")) {
        initialize();
    }
    ~WinSparkleUpdateService() override {
        if (m_initialized && m_cleanup != nullptr) {
            m_cleanup();
        }
        m_library.unload();
    }
    bool isAvailable() const override { return m_available; }
    void checkForUpdates(bool userInitiated) override {
        if (!m_available) {
            emit updateError(m_error.isEmpty()
                                 ? QStringLiteral("WinSparkle is not installed with this application.")
                                 : m_error);
            return;
        }
        const Check check = userInitiated ? m_checkWithUi : m_checkWithoutUi;
        if (check == nullptr) {
            emit updateError(QStringLiteral("The WinSparkle library is incompatible with this build."));
            return;
        }
        check();
    }

  private:
    using Configure = void (*)(const char*);
    using SetPublicKey = int (*)(const char*);
    using SetAutomaticChecks = void (*)(int);
    using Lifecycle = void (*)();
    using Check = void (*)();

    void initialize() {
        if (!m_library.load()) {
            m_error = QStringLiteral("WinSparkle is not installed with this application.");
            return;
        }

        const auto setUrl = reinterpret_cast<Configure>(m_library.resolve("win_sparkle_set_appcast_url"));
        const auto setPublicKey =
            reinterpret_cast<SetPublicKey>(m_library.resolve("win_sparkle_set_eddsa_public_key"));
        const auto setAutomaticChecks = reinterpret_cast<SetAutomaticChecks>(
            m_library.resolve("win_sparkle_set_automatic_check_for_updates"));
        const auto initializeLibrary = reinterpret_cast<Lifecycle>(m_library.resolve("win_sparkle_init"));
        m_cleanup = reinterpret_cast<Lifecycle>(m_library.resolve("win_sparkle_cleanup"));
        m_checkWithUi = reinterpret_cast<Check>(m_library.resolve("win_sparkle_check_update_with_ui"));
        m_checkWithoutUi = reinterpret_cast<Check>(m_library.resolve("win_sparkle_check_update_without_ui"));
        if (setUrl == nullptr || setPublicKey == nullptr || setAutomaticChecks == nullptr ||
            initializeLibrary == nullptr || m_cleanup == nullptr || m_checkWithUi == nullptr ||
            m_checkWithoutUi == nullptr) {
            m_error = QStringLiteral("The WinSparkle library is incompatible with this build.");
            return;
        }

        const QByteArray appcastUrl(BREEZEDESK_APPCAST_URL);
        const QByteArray publicKey(BREEZEDESK_EDDSA_PUBLIC_KEY);
        if (!QString::fromUtf8(appcastUrl).startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
            publicKey.trimmed().isEmpty()) {
            m_error = QStringLiteral("WinSparkle updates require an HTTPS appcast and an EdDSA "
                                     "public key configured at build time.");
            return;
        }

        setUrl(appcastUrl.constData());
        if (setPublicKey(publicKey.constData()) != 1) {
            m_error = QStringLiteral("WinSparkle rejected the configured EdDSA public key.");
            return;
        }
        // BreezeDesk owns the opt-in setting and performs its launch check explicitly.
        setAutomaticChecks(0);
        initializeLibrary();
        m_initialized = true;
        m_available = true;
    }

    QLibrary m_library;
    Lifecycle m_cleanup = nullptr;
    Check m_checkWithUi = nullptr;
    Check m_checkWithoutUi = nullptr;
    QString m_error;
    bool m_initialized = false;
    bool m_available = false;
};

std::unique_ptr<IUpdateService> createNativeUpdateService(QObject* parent) {
    return std::make_unique<WinSparkleUpdateService>(parent);
}

} // namespace BreezeDesk
