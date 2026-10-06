#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration>

#include <atomic>
#include <functional>
#include <memory>

// Finds files with the same contents below a folder, on a worker thread:
// grouped by size first (free), then by a hash of the first 64 KB, and only
// files still tied are read in full and compared by SHA-256. Hard links to
// one file are one file, not duplicates; symlinks and empty files are left
// out.
class DuplicateFinder : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    // "Looking at 1 204 files…", "Comparing 37 files…"
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    // Largest waste first: {size, paths: [...]} — paths sorted, so the
    // window's default "keep the first" is stable.
    Q_PROPERTY(QVariantList groups READ groups NOTIFY stateChanged)
    // Bytes that removing every extra copy would free.
    Q_PROPERTY(qint64 wastedBytes READ wastedBytes NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    struct Group {
        qint64 size = 0;
        QStringList paths;
    };

    explicit DuplicateFinder(QObject *parent = nullptr);
    ~DuplicateFinder() override;

    QString rootPath() const { return m_root; }
    bool running() const { return m_running; }
    QString status() const { return m_status; }
    QVariantList groups() const { return m_groups; }
    qint64 wastedBytes() const { return m_wasted; }
    QString error() const { return m_error; }

    Q_INVOKABLE void start(const QString &path, bool includeHidden = false);
    Q_INVOKABLE void cancel();

    // Replaces each of `duplicates` with a hard link to `keep` — same
    // contents, one copy on disk. Checks each is still the same size and on
    // the same filesystem, links under a temporary name, then renames over
    // the duplicate, so a failure leaves the duplicate untouched. Answers how
    // many were replaced; *errors gets one line per failure.
    Q_INVOKABLE int replaceWithLinks(const QString &keep, const QStringList &duplicates);
    QStringList lastErrors() const { return m_lastErrors; }
    Q_INVOKABLE QStringList linkErrors() const { return m_lastErrors; }

    // The search, synchronous, for the worker and the tests.
    static QList<Group> find(const QString &root, bool includeHidden,
                             const std::function<bool()> &cancelled,
                             const std::function<void(const QString &)> &status);

Q_SIGNALS:
    void stateChanged();
    void progressChanged();

private:
    QString m_root;
    bool m_running = false;
    QString m_status;
    QVariantList m_groups;
    qint64 m_wasted = 0;
    QString m_error;
    QStringList m_lastErrors;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
