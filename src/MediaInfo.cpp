#include "MediaInfo.h"
#include "ThumbnailProvider.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QQmlEngine>
#include <QStandardPaths>

namespace {

const QStringList kHeaderImages = { "png", "jpg", "jpeg", "jpe", "gif", "bmp" };
const QStringList kProbed = { "webp", "tif", "tiff", "heic", "heif", "avif", "jxl",
                              "mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "wav",
                              "wma", "mp4", "m4v", "mkv", "webm", "mov", "avi", "wmv",
                              "mpg", "mpeg", "ogv", "3gp" };

QString suffixOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

quint32 readUInt(const QByteArray &data, int at, int bytes, bool little)
{
    if (at < 0 || at + bytes > data.size())
        return 0;
    quint32 value = 0;
    for (int i = 0; i < bytes; ++i) {
        const quint32 byte = quint8(data.at(at + i));
        value |= little ? byte << (8 * i) : byte << (8 * (bytes - 1 - i));
    }
    return value;
}

} // namespace

MediaInfo::MediaInfo(QObject *parent)
    : QObject(parent)
{
    // Two at a time: ffprobe is a process each, and a folder of videos must
    // not fork forty of them.
    m_pool.setMaxThreadCount(2);
    m_notify.setSingleShot(true);
    m_notify.setInterval(120);
    connect(&m_notify, &QTimer::timeout, this, [this] {
        ++m_revision;
        Q_EMIT changed();
    });
}

MediaInfo *MediaInfo::instance()
{
    static MediaInfo *shared = new MediaInfo;
    return shared;
}

MediaInfo *MediaInfo::create(QQmlEngine *, QJSEngine *)
{
    MediaInfo *shared = instance();
    QQmlEngine::setObjectOwnership(shared, QQmlEngine::CppOwnership);
    return shared;
}

QStringList MediaInfo::fields()
{
    return { QStringLiteral("dimensions"), QStringLiteral("duration"), QStringLiteral("artist"),
             QStringLiteral("album"), QStringLiteral("photoDate") };
}

bool MediaInfo::interesting(const QString &path)
{
    if (!path.startsWith(QLatin1Char('/')))
        return false;
    const QString suffix = suffixOf(path);
    return kHeaderImages.contains(suffix) || kProbed.contains(suffix);
}

const MediaInfo::Details *MediaInfo::lookup(const QString &path)
{
    auto it = m_cache.constFind(path);
    if (it != m_cache.cend())
        return &*it;
    if (!interesting(path) || m_pending.contains(path))
        return nullptr;
    m_pending.insert(path);
    QPointer<MediaInfo> self(this);
    m_pool.start([self, path] {
        const Details details = read(path);
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, path, details] {
            if (self)
                self->deliver(path, details);
        }, Qt::QueuedConnection);
    });
    return nullptr;
}

void MediaInfo::deliver(const QString &path, const Details &details)
{
    m_pending.remove(path);
    if (m_cache.size() > 20000)
        m_cache.clear();
    m_cache.insert(path, details);
    m_notify.start();
}

