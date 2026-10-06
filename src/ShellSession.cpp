#include "ShellSession.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

ShellSession::ShellSession(QObject *parent)
    : QObject(parent)
{
}

ShellSession::~ShellSession()
{
    stop();
}

void ShellSession::setDirectory(const QString &directory)
{
    if (m_directory == directory)
        return;
    m_directory = directory;
    Q_EMIT directoryChanged();
}

void ShellSession::append(const QString &text)
{
    m_output += text;
    // Keep the tail: what just happened is what you're looking at.
    if (m_output.size() > kMaxOutput)
        m_output = m_output.right(kMaxOutput * 3 / 4);
    Q_EMIT outputChanged();
}

void ShellSession::clear()
{
    m_output.clear();
    Q_EMIT outputChanged();
}

QString ShellSession::resolveCd(const QString &argument, const QString &directory)
{
    QString target = argument.trimmed();
    if ((target.startsWith(QLatin1Char('"')) && target.endsWith(QLatin1Char('"')))
        || (target.startsWith(QLatin1Char('\'')) && target.endsWith(QLatin1Char('\''))))
        target = target.mid(1, target.size() - 2);
    if (target.isEmpty() || target == QLatin1String("~"))
        return QDir::homePath();
    if (target.startsWith(QLatin1String("~/")))
        target = QDir::homePath() + target.mid(1);
    const QString path = QDir::cleanPath(QDir(directory).absoluteFilePath(target));
    return QFileInfo(path).isDir() ? path : QString();
}

void ShellSession::run(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty() || m_process)
        return;
    if (m_history.isEmpty() || m_history.last() != trimmed) {
        m_history.append(trimmed);
        if (m_history.size() > 200)
            m_history.removeFirst();
        Q_EMIT historyChanged();
    }
    append(QStringLiteral("$ ") + trimmed + QLatin1Char('\n'));

    if (trimmed == QLatin1String("clear")) {
        clear();
        return;
    }
    // `cd` moves the tab; a shell child can't change our directory.
    if (trimmed == QLatin1String("cd") || trimmed.startsWith(QLatin1String("cd "))) {
        const QString target = resolveCd(trimmed.mid(2), m_directory);
        if (target.isEmpty())
            append(tr("cd: no such folder: %1\n").arg(trimmed.mid(3).trimmed()));
        else
            Q_EMIT changeDirectoryRequested(target);
        return;
    }
    if (!QFileInfo(m_directory).isDir()) {
        append(tr("Commands run in folders on this computer.\n"));
        return;
    }

    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty() || !QFileInfo(shell).isExecutable())
        shell = QStandardPaths::findExecutable(QStringLiteral("bash"));
    if (shell.isEmpty())
        shell = QStringLiteral("/bin/sh");

    auto *process = new QProcess(this);
    process->setProgram(shell);
    process->setArguments({ QStringLiteral("-c"), trimmed });
    process->setWorkingDirectory(m_directory);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // No terminal behind us: ask programs for plain, uncoloured output.
    environment.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    environment.insert(QStringLiteral("PAGER"), QStringLiteral("cat"));
    environment.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    process->setProcessEnvironment(environment);
    process->setStandardInputFile(QProcess::nullDevice());

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        append(QString::fromLocal8Bit(process->readAllStandardOutput()));
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        append(QString::fromLocal8Bit(process->readAllStandardOutput()));
        if (status == QProcess::CrashExit)
            append(tr("[stopped]\n"));
        else if (code != 0)
            append(tr("[exit %1]\n").arg(code));
        process->deleteLater();
        m_process = nullptr;
        Q_EMIT runningChanged();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        append(tr("Could not start %1\n").arg(process->program()));
        process->deleteLater();
        m_process = nullptr;
        Q_EMIT runningChanged();
    });
    m_process = process;
    Q_EMIT runningChanged();
    process->start();
}

void ShellSession::stop()
{
    if (!m_process)
        return;
    m_process->terminate();
    if (!m_process->waitForFinished(1000))
        m_process->kill();
}
