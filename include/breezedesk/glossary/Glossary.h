#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace BreezeDesk {

struct GlossaryProfile {
    QString id;
    QString name;
    QString description;
    QString projectContext;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct GlossaryTerm {
    QString id;
    QString profileId;
    QString canonicalText;
    QStringList aliases;
    QString category;
    QString language;
    int priority = 0;
    bool caseSensitive = false;
    bool enabled = true;
    QString notes;
    QDateTime createdAt;
    QDateTime updatedAt;
};

} // namespace BreezeDesk
