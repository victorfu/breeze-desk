#pragma once

#include <QString>

namespace BreezeDesk {

class FFmpegLocator final {
  public:
    struct Tools {
        QString ffmpegPath;
        QString ffprobePath;
        QString error;
        [[nodiscard]] bool isValid() const { return !ffmpegPath.isEmpty() && !ffprobePath.isEmpty(); }
    };

    [[nodiscard]] static Tools locate();
    [[nodiscard]] static QString version(const QString& executable, QString* error = nullptr);
};

} // namespace BreezeDesk
