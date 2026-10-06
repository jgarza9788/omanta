#pragma once

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration>

#include <atomic>
#include <functional>
#include <memory>

// Changes permissions (and optionally owner and group) for many files at
// once, on a worker thread. Each permission bit is set, cleared or left as
// it is, so a mixed selection keeps what nobody asked to change. Can go
// through every subfolder, and limit itself to files or to folders — the
// classic "folders 755, files 644" in two passes.
//
// Symlinks are never followed or changed. Changing the owner needs the
// rights to do it (usually root); a refusal is reported per file.
class BatchPermissions : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(int changedCount READ changedCount NOTIFY stateChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY stateChanged)
    // The first few failures, one line each.
    Q_PROPERTY(QStringList errors READ errors NOTIFY stateChanged)

public:
    struct Request {
        QStringList paths;
        int setBits = 0;   // bits to turn on (0o7777 range)
        int clearBits = 0; // bits to turn off
        bool recursive = false;
        QString scope = QStringLiteral("all"); // all | files | folders
        QString owner; // user name, empty to leave alone
        QString group; // group name, empty to leave alone
    };
    struct Outcome {
        int changed = 0;
        int failed = 0;
        QStringList errors;
    };

    explicit BatchPermissions(QObject *parent = nullptr);
    ~BatchPermissions() override;

    bool running() const { return m_running; }
    int changedCount() const { return m_outcome.changed; }
    int failedCount() const { return m_outcome.failed; }
    QStringList errors() const { return m_outcome.errors; }

    Q_INVOKABLE void apply(const QStringList &paths, int setBits, int clearBits, bool recursive,
                           const QString &scope, const QString &owner, const QString &group);
    Q_INVOKABLE void cancel();

    // The work, synchronous, for the worker and the tests.
    static Outcome run(const Request &request, const std::function<bool()> &cancelled);

Q_SIGNALS:
    void stateChanged();
    void finished();

private:
    bool m_running = false;
    Outcome m_outcome;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
