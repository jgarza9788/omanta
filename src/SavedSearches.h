#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration>

// Saved searches ("smart folders"): a search's folder, query and filters
// under a name, listed in the sidebar. Opening one runs the search again, so
// it always shows what matches now. Kept in ~/.config/omanta/saved-searches
// as JSON.
class SavedSearches : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Each {id, name, folder, query, content, matchMode, dateKind, dateRange,
    // typeFilter}, in the order they were saved.
    Q_PROPERTY(QVariantList searches READ searches NOTIFY changed)

public:
    explicit SavedSearches(QObject *parent = nullptr);

    QVariantList searches() const { return m_searches; }

    // Saves `search` (the fields above, minus id) and answers its id. A
    // search with the same name replaces the old one.
    Q_INVOKABLE QString save(const QVariantMap &search);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE QVariantMap find(const QString &id) const;

Q_SIGNALS:
    void changed();

private:
    QString filePath() const;
    void load();
    void write();

    QVariantList m_searches;
};
