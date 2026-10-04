#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration>

// What the built-in quick view (Space) shows for one location, worked out
// off the UI thread: which kind of preview fits, and the data it needs.
//
//   image / animated  dimensions; QML decodes (Qt's 256 MB allocation cap)
//   text              the first 256 KB, with line numbers
//   markdown          rendered to HTML with images and raw HTML removed —
//                     a README must not make the file manager fetch a URL
//   folder            the first entries; FolderSizer adds the total
//   archive           a header-only listing (ArchiveEngine::list)
//   media / pdf       handled by QML when built with Qt Multimedia / Pdf
//   info              everything else: name, type, size, dates
//
// Remote locations get "info" only: previews read content, and content over
// a network share is a stall waiting to happen.
class QuickViewInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    // The path as a URL for Image/MediaPlayer/PdfDocument — properly
    // escaped, which hand-built "file://" + path is not (# and ? in names).
    Q_PROPERTY(QUrl url READ url NOTIFY pathChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loaded)
    Q_PROPERTY(QString kind READ kind NOTIFY loaded)
    Q_PROPERTY(QString name READ name NOTIFY loaded)
    Q_PROPERTY(QString contentType READ contentType NOTIFY loaded)
    Q_PROPERTY(QString typeDescription READ typeDescription NOTIFY loaded)
    Q_PROPERTY(qint64 size READ size NOTIFY loaded)
    Q_PROPERTY(QDateTime modified READ modified NOTIFY loaded)
    Q_PROPERTY(QString text READ text NOTIFY loaded)
    Q_PROPERTY(QString lineNumbers READ lineNumbers NOTIFY loaded)
    Q_PROPERTY(QString html READ html NOTIFY loaded)
    Q_PROPERTY(bool truncated READ truncated NOTIFY loaded)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY loaded)
    Q_PROPERTY(int entryCount READ entryCount NOTIFY loaded)
    Q_PROPERTY(qint64 entriesBytes READ entriesBytes NOTIFY loaded)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY loaded)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY loaded)
    Q_PROPERTY(QString error READ error NOTIFY loaded)
    // Which optional previewers this build carries.
    Q_PROPERTY(bool hasMedia READ hasMedia CONSTANT)
    Q_PROPERTY(bool hasPdf READ hasPdf CONSTANT)

public:
    struct Result {
        QString kind = QStringLiteral("info");
        QString name;
        QString contentType;
        QString typeDescription;
        qint64 size = -1;
        QDateTime modified;
        QString text;
        QString lineNumbers;
        QString html;
        bool truncated = false;
        QVariantList entries;
        int entryCount = 0;
        qint64 entriesBytes = 0;
        int imageWidth = 0;
        int imageHeight = 0;
        QString error;
    };

    explicit QuickViewInfo(QObject *parent = nullptr);

    QString path() const { return m_path; }
    QUrl url() const;
    void setPath(const QString &path);
    bool loading() const { return m_loading; }
    QString kind() const { return m_result.kind; }
    QString name() const { return m_result.name; }
    QString contentType() const { return m_result.contentType; }
    QString typeDescription() const { return m_result.typeDescription; }
    qint64 size() const { return m_result.size; }
    QDateTime modified() const { return m_result.modified; }
    QString text() const { return m_result.text; }
    QString lineNumbers() const { return m_result.lineNumbers; }
    QString html() const { return m_result.html; }
    bool truncated() const { return m_result.truncated; }
    QVariantList entries() const { return m_result.entries; }
    int entryCount() const { return m_result.entryCount; }
    qint64 entriesBytes() const { return m_result.entriesBytes; }
    int imageWidth() const { return m_result.imageWidth; }
    int imageHeight() const { return m_result.imageHeight; }
    QString error() const { return m_result.error; }
    static bool hasMedia();
    static bool hasPdf();

    // The whole inspection, synchronous — the worker's body, public so the
    // tests can assert on kinds without an event loop.
    static Result inspect(const QString &location);
    // Markdown → HTML that cannot load anything: images become their alt
    // text, raw HTML is dropped. Public for the tests.
    static QString safeMarkdownHtml(const QString &markdown);

    static constexpr qint64 kTextBytes = 256 * 1024;
    static constexpr int kTextLines = 5000;
    static constexpr int kListEntries = 200;
    static constexpr int kArchiveEntries = 5000;

Q_SIGNALS:
    void pathChanged();
    void loaded();

private:
    QString m_path;
    Result m_result;
    bool m_loading = false;
    quint64 m_generation = 0;
};
