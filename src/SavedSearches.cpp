#include "SavedSearches.h"
#include "ProPaths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QVariantMap>

namespace {

const QStringList kFields = { "name", "folder", "query", "content", "matchMode",
                              "dateKind", "dateRange", "typeFilter" };

} // namespace

SavedSearches::SavedSearches(QObject *parent)
    : QObject(parent)
{
    load();
}

QString SavedSearches::filePath() const
{
    return ProPaths::file(QStringLiteral("saved-searches"));
}

void SavedSearches::load()
{
    m_searches.clear();
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : array) {
        const QVariantMap map = value.toObject().toVariantMap();
        if (map.value(QStringLiteral("id")).toString().isEmpty()
            || map.value(QStringLiteral("name")).toString().isEmpty()
            || !map.value(QStringLiteral("folder")).toString().startsWith(QLatin1Char('/')))
            continue;
        m_searches.append(map);
    }
}

void SavedSearches::write()
{
    ProPaths::write(filePath(),
                    QJsonDocument(QJsonArray::fromVariantList(m_searches)).toJson(QJsonDocument::Indented));
    Q_EMIT changed();
}

QString SavedSearches::save(const QVariantMap &search)
{
    QVariantMap clean;
    for (const QString &field : kFields)
        clean.insert(field, search.value(field));
    clean.insert(QStringLiteral("name"), clean.value(QStringLiteral("name")).toString().trimmed());
    if (clean.value(QStringLiteral("name")).toString().isEmpty()
        || !clean.value(QStringLiteral("folder")).toString().startsWith(QLatin1Char('/')))
        return {};

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (int i = 0; i < m_searches.size(); ++i) {
        if (m_searches.at(i).toMap().value(QStringLiteral("name")) == clean.value(QStringLiteral("name"))) {
            id = m_searches.at(i).toMap().value(QStringLiteral("id")).toString();
            m_searches.removeAt(i);
            break;
        }
    }
    clean.insert(QStringLiteral("id"), id);
    m_searches.append(clean);
    write();
    return id;
}

void SavedSearches::remove(const QString &id)
{
    for (int i = 0; i < m_searches.size(); ++i) {
        if (m_searches.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            m_searches.removeAt(i);
            write();
            return;
        }
    }
}

QVariantMap SavedSearches::find(const QString &id) const
{
    for (const QVariant &value : m_searches) {
        const QVariantMap map = value.toMap();
        if (map.value(QStringLiteral("id")).toString() == id)
            return map;
    }
    return {};
}
