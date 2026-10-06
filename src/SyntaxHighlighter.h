#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QtQmlIntegration>

class CodeHighlighter;

// Colours source code in the quick view's text preview. Attach it to a
// TextEdit's textDocument, give it the file name, and it picks a language by
// extension (or a #! line) and paints keywords, strings, comments, numbers
// and types in the theme's colours. Plain text stays plain.
//
// Deliberately small: a handful of rules per language family, not a parser.
// It reads only the text already loaded for the preview.
class SyntaxHighlighter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // The language picked ("" when none), for the preview's caption and tests.
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QColor keywordColor MEMBER m_keyword WRITE setKeywordColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor stringColor MEMBER m_string WRITE setStringColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor commentColor MEMBER m_comment WRITE setCommentColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor numberColor MEMBER m_number WRITE setNumberColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor typeColor MEMBER m_type WRITE setTypeColor NOTIFY colorsChanged)

public:
    explicit SyntaxHighlighter(QObject *parent = nullptr);
    ~SyntaxHighlighter() override;

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    QString fileName() const { return m_fileName; }
    void setFileName(const QString &name);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QString language() const { return m_language; }
    // Picks the language again — after the text changes, for a #! line.
    Q_INVOKABLE void refresh() { scheduleRebuild(); }

    void setKeywordColor(const QColor &c) { m_keyword = c; scheduleRebuild(); }
    void setStringColor(const QColor &c) { m_string = c; scheduleRebuild(); }
    void setCommentColor(const QColor &c) { m_comment = c; scheduleRebuild(); }
    void setNumberColor(const QColor &c) { m_number = c; scheduleRebuild(); }
    void setTypeColor(const QColor &c) { m_type = c; scheduleRebuild(); }

    // The language for a file name and its first line — "cpp", "python",
    // "shell", … or "" for none. Public for the tests.
    static QString languageFor(const QString &fileName, const QString &firstLine = QString());

    struct Rule {
        QRegularExpression pattern;
        QString role; // keyword | string | comment | number | type
    };
    struct Language {
        QList<Rule> rules;
        QString blockStart; // multi-line comment delimiters, if any
        QString blockEnd;
    };
    static Language rulesFor(const QString &language);

Q_SIGNALS:
    void documentChanged();
    void fileNameChanged();
    void enabledChanged();
    void languageChanged();
    void colorsChanged();

private:
    void rebuild();
    // Coalesces the setters' rebuilds into one, on the next event loop
    // pass. Never rebuild synchronously from a setter: deleting the old
    // highlighter clears the document's formats, the TextEdit announces a
    // text change, and QML can call straight back in mid-delete.
    void scheduleRebuild();

    bool m_rebuildQueued = false;
    bool m_rebuilding = false;

    QPointer<QQuickTextDocument> m_document;
    // Parented to the document, which deletes it when it goes away first —
    // a QPointer, so that never leaves a dangling pointer behind.
    QPointer<CodeHighlighter> m_highlighter;
    QString m_fileName;
    QString m_language;
    bool m_enabled = true;
    QColor m_keyword = QColor("#c678dd");
    QColor m_string = QColor("#98c379");
    QColor m_comment = QColor("#7f848e");
    QColor m_number = QColor("#d19a66");
    QColor m_type = QColor("#61afef");
};
