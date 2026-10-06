#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QtQmlIntegration>

// Git status for the folder a tab shows: which entries are modified, new,
// untracked, ignored or conflicted, as badges beside their names. One per
// tab; asks `git` asynchronously and never blocks the view.
//
// Paths in `git status --porcelain` are relative to the repository root;
// every entry is folded onto the child of `folder` that contains it, so a
// folder shows "modified" when anything beneath it is.
class GitStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool isRepo READ isRepo NOTIFY statusChanged)
    Q_PROPERTY(QString branch READ branch NOTIFY statusChanged)
    // Bumps whenever new results land, for delegate bindings.
    Q_PROPERTY(int revision READ revision NOTIFY statusChanged)
    // Names in `folder` that .gitignore covers — what "hide ignored" hides.
    Q_PROPERTY(QStringList ignoredNames READ ignoredNames NOTIFY statusChanged)

public:
    explicit GitStatus(QObject *parent = nullptr);
    ~GitStatus() override;

    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool isRepo() const { return m_isRepo; }
    QString branch() const { return m_branch; }
    int revision() const { return m_revision; }
    QStringList ignoredNames() const { return m_ignored; }

    // "modified" | "added" | "untracked" | "ignored" | "conflict" | ""
    Q_INVOKABLE QString statusOf(const QString &name) const;
    // Asks git again, after a short pause (a burst of changes asks once).
    Q_INVOKABLE void refresh();

    // The folding, public for the tests: porcelain v1 -z output, the repo
    // root and the folder → status per child name, plus the ignored names.
    static QHash<QString, QString> parse(const QByteArray &porcelain, const QString &root,
                                         const QString &folder, QStringList *ignored = nullptr);

Q_SIGNALS:
    void folderChanged();
    void enabledChanged();
    void statusChanged();

private:
    void start();
    void runStatus();
    void clear();
    void killProcess(QPointer<QProcess> &process);

    QString m_folder;
    bool m_enabled = false;
    bool m_isRepo = false;
    QString m_branch;
    QString m_root;
    int m_revision = 0;
    QHash<QString, QString> m_status;
    QStringList m_ignored;
    QTimer m_debounce;
    QPointer<QProcess> m_rootProcess;
    QPointer<QProcess> m_statusProcess;
    quint64 m_generation = 0;
};
