#include "BatchPermissions.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThread>

#include <cerrno>
#include <cstring>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

BatchPermissions::BatchPermissions(QObject *parent)
    : QObject(parent)
{
}

BatchPermissions::~BatchPermissions()
{
    if (m_cancel)
        m_cancel->store(true);
}

BatchPermissions::Outcome BatchPermissions::run(const Request &request,
                                                const std::function<bool()> &cancelled)
{
    Outcome outcome;
    const auto fail = [&outcome](const QString &path, const QString &why) {
        ++outcome.failed;
        if (outcome.errors.size() < 20)
            outcome.errors << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), why);
    };

    uid_t uid = uid_t(-1);
    gid_t gid = gid_t(-1);
    if (!request.owner.isEmpty()) {
        const struct passwd *pw = ::getpwnam(request.owner.toLocal8Bit().constData());
        if (!pw) {
            fail(request.owner, QCoreApplication::translate("BatchPermissions", "no such user"));
            return outcome;
        }
        uid = pw->pw_uid;
    }
    if (!request.group.isEmpty()) {
        const struct group *gr = ::getgrnam(request.group.toLocal8Bit().constData());
        if (!gr) {
            fail(request.group, QCoreApplication::translate("BatchPermissions", "no such group"));
            return outcome;
        }
        gid = gr->gr_gid;
    }

    const auto visit = [&](const QString &path) {
        struct stat st;
        const QByteArray native = QFile::encodeName(path);
        if (::lstat(native.constData(), &st) != 0) {
            fail(path, QString::fromLocal8Bit(std::strerror(errno)));
            return;
        }
        if (S_ISLNK(st.st_mode))
            return;
        const bool isDir = S_ISDIR(st.st_mode);
        if ((request.scope == QLatin1String("files") && isDir)
            || (request.scope == QLatin1String("folders") && !isDir))
            return;
        bool touched = false;
        const mode_t old = st.st_mode & 07777;
        const mode_t wanted = (old & ~mode_t(request.clearBits)) | mode_t(request.setBits);
        if (wanted != old) {
            if (::chmod(native.constData(), wanted) != 0) {
                fail(path, QString::fromLocal8Bit(std::strerror(errno)));
                return;
            }
            touched = true;
        }
        if ((uid != uid_t(-1) && uid != st.st_uid) || (gid != gid_t(-1) && gid != st.st_gid)) {
            if (::lchown(native.constData(), uid, gid) != 0) {
                fail(path, QString::fromLocal8Bit(std::strerror(errno)));
                return;
            }
            touched = true;
        }
        if (touched)
            ++outcome.changed;
    };

    for (const QString &path : request.paths) {
        if (cancelled())
            break;
        visit(path);
        const QFileInfo info(path);
        if (!request.recursive || !info.isDir() || info.isSymLink())
            continue;
        QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancelled())
                break;
            visit(it.next());
        }
    }
    return outcome;
}

void BatchPermissions::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

void BatchPermissions::apply(const QStringList &paths, int setBits, int clearBits, bool recursive,
                             const QString &scope, const QString &owner, const QString &group)
{
    cancel();
    Request request;
    for (const QString &path : paths) {
        if (path.startsWith(QLatin1Char('/')))
            request.paths << path;
    }
    request.setBits = setBits & 07777;
    request.clearBits = clearBits & 07777;
    request.recursive = recursive;
    request.scope = scope;
    request.owner = owner.trimmed();
    request.group = group.trimmed();

    m_outcome = Outcome();
    m_running = true;
    Q_EMIT stateChanged();

    const quint64 generation = ++m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto flag = m_cancel;
    QPointer<BatchPermissions> self(this);
    QThread *thread = QThread::create([self, flag, request, generation] {
        const Outcome outcome = run(request, [flag] { return flag->load(); });
        QMetaObject::invokeMethod(qApp, [self, outcome, generation] {
            if (!self || generation != self->m_generation)
                return;
            self->m_outcome = outcome;
            self->m_running = false;
            Q_EMIT self->stateChanged();
            Q_EMIT self->finished();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
