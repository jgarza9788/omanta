#include "FolderCompare.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QThread>

#include <algorithm>

namespace {

struct Side {
    qint64 size = 0;
    QDateTime modified;
    bool isDir = false;
};

// Every entry below `root`, keyed by relative path; symlinks are compared as
// themselves (never followed), and nothing outside `root` is read.
QHash<QString, Side> walk(const QString &root, const std::function<bool()> &cancelled,
                          int limit, bool *truncated)
{
    QHash<QString, Side> out;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    const int prefix = int(root.size()) + (root.endsWith(QLatin1Char('/')) ? 0 : 1);
    while (it.hasNext()) {
        if (cancelled())
            break;
        if (out.size() >= limit) {
            *truncated = true;
            break;
        }
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        Side side;
        side.isDir = info.isDir() && !info.isSymLink();
        side.size = side.isDir ? 0 : info.size();
        side.modified = info.lastModified();
        out.insert(path.mid(prefix), side);
    }
    return out;
}

bool underOneSidedFolder(const QString &path, const QHash<QString, Side> &other,
                         const QHash<QString, Side> &self)
{
    // Is any ancestor of `path` a folder that `other` lacks? Then this entry
    // is part of that folder's single "only on one side" row.
    int slash = int(path.lastIndexOf(QLatin1Char('/')));
    while (slash > 0) {
        const QString parent = path.left(slash);
        if (self.value(parent).isDir && !other.contains(parent))
            return true;
        slash = int(parent.lastIndexOf(QLatin1Char('/')));
    }
    return false;
}

int order(const QString &state)
{
    static const QStringList states = { "onlyLeft", "newerLeft", "differ", "newerRight",
                                        "onlyRight", "same" };
    return int(states.indexOf(state));
}

} // namespace

FolderCompare::FolderCompare(QObject *parent)
    : QObject(parent)
{
}

FolderCompare::~FolderCompare()
{
    cancel();
}

void FolderCompare::setLeftPath(const QString &path)
{
    if (m_left == path)
        return;
    m_left = path;
    Q_EMIT pathsChanged();
}

void FolderCompare::setRightPath(const QString &path)
{
    if (m_right == path)
        return;
    m_right = path;
    Q_EMIT pathsChanged();
}

void FolderCompare::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
    if (m_running) {
        ++m_generation;
        m_running = false;
        Q_EMIT stateChanged();
    }
}

QList<FolderCompare::Entry> FolderCompare::compare(const QString &left, const QString &right,
                                                   const std::function<bool()> &cancelled,
                                                   int limit, bool *truncated)
{
    bool cut = false;
    const QHash<QString, Side> a = walk(left, cancelled, limit, &cut);
    const QHash<QString, Side> b = walk(right, cancelled, limit, &cut);
    if (truncated)
        *truncated = cut;

    QList<Entry> out;
    for (auto it = a.cbegin(); it != a.cend(); ++it) {
        if (underOneSidedFolder(it.key(), b, a))
            continue;
        Entry entry;
        entry.path = it.key();
        entry.isDir = it->isDir;
        entry.leftSize = it->size;
        entry.leftModified = it->modified;
        const auto other = b.constFind(it.key());
        if (other == b.cend()) {
            entry.state = QStringLiteral("onlyLeft");
        } else {
            entry.rightSize = other->size;
            entry.rightModified = other->modified;
            if (it->isDir || other->isDir) {
                if (it->isDir && other->isDir)
                    continue; // a folder on both sides is compared by its contents
                entry.state = QStringLiteral("differ");
            } else {
                const qint64 delta = it->modified.msecsTo(other->modified);
                if (std::abs(delta) <= 2000)
                    entry.state = it->size == other->size ? QStringLiteral("same")
                                                          : QStringLiteral("differ");
                else
                    entry.state = delta < 0 ? QStringLiteral("newerLeft")
                                            : QStringLiteral("newerRight");
            }
        }
        out.append(entry);
    }
    for (auto it = b.cbegin(); it != b.cend(); ++it) {
        if (a.contains(it.key()) || underOneSidedFolder(it.key(), a, b))
            continue;
        Entry entry;
        entry.path = it.key();
        entry.isDir = it->isDir;
        entry.rightSize = it->size;
        entry.rightModified = it->modified;
        entry.state = QStringLiteral("onlyRight");
        out.append(entry);
    }
    std::sort(out.begin(), out.end(), [](const Entry &x, const Entry &y) {
        const int ox = order(x.state);
        const int oy = order(y.state);
        return ox != oy ? ox < oy : x.path.localeAwareCompare(y.path) < 0;
    });
    return out;
}

void FolderCompare::start()
{
    cancel();
    m_entries.clear();
    m_counts.clear();
    m_truncated = false;
    m_error.clear();
    if (!QFileInfo(m_left).isDir() || !QFileInfo(m_right).isDir()) {
        m_error = tr("Both panes must show a local folder.");
        Q_EMIT stateChanged();
        return;
    }
    if (QDir::cleanPath(m_left) == QDir::cleanPath(m_right)) {
        m_error = tr("Both panes show the same folder.");
        Q_EMIT stateChanged();
        return;
    }
    m_running = true;
    Q_EMIT stateChanged();

    const quint64 generation = ++m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto flag = m_cancel;
    const QString left = m_left;
    const QString right = m_right;
    QPointer<FolderCompare> self(this);
    QThread *thread = QThread::create([self, flag, left, right, generation] {
        bool truncated = false;
        const QList<Entry> result = compare(left, right, [flag] { return flag->load(); },
                                            kLimit, &truncated);
        if (flag->load())
            return;
        QMetaObject::invokeMethod(qApp, [self, result, truncated, generation] {
            if (!self || generation != self->m_generation)
                return;
            QVariantMap counts;
            QVariantList entries;
            for (const Entry &entry : result) {
                counts.insert(entry.state, counts.value(entry.state).toInt() + 1);
                QVariantMap row;
                row.insert(QStringLiteral("path"), entry.path);
                row.insert(QStringLiteral("state"), entry.state);
                row.insert(QStringLiteral("isDir"), entry.isDir);
                row.insert(QStringLiteral("leftSize"), double(entry.leftSize));
                row.insert(QStringLiteral("rightSize"), double(entry.rightSize));
                row.insert(QStringLiteral("leftModified"), entry.leftModified);
                row.insert(QStringLiteral("rightModified"), entry.rightModified);
                entries.append(row);
            }
            self->m_entries = entries;
            self->m_counts = counts;
            self->m_truncated = truncated;
            self->m_running = false;
            Q_EMIT self->stateChanged();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
