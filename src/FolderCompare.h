#pragma once

#include <QDateTime>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration>

#include <atomic>
#include <functional>
#include <memory>

// Compares two folders — the split view's two panes — file by file, through
// every subfolder, on a worker thread. Each difference becomes one entry:
//
//   onlyLeft / onlyRight     exists on one side (a folder that exists on one
//                            side only is one entry; its contents aren't listed)
//   newerLeft / newerRight   on both sides, one modified later
//   differ                   same time, different size
//   same                     same size, modified within two seconds
//
// The window then copies across or mirrors through FileOperations, so every
// sync step lands in the undo history like any other copy.
class FolderCompare : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString leftPath READ leftPath WRITE setLeftPath NOTIFY pathsChanged)
    Q_PROPERTY(QString rightPath READ rightPath WRITE setRightPath NOTIFY pathsChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    // Differences first (onlyLeft, newerLeft, differ, newerRight, onlyRight),
    // then identical files; each {path, state, isDir, leftSize, rightSize,
    // leftModified, rightModified}. `path` is relative to both roots.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY stateChanged)
    // How many of each state, keyed by state name.
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY stateChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    struct Entry {
        QString path;
        QString state;
        bool isDir = false;
        qint64 leftSize = -1;
        qint64 rightSize = -1;
        QDateTime leftModified;
        QDateTime rightModified;
    };

    explicit FolderCompare(QObject *parent = nullptr);
    ~FolderCompare() override;

    QString leftPath() const { return m_left; }
    void setLeftPath(const QString &path);
    QString rightPath() const { return m_right; }
    void setRightPath(const QString &path);
    bool running() const { return m_running; }
    QVariantList entries() const { return m_entries; }
    QVariantMap counts() const { return m_counts; }
    bool truncated() const { return m_truncated; }
    QString error() const { return m_error; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

    // The comparison itself, synchronous, for the worker and the tests.
    static QList<Entry> compare(const QString &left, const QString &right,
                                const std::function<bool()> &cancelled, int limit,
                                bool *truncated);

    static constexpr int kLimit = 50000;

Q_SIGNALS:
    void pathsChanged();
    void stateChanged();

private:
    QString m_left;
    QString m_right;
    bool m_running = false;
    QVariantList m_entries;
    QVariantMap m_counts;
    bool m_truncated = false;
    QString m_error;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
