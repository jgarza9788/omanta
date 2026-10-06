#include "DiskUsage.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <QVariantMap>

#include <algorithm>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Files past this many per folder are folded into their folder's total
// rather than kept as nodes: a folder of 200 000 thumbnails must not cost
// 200 000 nodes, and nobody can see a box that small anyway.
constexpr int kFilesPerFolder = 200;

qint64 onDisk(const struct stat &st)
{
    return qint64(st.st_blocks) * 512;
}

} // namespace

DiskUsage::DiskUsage(QObject *parent)
    : QObject(parent)
{
}

DiskUsage::~DiskUsage()
{
    if (m_cancel)
        m_cancel->store(true);
}

QList<DiskUsage::Node> DiskUsage::scan(const QString &root, const std::function<bool()> &cancelled,
                                       const std::function<void(qint64, int)> &progress)
{
    QList<Node> nodes;
    struct stat rootStat;
    const QByteArray nativeRoot = QFile::encodeName(root);
    if (::lstat(nativeRoot.constData(), &rootStat) != 0 || !S_ISDIR(rootStat.st_mode))
        return nodes;
    const dev_t device = rootStat.st_dev;

    Node top;
    top.name = root;
    top.isDir = true;
    top.size = onDisk(rootStat);
    nodes.append(top);

    QElapsedTimer clock;
    clock.start();
    qint64 bytes = 0;
    int files = 0;

    // Depth-first with an explicit stack of (node index, open fd): openat on
    // the parent's descriptor, so a folder renamed mid-scan can't redirect
    // the walk somewhere else.
    struct Frame {
        int node;
        DIR *dir;
        QList<QPair<qint64, QString>> smallFiles;
    };
    QList<Frame> stack;
    DIR *rootDir = ::opendir(nativeRoot.constData());
    if (!rootDir)
        return nodes;
    stack.append({ 0, rootDir, {} });

    while (!stack.isEmpty()) {
        if (cancelled()) {
            for (Frame &frame : stack)
                ::closedir(frame.dir);
            return {};
        }
        Frame &frame = stack.last();
        struct dirent *entry = ::readdir(frame.dir);
        if (!entry) {
            // Keep the largest files as nodes; the rest only add to the total.
            std::sort(frame.smallFiles.begin(), frame.smallFiles.end(),
                      [](const auto &a, const auto &b) { return a.first > b.first; });
            for (int i = 0; i < frame.smallFiles.size() && i < kFilesPerFolder; ++i) {
                Node file;
                file.name = frame.smallFiles.at(i).second;
                file.size = frame.smallFiles.at(i).first;
                file.files = 1;
                file.parent = frame.node;
                nodes[frame.node].children.append(int(nodes.size()));
                nodes.append(file);
            }
            ::closedir(frame.dir);
            const int finished = frame.node;
            stack.removeLast();
            if (!stack.isEmpty()) {
                Node &parent = nodes[stack.last().node];
                parent.size += nodes.at(finished).size;
                parent.files += nodes.at(finished).files;
            }
            continue;
        }
        const char *name = entry->d_name;
        if (qstrcmp(name, ".") == 0 || qstrcmp(name, "..") == 0)
            continue;
        const int fd = ::dirfd(frame.dir);
        struct stat st;
        if (::fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
            continue;
        const qint64 size = onDisk(st);
        if (S_ISDIR(st.st_mode)) {
            if (st.st_dev != device)
                continue; // another filesystem: not this folder's space
            const int childFd = ::openat(fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            DIR *child = childFd >= 0 ? ::fdopendir(childFd) : nullptr;
            if (!child) {
                if (childFd >= 0)
                    ::close(childFd);
                continue;
            }
            Node folder;
            folder.name = QFile::decodeName(name);
            folder.isDir = true;
            folder.size = size;
            folder.parent = frame.node;
            const int index = int(nodes.size());
            nodes[frame.node].children.append(index);
            nodes.append(folder);
            stack.append({ index, child, {} });
        } else {
            frame.smallFiles.append({ size, QFile::decodeName(name) });
            Node &parent = nodes[frame.node];
            parent.size += size;
            parent.files += 1;
            bytes += size;
            ++files;
            if (progress && clock.elapsed() > 150) {
                clock.restart();
                progress(bytes, files);
            }
        }
    }
    if (progress)
        progress(bytes, files);
    return nodes;
}

QString DiskUsage::pathOf(const QList<Node> &nodes, const QString &root, int index)
{
    QStringList parts;
    while (index > 0 && index < nodes.size()) {
        parts.prepend(nodes.at(index).name);
        index = nodes.at(index).parent;
    }
    QString path = root;
    for (const QString &part : std::as_const(parts))
        path += (path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + part;
    return path;
}

QString DiskUsage::currentPath() const
{
    return m_nodes.isEmpty() ? m_root : pathOf(m_nodes, m_root, m_current);
}

qint64 DiskUsage::currentSize() const
{
    return m_current < m_nodes.size() ? m_nodes.at(m_current).size : 0;
}

QVariantList DiskUsage::items() const
{
    QVariantList out;
    if (m_current >= m_nodes.size())
        return out;
    const Node &node = m_nodes.at(m_current);
    QList<int> children = node.children;
    std::sort(children.begin(), children.end(),
              [this](int a, int b) { return m_nodes.at(a).size > m_nodes.at(b).size; });
    const QString base = currentPath();
    qint64 shown = 0;
    int shownFiles = 0;
    for (int index : std::as_const(children)) {
        const Node &child = m_nodes.at(index);
        // Below half a percent a box is a sliver; fold it.
        if (out.size() >= 60 || (node.size > 0 && child.size * 200 < node.size))
            break;
        QVariantMap row;
        row.insert(QStringLiteral("name"), child.name);
        row.insert(QStringLiteral("path"), base + (base.endsWith(QLatin1Char('/')) ? "" : "/") + child.name);
        row.insert(QStringLiteral("size"), double(child.size));
        row.insert(QStringLiteral("isDir"), child.isDir);
        row.insert(QStringLiteral("files"), child.files);
        out.append(row);
        shown += child.size;
        shownFiles += child.files;
    }
    const qint64 rest = node.size - shown;
    if (rest > 0 && node.size > 0 && rest * 200 >= node.size) {
        QVariantMap row;
        row.insert(QStringLiteral("name"), tr("Smaller items"));
        row.insert(QStringLiteral("path"), QString());
        row.insert(QStringLiteral("size"), double(rest));
        row.insert(QStringLiteral("isDir"), false);
        row.insert(QStringLiteral("files"), node.files - shownFiles);
        out.append(row);
    }
    return out;
}

void DiskUsage::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
    if (m_running) {
        ++m_generation;
        m_running = false;
        Q_EMIT stateChanged();
    }
}

void DiskUsage::start(const QString &path)
{
    cancel();
    m_root = path;
    m_nodes.clear();
    m_current = 0;
    m_scannedBytes = 0;
    m_scannedFiles = 0;
    m_error.clear();
    if (!path.startsWith(QLatin1Char('/')) || !QFileInfo(path).isDir()) {
        m_error = tr("Disk usage works on local folders.");
        Q_EMIT stateChanged();
        Q_EMIT levelChanged();
        return;
    }
    m_running = true;
    Q_EMIT stateChanged();
    Q_EMIT levelChanged();
    Q_EMIT progressChanged();

    const quint64 generation = ++m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto flag = m_cancel;
    QPointer<DiskUsage> self(this);
    QThread *thread = QThread::create([self, flag, path, generation] {
        const QList<Node> nodes = scan(path, [flag] { return flag->load(); },
            [self, generation](qint64 bytes, int files) {
                QMetaObject::invokeMethod(qApp, [self, generation, bytes, files] {
                    if (!self || generation != self->m_generation)
                        return;
                    self->m_scannedBytes = bytes;
                    self->m_scannedFiles = files;
                    Q_EMIT self->progressChanged();
                }, Qt::QueuedConnection);
            });
        if (flag->load())
            return;
        QMetaObject::invokeMethod(qApp, [self, nodes, generation] {
            if (!self || generation != self->m_generation)
                return;
            self->m_nodes = nodes;
            self->m_current = 0;
            self->m_running = false;
            if (nodes.isEmpty())
                self->m_error = tr("The folder could not be read.");
            Q_EMIT self->stateChanged();
            Q_EMIT self->levelChanged();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DiskUsage::enter(const QString &path)
{
    if (m_current >= m_nodes.size())
        return;
    const QString base = currentPath();
    for (int index : m_nodes.at(m_current).children) {
        const Node &child = m_nodes.at(index);
        if (child.isDir && base + (base.endsWith(QLatin1Char('/')) ? "" : "/") + child.name == path) {
            m_current = index;
            Q_EMIT levelChanged();
            return;
        }
    }
}

void DiskUsage::up()
{
    if (m_current > 0 && m_current < m_nodes.size()) {
        m_current = m_nodes.at(m_current).parent;
        Q_EMIT levelChanged();
    }
}
