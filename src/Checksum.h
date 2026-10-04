#pragma once

#include <QCryptographicHash>
#include <QObject>
#include <QString>
#include <QtQmlIntegration>

#include <atomic>
#include <memory>

// A file's checksum, computed on a worker thread with progress and cancel —
// Properties and the info panel ask for one on demand, never on open: hashing
// a 40 GB image because a dialog appeared is not on.
class Checksum : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    // "sha256" | "sha1" | "md5"
    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm NOTIFY algorithmChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY stateChanged)
    // Lower-case hex once finished, empty before.
    Q_PROPERTY(QString result READ result NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    explicit Checksum(QObject *parent = nullptr);
    ~Checksum() override;

    QString path() const { return m_path; }
    void setPath(const QString &path);
    QString algorithm() const { return m_algorithm; }
    void setAlgorithm(const QString &algorithm);
    bool running() const { return m_running; }
    qreal progress() const { return m_progress; }
    QString result() const { return m_result; }
    QString error() const { return m_error; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

    // Whether `text` (say, pasted from a download page) names this result:
    // trimmed, case-insensitive, and the hex may sit inside a longer line
    // such as `sha256sum` output.
    Q_INVOKABLE bool matches(const QString &text) const;

    // Synchronous, for the worker and the tests. Empty on failure or cancel.
    static QString compute(const QString &path, QCryptographicHash::Algorithm algorithm,
                           const std::atomic_bool &cancelled,
                           const std::function<void(qint64, qint64)> &progress = {},
                           QString *error = nullptr);

Q_SIGNALS:
    void pathChanged();
    void algorithmChanged();
    void stateChanged();

private:
    void reset();

    QString m_path;
    QString m_algorithm = QStringLiteral("sha256");
    bool m_running = false;
    qreal m_progress = 0;
    QString m_result;
    QString m_error;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
