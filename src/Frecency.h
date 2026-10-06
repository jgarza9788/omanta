#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration>

// The folders you go to, ranked by how often and how recently — zoxide's
// "frecency" — for the command palette's fuzzy jump (Ctrl+P).
//
// One line per folder in ~/.config/omanta/frecency: visit count, last visit
// (seconds since the epoch) and the path, tab-separated. Writes are batched:
// navigating must never wait on the disk.
class Frecency : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Frecency(QObject *parent = nullptr);
    ~Frecency() override;

    // Records one visit to a local folder. Virtual places (trash:///…) and
    // remote URIs are not remembered.
    Q_INVOKABLE void visit(const QString &path);

    // Folders matching `query` (fuzzy, see fuzzyScore), best first, each a
    // {path, name, score} map. An empty query lists the top folders. Folders
    // that no longer exist are skipped.
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 20) const;

    // Forget everything — the palette's "Clear History".
    Q_INVOKABLE void clear();

    // How well `needle` matches `haystack` as a subsequence, ignoring case:
    // -1 when it doesn't. Consecutive letters, word starts (after / - _ . or
    // a space) and the last path component score higher. Public for the
    // command palette's actions and the tests.
    Q_INVOKABLE static int fuzzyScore(const QString &needle, const QString &haystack);

    // Visit weight by age, zoxide's buckets.
    static double frecency(int count, qint64 lastVisit, qint64 now);

    // The number of remembered folders (pruned past kMaxEntries).
    int size() const { return int(m_entries.size()); }

    static constexpr int kMaxEntries = 1000;

private:
    struct Entry {
        int count = 0;
        qint64 last = 0;
    };

    QString filePath() const;
    void load();
    void save();
    void scheduleSave();

    QHash<QString, Entry> m_entries;
    QTimer m_saveTimer;
    bool m_dirty = false;
};
