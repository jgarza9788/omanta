#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration>

// Coloured tags, stored the freedesktop way: a comma-separated list in the
// file's own `user.xdg.tags` extended attribute, so they travel with the file
// through moves and renames and other apps (Dolphin, tmsu, `getfattr`) can
// read them. An index of tagged paths (~/.config/omanta/tags) is what lets a
// sidebar entry list every file with a tag without walking the disk; the
// attribute stays the truth, and the index is checked against it on read.
//
// Local files only — extended attributes don't exist through gvfs.
class Tags : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Bumps on every change, so a delegate binding `Tags.revision, ...`
    // re-reads its file's tags.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // The tags that have at least one file, in palette order then by name.
    Q_PROPERTY(QStringList usedTags READ usedTags NOTIFY changed)

public:
    explicit Tags(QObject *parent = nullptr);

    int revision() const { return m_revision; }
    QStringList usedTags() const;

    // The palette, in menu order: Red, Orange, Yellow, Green, Blue, Purple,
    // Gray. Any other tag a file carries still shows, in grey.
    Q_INVOKABLE static QStringList palette();
    Q_INVOKABLE static QString colorFor(const QString &tag);

    Q_INVOKABLE QStringList tagsFor(const QString &path);
    // True only when every path carries `tag` — what makes the menu row a tick.
    Q_INVOKABLE bool allHave(const QStringList &paths, const QString &tag);
    // Adds `tag` to every path, or removes it from all when all have it.
    Q_INVOKABLE void toggle(const QStringList &paths, const QString &tag);
    Q_INVOKABLE void setTag(const QStringList &paths, const QString &tag, bool on);
    Q_INVOKABLE void clearTags(const QStringList &paths);
    // Every indexed file still carrying `tag`, for the tag's sidebar view.
    Q_INVOKABLE QStringList pathsWithTag(const QString &tag);

    // The attribute itself, for the tests and other callers.
    static QStringList readAttribute(const QString &path, bool *ok = nullptr);
    static bool writeAttribute(const QString &path, const QStringList &tags);

Q_SIGNALS:
    void changed();

private:
    QString filePath() const;
    void load();
    void save();
    void apply(const QString &path, const QStringList &tags);

    QHash<QString, QStringList> m_cache; // path → tags, as last read
    QHash<QString, QStringList> m_index; // the persisted index
    int m_revision = 0;
};
