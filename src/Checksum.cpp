#include "Checksum.h"
#include "Location.h"

#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QThreadPool>

#include <functional>

namespace {

QCryptographicHash::Algorithm algorithmFor(const QString &name)
{
    if (name == QLatin1String("md5"))
        return QCryptographicHash::Md5;
    if (name == QLatin1String("sha1"))
        return QCryptographicHash::Sha1;
    return QCryptographicHash::Sha256;
}

} // namespace

Checksum::Checksum(QObject *parent)
    : QObject(parent)
{
}

Checksum::~Checksum()
{
    if (m_cancel)
        m_cancel->store(true);
}

void Checksum::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    reset();
    Q_EMIT pathChanged();
}

void Checksum::setAlgorithm(const QString &algorithm)
{
    const QString clean = algorithm == QLatin1String("md5") || algorithm == QLatin1String("sha1")
        ? algorithm : QStringLiteral("sha256");
    if (m_algorithm == clean)
        return;
    m_algorithm = clean;
    reset();
    Q_EMIT algorithmChanged();
}

void Checksum::reset()
{
    ++m_generation;
    if (m_cancel)
        m_cancel->store(true);
    m_cancel.reset();
    m_running = false;
    m_progress = 0;
    m_result.clear();
    m_error.clear();
    Q_EMIT stateChanged();
}

void Checksum::cancel()
{
    reset();
}

void Checksum::start()
{
    reset();
    if (m_path.isEmpty() || !Location::isLocal(m_path)) {
        m_error = QStringLiteral("Checksums are for local files");
        Q_EMIT stateChanged();
        return;
    }
    m_running = true;
    Q_EMIT stateChanged();

    const quint64 generation = m_generation;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_cancel = cancelled;
    const QString path = Location::clean(m_path);
    const auto algorithm = algorithmFor(m_algorithm);
    QPointer<Checksum> self(this);

    QThreadPool::globalInstance()->start([=] {
        QElapsedTimer clock;
        clock.start();
        QString error;
        const QString hex = compute(path, algorithm, *cancelled, [&](qint64 done, qint64 total) {
            if (clock.elapsed() < 100 || total <= 0)
                return;
            clock.restart();
            const qreal fraction = qreal(done) / qreal(total);
            QMetaObject::invokeMethod(self.data(), [self, generation, fraction] {
                if (self && self->m_generation == generation) {
                    self->m_progress = fraction;
                    Q_EMIT self->stateChanged();
                }
            }, Qt::QueuedConnection);
        }, &error);
        if (cancelled->load())
            return;
        QMetaObject::invokeMethod(self.data(), [self, generation, hex, error] {
            if (!self || self->m_generation != generation)
                return;
            self->m_running = false;
            self->m_progress = hex.isEmpty() ? 0 : 1;
            self->m_result = hex;
            self->m_error = error;
            self->m_cancel.reset();
            Q_EMIT self->stateChanged();
        }, Qt::QueuedConnection);
    });
}

QString Checksum::compute(const QString &path, QCryptographicHash::Algorithm algorithm,
                          const std::atomic_bool &cancelled,
                          const std::function<void(qint64, qint64)> &progress, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    const qint64 total = file.size();
    QCryptographicHash hash(algorithm);
    QByteArray buffer(1 << 20, Qt::Uninitialized);
    qint64 done = 0;
    while (true) {
        if (cancelled.load())
            return {};
        const qint64 got = file.read(buffer.data(), buffer.size());
        if (got < 0) {
            if (error)
                *error = file.errorString();
            return {};
        }
        if (got == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), got));
        done += got;
        if (progress)
            progress(done, total);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool Checksum::matches(const QString &text) const
{
    if (m_result.isEmpty())
        return false;
    return text.trimmed().contains(m_result, Qt::CaseInsensitive);
}
