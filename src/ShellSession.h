#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QtQmlIntegration>

// The terminal pane's engine: runs one shell command at a time in the folder
// the tab shows and collects what it prints. `cd` is handled here and moves
// the tab, so the pane and the files never disagree about where you are.
//
// It is a command runner, not a terminal emulator: there is no TTY, so
// full-screen programs (vim, htop, less) belong in a real terminal — the
// pane's button opens one in the same folder.
class ShellSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    // Commands run so far, oldest first, for ↑/↓ in the input line.
    Q_PROPERTY(QStringList history READ history NOTIFY historyChanged)

public:
    explicit ShellSession(QObject *parent = nullptr);
    ~ShellSession() override;

    QString directory() const { return m_directory; }
    void setDirectory(const QString &directory);
    bool running() const { return m_process != nullptr; }
    QString output() const { return m_output; }
    QStringList history() const { return m_history; }

    Q_INVOKABLE void run(const QString &command);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();

    // Where `cd <argument>` from `directory` leads ("" when nowhere real).
    static QString resolveCd(const QString &argument, const QString &directory);

    static constexpr int kMaxOutput = 256 * 1024;

Q_SIGNALS:
    void directoryChanged();
    void runningChanged();
    void outputChanged();
    void historyChanged();
    // `cd` asked to go somewhere; the tab navigates.
    void changeDirectoryRequested(const QString &path);

private:
    void append(const QString &text);

    QString m_directory;
    QString m_output;
    QStringList m_history;
    QPointer<QProcess> m_process;
};
