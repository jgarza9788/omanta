#include "GitStatus.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

// Which badge wins when several entries fold onto one folder.
int rank(const QString &status)
{
    if (status == QLatin1String("conflict")) return 5;
    if (status == QLatin1String("modified")) return 4;
    if (status == QLatin1String("added")) return 3;
    if (status == QLatin1String("untracked")) return 2;
    if (status == QLatin1String("ignored")) return 1;
    return 0;
}

QString statusFor(char x, char y)
{
    if (x == '?' && y == '?')
        return QStringLiteral("untracked");
    if (x == '!' && y == '!')
        return QStringLiteral("ignored");
    if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D'))
        return QStringLiteral("conflict");
    if (x == 'A' && (y == ' ' || y == 'M'))
        return y == 'M' ? QStringLiteral("modified") : QStringLiteral("added");
    return QStringLiteral("modified");
}

} // namespace

GitStatus::GitStatus(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(400);
    connect(&m_debounce, &QTimer::timeout, this, &GitStatus::start);
}

GitStatus::~GitStatus()
{
    killProcess(m_rootProcess);
    killProcess(m_statusProcess);
}

void GitStatus::killProcess(QPointer<QProcess> &process)
{
    if (!process)
        return;
    process->disconnect(this);
    process->kill();
    process->waitForFinished(200);
    process->deleteLater();
    process = nullptr;
}

void GitStatus::setFolder(const QString &folder)
{
    if (m_folder == folder)
        return;
    m_folder = folder;
    Q_EMIT folderChanged();
    clear();
    if (m_enabled)
        start(); // a new folder asks at once
}

void GitStatus::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    Q_EMIT enabledChanged();
    clear();
    if (m_enabled)
        start();
}

void GitStatus::clear()
{
    ++m_generation;
    killProcess(m_rootProcess);
    killProcess(m_statusProcess);
    const bool had = m_isRepo || !m_status.isEmpty();
    m_isRepo = false;
    m_branch.clear();
    m_root.clear();
    m_status.clear();
    m_ignored.clear();
    if (had) {
        ++m_revision;
        Q_EMIT statusChanged();
    }
}

void GitStatus::refresh()
{
    if (m_enabled && !m_folder.isEmpty())
        m_debounce.start();
}

QString GitStatus::statusOf(const QString &name) const
{
    return m_status.value(name);
}

void GitStatus::start()
{
    if (!m_enabled || !m_folder.startsWith(QLatin1Char('/')) || !QFileInfo(m_folder).isDir())
        return;
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty())
        return;
    killProcess(m_rootProcess);
    killProcess(m_statusProcess);

    const quint64 generation = ++m_generation;
    m_rootProcess = new QProcess(this);
    m_rootProcess->setProgram(git);
    m_rootProcess->setArguments({ QStringLiteral("-C"), m_folder, QStringLiteral("rev-parse"),
                                  QStringLiteral("--show-toplevel"),
                                  QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD") });
    QProcess *process = m_rootProcess;
    connect(process, &QProcess::finished, this, [this, process, generation](int code) {
        process->deleteLater();
        if (generation != m_generation)
            return;
        m_rootProcess = nullptr;
        const QStringList lines = QString::fromUtf8(process->readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (code != 0 || lines.isEmpty()) {
            // Not a repository, or a fresh one with no commits (no HEAD) —
            // the second case still has a root on the first line.
            if (lines.isEmpty() || !lines.first().startsWith(QLatin1Char('/'))) {
                if (m_isRepo) {
                    m_isRepo = false;
                    ++m_revision;
                    Q_EMIT statusChanged();
                }
                return;
            }
        }
        m_root = lines.first();
        m_branch = lines.size() > 1 ? lines.at(1) : QString();
        runStatus();
    });
    process->start();
}

void GitStatus::runStatus()
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    const quint64 generation = m_generation;
    m_statusProcess = new QProcess(this);
    m_statusProcess->setProgram(git);
    // Ignored directories are listed as "dir/" without descending into them
    // (traditional mode), so a node_modules costs one line, not thousands.
    m_statusProcess->setArguments({ QStringLiteral("-C"), m_folder,
                                    QStringLiteral("--no-optional-locks"),
                                    QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
                                    QStringLiteral("-z"), QStringLiteral("--ignored=traditional"),
                                    QStringLiteral("--untracked-files=normal"),
                                    QStringLiteral("--"), QStringLiteral(".") });
    QProcess *process = m_statusProcess;
    connect(process, &QProcess::finished, this, [this, process, generation](int code) {
        process->deleteLater();
        if (generation != m_generation)
            return;
        m_statusProcess = nullptr;
        if (code != 0)
            return;
        m_status = parse(process->readAllStandardOutput(), m_root, m_folder, &m_ignored);
        m_isRepo = true;
        ++m_revision;
        Q_EMIT statusChanged();
    });
    process->start();
}

QHash<QString, QString> GitStatus::parse(const QByteArray &porcelain, const QString &root,
                                         const QString &folder, QStringList *ignored)
{
    QHash<QString, QString> out;
    if (ignored)
        ignored->clear();
    const QString cleanRoot = QDir::cleanPath(root);
    const QString cleanFolder = QDir::cleanPath(folder);
    QString prefix;
    if (cleanFolder != cleanRoot) {
        if (!cleanFolder.startsWith(cleanRoot + QLatin1Char('/')))
            return out;
        prefix = cleanFolder.mid(cleanRoot.size() + 1) + QLatin1Char('/');
    }

    const QList<QByteArray> records = porcelain.split('\0');
    for (int i = 0; i < records.size(); ++i) {
        const QByteArray &record = records.at(i);
        if (record.size() < 4)
            continue;
        const char x = record.at(0);
        const char y = record.at(1);
        // A rename or copy carries its old path as the next record.
        if (x == 'R' || x == 'C')
            ++i;
        QString path = QString::fromUtf8(record.mid(3));
        if (!prefix.isEmpty()) {
            if (!path.startsWith(prefix))
                continue;
            path = path.mid(prefix.size());
        }
        const bool trailingSlash = path.endsWith(QLatin1Char('/'));
        if (trailingSlash)
            path.chop(1);
        if (path.isEmpty())
            continue;
        const int slash = path.indexOf(QLatin1Char('/'));
        const bool direct = slash < 0;
        const QString name = direct ? path : path.left(slash);
        const QString status = statusFor(x, y);
        // An ignored file deep inside a tracked folder doesn't make the
        // folder ignored; only a direct child is.
        if (status == QLatin1String("ignored") && !direct)
            continue;
        if (status == QLatin1String("ignored") && ignored)
            ignored->append(name);
        if (rank(status) > rank(out.value(name)))
            out.insert(name, status);
    }
    return out;
}
