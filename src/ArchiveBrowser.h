#pragma once

#include <QHash>
#include <QObject>
#include <QtQmlIntegration>

#include <atomic>
#include <memory>

// Browse an archive as if it were a folder. The archive is unpacked —
// through the same hardened ArchiveEngine::extract as Extract Here, with its
// path sanitising, permission stripping and size guards — into a private
// cache folder (~/.cache/omanta/archives, 0700), and the window navigates
// there. You look around and copy out what you need; the archive itself is
// never touched, and the cache is cleared when omanta starts and quits.
class ArchiveBrowser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit ArchiveBrowser(QObject *parent = nullptr);
    ~ArchiveBrowser() override;

    bool busy() const { return m_busy; }

    // Unpacks (or reuses an unchanged earlier unpack of) `archivePath`, then
    // emits opened() with the folder to show, or failed() with why.
    Q_INVOKABLE void open(const QString &archivePath);
    Q_INVOKABLE void cancel();

    // The archive a location is a browsed copy of, or "" — for the banner
    // that says "you are looking inside photos.zip".
    Q_INVOKABLE QString archiveFor(const QString &location) const;
    // The top folder of that browsed copy (where "leave" goes up from).
    Q_INVOKABLE QString browseRootFor(const QString &location) const;

    static QString cacheRoot();
    // Removes every browsed copy. Only ever our own cache folder.
    static void clearCache();

Q_SIGNALS:
    void busyChanged();
    void opened(const QString &archivePath, const QString &folder);
    void failed(const QString &archivePath, const QString &message);

private:
    bool m_busy = false;
    QHash<QString, QString> m_rootToArchive; // browse root → archive path
    std::shared_ptr<std::atomic_bool> m_cancel;
    quint64 m_generation = 0;
};