QString MediaInfo::formatDuration(double seconds)
{
    if (seconds < 0)
        return {};
    const qint64 total = qint64(seconds + 0.5);
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
                                         .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString MediaInfo::text(const QString &path, const QString &field)
{
    const Details *details = lookup(path);
    if (!details)
        return {};
    if (field == QLatin1String("dimensions"))
        return details->width > 0 ? QStringLiteral("%1 × %2").arg(details->width).arg(details->height)
                                  : QString();
    if (field == QLatin1String("duration"))
        return formatDuration(details->seconds);
    if (field == QLatin1String("artist"))
        return details->artist;
    if (field == QLatin1String("album"))
        return details->album;
    if (field == QLatin1String("photoDate"))
        return details->taken.isValid()
            ? QLocale().toString(details->taken, QLocale::ShortFormat) : QString();
    return {};
}

QVariant MediaInfo::sortValue(const QString &path, const QString &field)
{
    const Details *details = lookup(path);
    if (!details)
        return {};
    if (field == QLatin1String("dimensions"))
        return details->width > 0 ? QVariant(qint64(details->width) * details->height) : QVariant();
    if (field == QLatin1String("duration"))
        return details->seconds >= 0 ? QVariant(details->seconds) : QVariant();
    if (field == QLatin1String("artist"))
        return details->artist.isEmpty() ? QVariant() : QVariant(details->artist);
    if (field == QLatin1String("album"))
        return details->album.isEmpty() ? QVariant() : QVariant(details->album);
    if (field == QLatin1String("photoDate"))
        return details->taken.isValid() ? QVariant(details->taken) : QVariant();
    return {};
}

QDateTime MediaInfo::exifDate(const QByteArray &jpeg)
{
    // Walk the JPEG markers to APP1 "Exif\0\0", then the TIFF structure:
    // IFD0 → the Exif sub-IFD (tag 0x8769) → DateTimeOriginal (0x9003),
    // falling back to IFD0's DateTime (0x0132).
    if (jpeg.size() < 4 || quint8(jpeg.at(0)) != 0xFF || quint8(jpeg.at(1)) != 0xD8)
        return {};
    int at = 2;
    while (at + 4 <= jpeg.size()) {
        if (quint8(jpeg.at(at)) != 0xFF)
            return {};
        const int marker = quint8(jpeg.at(at + 1));
        const int length = int(readUInt(jpeg, at + 2, 2, false));
        if (marker == 0xDA || length < 2)
            return {}; // image data starts: no EXIF before it
        if (marker == 0xE1 && jpeg.mid(at + 4, 6) == QByteArray("Exif\0\0", 6)) {
            const QByteArray tiff = jpeg.mid(at + 10, length - 8);
            const bool little = tiff.startsWith("II");
            if (!little && !tiff.startsWith("MM"))
                return {};
            const auto readIfd = [&](int offset, quint16 wanted, quint32 *value) {
                const int count = int(readUInt(tiff, offset, 2, little));
                if (count <= 0 || count > 500)
                    return false;
                for (int i = 0; i < count; ++i) {
                    const int entry = offset + 2 + i * 12;
                    if (readUInt(tiff, entry, 2, little) == wanted) {
                        *value = readUInt(tiff, entry + 8, 4, little);
                        return true;
                    }
                }
                return false;
            };
            const auto dateAt = [&](quint32 offset) -> QDateTime {
                const QByteArray text = tiff.mid(int(offset), 19);
                QDateTime when = QDateTime::fromString(QString::fromLatin1(text),
                                                       QStringLiteral("yyyy:MM:dd HH:mm:ss"));
                return when;
            };
            const int ifd0 = int(readUInt(tiff, 4, 4, little));
            quint32 value = 0;
            if (readIfd(ifd0, 0x8769, &value)) {
                quint32 dateOffset = 0;
                if (readIfd(int(value), 0x9003, &dateOffset)) {
                    const QDateTime when = dateAt(dateOffset);
                    if (when.isValid())
                        return when;
                }
            }
            if (readIfd(ifd0, 0x0132, &value))
                return dateAt(value);
            return {};
        }
        at += 2 + length;
    }
    return {};
}

MediaInfo::Details MediaInfo::parseFfprobe(const QByteArray &json)
{
    Details details;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    bool ok = false;
    const double duration = format.value(QStringLiteral("duration")).toString().toDouble(&ok);
    // A still picture reports a token duration; only real media has one.
    const QString formatName = format.value(QStringLiteral("format_name")).toString();
    const bool still = formatName.contains(QLatin1String("image2"))
                    || formatName.contains(QLatin1String("_pipe"))
                    || formatName == QLatin1String("webp");
    if (ok && duration > 0 && !still)
        details.seconds = duration;

    const auto tagOf = [](const QJsonObject &tags, const char *key) {
        for (auto it = tags.begin(); it != tags.end(); ++it) {
            if (it.key().compare(QLatin1String(key), Qt::CaseInsensitive) == 0)
                return it.value().toString();
        }
        return QString();
    };
    QJsonObject tags = format.value(QStringLiteral("tags")).toObject();
    for (const QJsonValue &value : root.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject stream = value.toObject();
        if (stream.value(QStringLiteral("codec_type")).toString() == QLatin1String("video")
            && details.width == 0) {
            details.width = stream.value(QStringLiteral("width")).toInt();
            details.height = stream.value(QStringLiteral("height")).toInt();
        }
        // Ogg/Opus keep their tags on the stream, not the container.
        if (tags.isEmpty())
            tags = stream.value(QStringLiteral("tags")).toObject();
    }
    details.artist = tagOf(tags, "artist");
    if (details.artist.isEmpty())
        details.artist = tagOf(tags, "album_artist");
    details.album = tagOf(tags, "album");
    const QString created = tagOf(tags, "creation_time");
    if (!created.isEmpty())
        details.taken = QDateTime::fromString(created, Qt::ISODateWithMs);
    return details;
}

MediaInfo::Details MediaInfo::read(const QString &path)
{
    Details details;
    const QString suffix = suffixOf(path);
    if (kHeaderImages.contains(suffix)) {
        // Header only: size() never decodes the picture.
        QImageReader reader(path);
        const QSize size = reader.size();
        if (size.isValid()) {
            details.width = size.width();
            details.height = size.height();
        }
        if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
            || suffix == QLatin1String("jpe")) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly))
                details.taken = exifDate(file.read(256 * 1024));
        }
        return details;
    }
    if (!kProbed.contains(suffix))
        return details;

    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty())
        return details;
    QStringList command = { ffprobe, QStringLiteral("-v"), QStringLiteral("quiet"),
                            QStringLiteral("-print_format"), QStringLiteral("json"),
                            QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
                            path };
    if (ThumbnailCache::sandboxAvailable()) {
        const QStringList sandboxed = ThumbnailCache::sandboxedCommand(command, path, QString());
        if (!sandboxed.isEmpty())
            command = sandboxed;
    }
    QProcess process;
    process.start(command.first(), command.mid(1));
    if (!process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(500);
        return details;
    }
    return parseFfprobe(process.readAllStandardOutput());
}
