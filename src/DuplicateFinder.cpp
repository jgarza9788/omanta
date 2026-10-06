#include "DuplicateFinder.h"
#include "Checksum.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct Candidate {
    QString path;
    qint64 size = 0;
};

QByteArray headHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(file.read(64 * 1024), QCryptographicHash::Sha256);
}

} // namespace

DuplicateFinder::DuplicateFinder(QObject *parent)
    : QObject(parent)
{
}

DuplicateFinder::~DuplicateFinder()
{
    if (m_cancel)
        m_cancel->store(true);
}

QList<DuplicateFinder::Group> DuplicateFinder::find(const QString &root, bool includeHidden,
                                                    const std::function<bool()> &cancelled,
                                                    const std::function<void(const QString &)> &status)
{
    QHash<qint64, QList<Candidate>> bySize;
    QSet<QPair<quint64, quint64>> seenInodes;
    QElapsedTimer clock;
    clock.start();
    int looked = 0;

    QDir::Filters filters = QDir::Files | QDir::NoDotAndDotDot;
    if (includeHidden)
        filters |= QDir::Hidden;
    QDirIterator it(root, filters, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancelled())
            return {};
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink() || info.size() <= 0)
            continue;
        if (!includeHidden && path.mid(root.size()).contains(QLatin1String("/.")))
            continue; // inside a hidden folder
        struct stat st;
        if (::lstat(QFile::encodeName(path).constData(), &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        // A second hard link to a file already seen is the same file.
        const QPair<quint64, quint64> inode(quint64(st.st_dev), quint64(st.st_ino));
        if (seenInodes.contains(inode))
            continue;
        seenInodes.insert(inode);
        bySize[info.size()].append({ path, info.size() });
        ++looked;
        if (status && clock.elapsed() > 200) {
            clock.restart();
            status(QCoreApplication::translate("DuplicateFinder", "Looking at %1 files…").arg(looked));
        }
    }

    // Narrow by the first 64 KB, then confirm with the whole file.
    QList<Group> groups;
    int compared = 0;
    for (auto sizeIt = bySize.cbegin(); sizeIt != bySize.cend(); ++sizeIt) {
        if (sizeIt->size() < 2)
            continue;
        QHash<QByteArray, QStringList> byHead;
        for (const Candidate &candidate : *sizeIt) {
            if (cancelled())
                return {};
            const QByteArray head = headHash(candidate.path);
            if (!head.isEmpty())
                byHead[head].append(candidate.path);
        }
        for (const QStringList &tied : std::as_const(byHead)) {
            if (tied.size() < 2)
                continue;
            QHash<QString, QStringList> byFull;
            for (const QString &path : tied) {
                if (cancelled())
                    return {};
                ++compared;
                if (status && clock.elapsed() > 200) {
                    clock.restart();
                    status(QCoreApplication::translate("DuplicateFinder", "Comparing %1 files…")
                               .arg(compared));
                }
                // Small files: the head hash already covered every byte.
                QString key;
                if (sizeIt.key() <= 64 * 1024) {
                    key = QString::fromLatin1(headHash(path).toHex());
                } else {
                    std::atomic_bool stop(false);
                    key = Checksum::compute(path, QCryptographicHash::Sha256, stop);
                }
                if (!key.isEmpty())
                    byFull[key].append(path);
            }
            for (QStringList paths : std::as_const(byFull)) {
                if (paths.size() < 2)
                    continue;
                paths.sort();
                groups.append({ sizeIt.key(), paths });
            }
        }
    }
    std::sort(groups.begin(), groups.end(), [](const Group &a, const Group &b) {
        const qint64 wasteA = a.size * (a.paths.size() - 1);
        const qint64 wasteB = b.size * (b.paths.size() - 1);
        return wasteA != wasteB ? wasteA > wasteB : a.paths.first() < b.paths.first();
    });
    return groups;
}

