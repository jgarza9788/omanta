#include "ArchiveBrowser.h"
#include "ArchiveEngine.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>

ArchiveBrowser::ArchiveBrowser(QObject *parent)
    : QObject(parent)
{
    // Whatever an earlier run left behind is stale by definition.
    clearCache();
}

ArchiveBrowser::~ArchiveBrowser()
{
    cancel();
    clearCache();
}

QString ArchiveBrowser::cacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
         + QStringLiteral("/omanta/archives");
}

void ArchiveBrowser::clearCache()
{
    QDir root(cacheRoot());
    if (!root.exists())
        return;
    // Extraction strips write bits from nothing the user can't restore, but
    // a read-only folder still blocks removal of its children.
    QDirIterator::IteratorFlags flags = QDirIterator::Subdirectories;
    QDirIterator it(root.path(), QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, flags);
    while (it.hasNext()) {
        const QString dir = it.next();
        if (!it.fileInfo().isSymLink())
            QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner);
    }
    root.removeRecursively();
}

void ArchiveBrowser::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
    if (m_busy) {
        ++m_generation;
        m_busy = false;
        Q_EMIT busyChanged();
    }
}

QString ArchiveBrowser::archiveFor(const QString &location) const
{
    const QString root = browseRootFor(location);
    return root.isEmpty() ? QString() : m_rootToArchive.value(root);
}

QString ArchiveBrowser::browseRootFor(const QString &location) const
{
    for (auto it = m_rootToArchive.cbegin(); it != m_rootToArchive.cend(); ++it) {
        if (location == it.key() || location.startsWith(it.key() + QLatin1Char('/')))
            return it.key();
    }
    return {};
}

void ArchiveBrowser::open(const QString &archivePath)
{
    cancel();
    const QFileInfo info(archivePath);
    if (!archivePath.startsWith(QLatin1Char('/')) || !info.isFile()) {
        Q_EMIT failed(archivePath, tr("Only archives on this computer can be browsed."));
        return;
    }
    // One folder per archive version: same path and modification time
    // reuse the earlier unpack, a changed archive gets a fresh one.
    const QByteArray key = QCryptographicHash::hash(
        (archivePath + QLatin1Char('\n') + QString::number(info.lastModified().toMSecsSinceEpoch()))
            .toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    const QString slot = cacheRoot() + QLatin1Char('/') + QString::fromLatin1(key);
    for (auto it = m_rootToArchive.cbegin(); it != m_rootToArchive.cend(); ++it) {
        if (it.value() == archivePath && it.key().startsWith(slot + QLatin1Char('/'))
            && QFileInfo::exists(it.key())) {
            Q_EMIT opened(archivePath, it.key());
            return;
        }
    }

    m_busy = true;
    Q_EMIT busyChanged();
    const quint64 generation = ++m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto flag = m_cancel;
    QPointer<ArchiveBrowser> self(this);
    QThread *thread = QThread::create([self, flag, archivePath, slot, generation] {
        QDir().mkpath(slot);
        QFile::setPermissions(cacheRoot(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner);
        QFile::setPermissions(slot, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner);
        QString produced;
        QString error;
        bool needsPassphrase = false;
        bool needsConfirmation = false;
        const bool ok = ArchiveEngine::extract(archivePath, slot, &produced, &error,
            [flag] { return flag->load(); }, [](qint64, qint64) {}, QString(),
            &needsPassphrase, nullptr, ArchiveEngine::defaultExtractLimits(), &needsConfirmation);
        if (flag->load())
            return;
        QString message;
        if (!ok) {
            if (needsPassphrase)
                message = QCoreApplication::translate("ArchiveBrowser",
                    "This archive is password-protected — use Extract to open it.");
            else if (needsConfirmation)
                message = QCoreApplication::translate("ArchiveBrowser",
                    "This archive expands to far more than its size — use Extract if you trust it.");
            else
                message = error;
        }
        QMetaObject::invokeMethod(qApp, [self, archivePath, slot, produced, ok, message, generation] {
            if (!self || generation != self->m_generation)
                return;
            self->m_busy = false;
            Q_EMIT self->busyChanged();
            if (!ok) {
                Q_EMIT self->failed(archivePath, message);
                return;
            }
            // A single top-level file lands as itself: show the folder
            // around it rather than trying to "open" a file as a folder.
            const QString root = QFileInfo(produced).isDir() ? produced : slot;
            self->m_rootToArchive.insert(root, archivePath);
            Q_EMIT self->opened(archivePath, root);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
