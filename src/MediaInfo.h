#pragma once

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QtQmlIntegration>

class QJSEngine;
class QQmlEngine;

// Media details for the extra list columns: picture size, playing time,
// artist, album and the date a photo was taken. Read in the background the
// first time a row asks, then cached — a column of a thousand photos fills in
// as you look at it, never blocking the view.
//
// Pictures in the formats omanta already decodes in-process (PNG, JPEG, GIF,
// BMP) are measured from their headers, and a JPEG's EXIF block is parsed for
// its date. Everything else goes to ffprobe, inside the same bubblewrap
// sandbox as the thumbnailers when one can be built.
class MediaInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    struct Details {
        int width = 0;
        int height = 0;
        double seconds = -1;
        QString artist;
        QString album;
        QDateTime taken;
    };

    // One shared instance: the proxy model sorts by these values too.
    static MediaInfo *instance();
    static MediaInfo *create(QQmlEngine *, QJSEngine *);

    int revision() const { return m_revision; }

    // The column ids this provides.
    static QStringList fields();
    // Display text for `field` ("dimensions", "duration", "artist", "album",
    // "photoDate"), or "" while unknown — asking queues the read.
    Q_INVOKABLE QString text(const QString &path, const QString &field);
    // A sortable value for the same field (invalid while unknown).
    Q_INVOKABLE QVariant sortValue(const QString &path, const QString &field);
    // Whether this kind of file can have any of the fields at all.
    static bool interesting(const QString &path);

    // The readers, synchronous, public for the tests.
    static Details read(const QString &path);
    static QDateTime exifDate(const QByteArray &jpeg);
    static Details parseFfprobe(const QByteArray &json);
    static QString formatDuration(double seconds);

Q_SIGNALS:
    void changed();

private:
    explicit MediaInfo(QObject *parent = nullptr);
    const Details *lookup(const QString &path);
    void deliver(const QString &path, const Details &details);

    QHash<QString, Details> m_cache;
    QSet<QString> m_pending;
    QThreadPool m_pool;
    QTimer m_notify;
    int m_revision = 0;
};