void DuplicateFinder::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
    if (m_running) {
        ++m_generation;
        m_running = false;
        Q_EMIT stateChanged();
    }
}

void DuplicateFinder::start(const QString &path, bool includeHidden)
{
    cancel();
    m_root = path;
    m_groups.clear();
    m_wasted = 0;
    m_error.clear();
    m_status.clear();
    if (!path.startsWith(QLatin1Char('/')) || !QFileInfo(path).isDir()) {
        m_error = tr("Finding duplicates works on local folders.");
        Q_EMIT stateChanged();
        return;
    }
    m_running = true;
    Q_EMIT stateChanged();

    const quint64 generation = ++m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto flag = m_cancel;
    QPointer<DuplicateFinder> self(this);
    QThread *thread = QThread::create([self, flag, path, includeHidden, generation] {
        const QList<Group> groups = find(path, includeHidden, [flag] { return flag->load(); },
            [self, generation](const QString &text) {
                QMetaObject::invokeMethod(qApp, [self, generation, text] {
                    if (!self || generation != self->m_generation)
                        return;
                    self->m_status = text;
                    Q_EMIT self->progressChanged();
                }, Qt::QueuedConnection);
            });
        if (flag->load())
            return;
        QMetaObject::invokeMethod(qApp, [self, groups, generation] {
            if (!self || generation != self->m_generation)
                return;
            QVariantList out;
            qint64 wasted = 0;
            for (const Group &group : groups) {
                QVariantMap row;
                row.insert(QStringLiteral("size"), double(group.size));
                row.insert(QStringLiteral("paths"), group.paths);
                out.append(row);
                wasted += group.size * (group.paths.size() - 1);
            }
            self->m_groups = out;
            self->m_wasted = wasted;
            self->m_running = false;
            self->m_status.clear();
            Q_EMIT self->progressChanged();
            Q_EMIT self->stateChanged();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

int DuplicateFinder::replaceWithLinks(const QString &keep, const QStringList &duplicates)
{
    m_lastErrors.clear();
    struct stat keepStat;
    const QByteArray nativeKeep = QFile::encodeName(keep);
    if (::lstat(nativeKeep.constData(), &keepStat) != 0 || !S_ISREG(keepStat.st_mode)) {
        m_lastErrors << tr("“%1” is no longer there.").arg(QFileInfo(keep).fileName());
        return 0;
    }
    int replaced = 0;
    for (const QString &duplicate : duplicates) {
        const QString name = QFileInfo(duplicate).fileName();
        struct stat st;
        const QByteArray native = QFile::encodeName(duplicate);
        if (::lstat(native.constData(), &st) != 0 || !S_ISREG(st.st_mode)) {
            m_lastErrors << tr("“%1” is no longer there.").arg(name);
            continue;
        }
        if (st.st_dev != keepStat.st_dev) {
            m_lastErrors << tr("“%1” is on another drive; hard links can't cross drives.").arg(name);
            continue;
        }
        if (st.st_ino == keepStat.st_ino)
            continue; // already the same file
        if (st.st_size != keepStat.st_size) {
            m_lastErrors << tr("“%1” changed since the search.").arg(name);
            continue;
        }
        const QString temporary = QFileInfo(duplicate).absolutePath() + QStringLiteral("/.omanta-link-")
                                + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QByteArray nativeTemporary = QFile::encodeName(temporary);
        if (::link(nativeKeep.constData(), nativeTemporary.constData()) != 0) {
            m_lastErrors << tr("Could not link “%1”: %2").arg(name, QString::fromLocal8Bit(strerror(errno)));
            continue;
        }
        if (::rename(nativeTemporary.constData(), native.constData()) != 0) {
            m_lastErrors << tr("Could not replace “%1”: %2").arg(name, QString::fromLocal8Bit(strerror(errno)));
            ::unlink(nativeTemporary.constData());
            continue;
        }
        ++replaced;
    }
    return replaced;
}
