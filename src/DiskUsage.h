#pragma once

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration>

#include <atomic>
#include <functional>
#include <memory>

// What fills a folder: one scan of the tree on a worker thread (space on
// disk, like `du -x` — symlinks are not followed and other filesystems are
// not entered), then a level at a time for the disk usage map to draw.
// Clicking into a folder in the map only re-reads the scan, never the disk.
class DiskUsage : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY stateChanged)
    // The folder the map shows now: rootPath or somewhere below it.
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY levelChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(qint64 scannedBytes READ scannedBytes NOTIFY progressChanged)
    Q_PROPERTY(int scannedFiles READ scannedFiles NOTIFY progressChanged)
    // currentPath's children, largest first: {name, path, size, isDir, files}.
    // Files too small to draw are folded into one "smaller items" entry.
    Q_PROPERTY(QVariantList items READ items NOTIFY levelChanged)
    Q_PROPERTY(qint64 currentSize READ currentSize NOTIFY levelChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    struct Node {
        QString name;
        qint64 size = 0;
        int files = 0;
        bool isDir = false;
        int parent = -1;
        QList<int> children;
    };

    explicit DiskUsage(QObject *parent = nullptr);
    ~DiskUsage() override;

    QString rootPath() const { return m_root; }
    QString currentPath() const;
    bool running() const { return m_running; }
    qint64 scannedBytes() const { return m_scannedBytes; }
    int scannedFiles() const { return m_scannedFiles; }
    QVariantList items() const;
    qint64 currentSize() const;
    QString error() const { return m_error; }

    Q_INVOKABLE void start(const QString &path);
    Q_INVOKABLE void cancel();
    // Into a folder shown in the map (its full path), or one level up.
    Q_INVOKABLE void enter(const QString &path);
    Q_INVOKABLE void up();

    // The scan, synchronous, for the worker and the tests. Node 0 is `root`.
    // `progress` is called now and then with the bytes and files so far.
    static QList<Node> scan(const QString &root, const std::function<bool()> &cancelled,
                            const std::function<void(qint64, int)> &progress);
    // The path of node `index`, rebuilt from the names up to the root.
    static QString pathOf(const QList<Node> &nodes, const QString &root, int index);

Q_SIGNALS:
    void stateChanged();
    void progressChanged();
    void levelChanged();

private:
    QString m_root;
    bool m_running = false;
    qint64 m_scannedBytes = 0;
    int m_scannedFiles = 0;
    QString m_error;
    QList<Node> m_nodes;
    int m_current = 0;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
