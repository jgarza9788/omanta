#include "SyntaxHighlighter.h"

#include <QFileInfo>
#include <QHash>
#include <QTextDocument>

#include <utility>

// The QSyntaxHighlighter proper. Kept apart from the QML-facing object so a
// TextEdit that swaps its document (or none) never leaves a dangling one.
class CodeHighlighter : public QSyntaxHighlighter
{
public:
    CodeHighlighter(QTextDocument *document, SyntaxHighlighter::Language language,
                    QHash<QString, QTextCharFormat> formats)
        : QSyntaxHighlighter(document)
        , m_language(std::move(language))
        , m_formats(std::move(formats))
    {
        if (!m_language.blockStart.isEmpty()) {
            m_blockStart = QRegularExpression(QRegularExpression::escape(m_language.blockStart));
            m_blockEnd = QRegularExpression(QRegularExpression::escape(m_language.blockEnd));
        }
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const SyntaxHighlighter::Rule &rule : std::as_const(m_language.rules)) {
            auto matches = rule.pattern.globalMatch(text);
            while (matches.hasNext()) {
                const QRegularExpressionMatch match = matches.next();
                // A capture group, when the rule has one, is the part to paint.
                const int group = match.lastCapturedIndex() >= 1 ? 1 : 0;
                setFormat(int(match.capturedStart(group)), int(match.capturedLength(group)),
                          m_formats.value(rule.role));
            }
        }
        if (m_language.blockStart.isEmpty())
            return;
        // Block comments that span lines, the textbook way.
        setCurrentBlockState(0);
        qsizetype start = 0;
        if (previousBlockState() != 1) {
            const QRegularExpressionMatch open = m_blockStart.match(text);
            start = open.hasMatch() ? open.capturedStart() : -1;
        }
        while (start >= 0) {
            const QRegularExpressionMatch close =
                m_blockEnd.match(text, start + (previousBlockState() == 1 && start == 0 ? 0
                                                : m_language.blockStart.size()));
            qsizetype length;
            if (!close.hasMatch()) {
                setCurrentBlockState(1);
                length = text.size() - start;
            } else {
                length = close.capturedEnd() - start;
            }
            setFormat(int(start), int(length), m_formats.value(QStringLiteral("comment")));
            const QRegularExpressionMatch next = m_blockStart.match(text, start + length);
            start = next.hasMatch() ? next.capturedStart() : -1;
        }
    }

private:
    SyntaxHighlighter::Language m_language;
    QHash<QString, QTextCharFormat> m_formats;
    QRegularExpression m_blockStart;
    QRegularExpression m_blockEnd;
};

namespace {

QRegularExpression words(const QStringList &list)
{
    return QRegularExpression(QStringLiteral("\\b(?:%1)\\b").arg(list.join(QLatin1Char('|'))));
}

const QString kDoubleString = QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"");
const QString kSingleString = QStringLiteral("'(?:[^'\\\\]|\\\\.)*'");
const QString kNumber = QStringLiteral("\\b(?:0[xX][0-9a-fA-F_]+|\\d[\\d_]*(?:\\.\\d+)?(?:[eE][+-]?\\d+)?)\\b");

} // namespace

SyntaxHighlighter::SyntaxHighlighter(QObject *parent)
    : QObject(parent)
{
}

SyntaxHighlighter::~SyntaxHighlighter()
{
    delete m_highlighter;
}

void SyntaxHighlighter::setDocument(QQuickTextDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;
    Q_EMIT documentChanged();
    scheduleRebuild();
}

void SyntaxHighlighter::setFileName(const QString &name)
{
    if (m_fileName == name)
        return;
    m_fileName = name;
    Q_EMIT fileNameChanged();
    scheduleRebuild();
}

void SyntaxHighlighter::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    Q_EMIT enabledChanged();
    scheduleRebuild();
}

QString SyntaxHighlighter::languageFor(const QString &fileName, const QString &firstLine)
{
    static const QHash<QString, QString> bySuffix = {
        { "c", "cpp" }, { "h", "cpp" }, { "cc", "cpp" }, { "cpp", "cpp" }, { "cxx", "cpp" },
        { "hpp", "cpp" }, { "hh", "cpp" }, { "hxx", "cpp" }, { "ino", "cpp" },
        { "java", "java" }, { "kt", "java" }, { "kts", "java" }, { "scala", "java" },
        { "cs", "java" }, { "swift", "java" }, { "dart", "java" },
        { "js", "js" }, { "mjs", "js" }, { "cjs", "js" }, { "jsx", "js" }, { "ts", "js" },
        { "tsx", "js" }, { "qml", "js" }, { "json", "json" }, { "jsonc", "js" },
        { "py", "python" }, { "pyw", "python" }, { "pyi", "python" },
        { "rs", "rust" }, { "go", "go" },
        { "sh", "shell" }, { "bash", "shell" }, { "zsh", "shell" }, { "fish", "shell" },
        { "ksh", "shell" },
        { "rb", "ruby" }, { "lua", "lua" }, { "php", "php" }, { "pl", "perl" }, { "pm", "perl" },
        { "toml", "ini" }, { "ini", "ini" }, { "conf", "ini" }, { "cfg", "ini" },
        { "desktop", "ini" }, { "service", "ini" },
        { "yml", "yaml" }, { "yaml", "yaml" },
        { "css", "css" }, { "scss", "css" }, { "less", "css" },
        { "html", "markup" }, { "htm", "markup" }, { "xml", "markup" }, { "svg", "markup" },
        { "ui", "markup" }, { "qrc", "markup" },
        { "sql", "sql" }, { "cmake", "cmake" },
    };
    const QFileInfo info(fileName);
    const QString name = info.fileName();
    if (name == QLatin1String("CMakeLists.txt"))
        return QStringLiteral("cmake");
    if (name == QLatin1String("Makefile") || name == QLatin1String("PKGBUILD")
        || name.startsWith(QLatin1String(".bash")) || name == QLatin1String(".zshrc")
        || name == QLatin1String(".profile"))
        return QStringLiteral("shell");
    const QString language = bySuffix.value(info.suffix().toLower());
    if (!language.isEmpty())
        return language;
    if (firstLine.startsWith(QLatin1String("#!"))) {
        if (firstLine.contains(QLatin1String("python")))
            return QStringLiteral("python");
        if (firstLine.contains(QLatin1String("node")))
            return QStringLiteral("js");
        if (firstLine.contains(QLatin1String("ruby")))
            return QStringLiteral("ruby");
        if (firstLine.contains(QLatin1String("lua")))
            return QStringLiteral("lua");
        if (firstLine.contains(QLatin1String("perl")))
            return QStringLiteral("perl");
        if (firstLine.contains(QLatin1String("sh")))
            return QStringLiteral("shell");
    }
    return {};
}

SyntaxHighlighter::Language SyntaxHighlighter::rulesFor(const QString &language)
{
    Language out;
    const auto add = [&out](const QString &pattern, const char *role) {
        out.rules.append({ QRegularExpression(pattern), QString::fromLatin1(role) });
    };
    const auto addWords = [&out](const QStringList &list, const char *role) {
        out.rules.append({ words(list), QString::fromLatin1(role) });
    };
    const QString hashComment = QStringLiteral("#.*$");
    const QString slashComment = QStringLiteral("//.*$");

    if (language == QLatin1String("cpp") || language == QLatin1String("java")
        || language == QLatin1String("js") || language == QLatin1String("rust")
        || language == QLatin1String("go") || language == QLatin1String("php")) {
        QStringList keywords;
        if (language == QLatin1String("cpp"))
            keywords = { "auto", "break", "case", "catch", "class", "const", "constexpr", "continue",
                         "default", "delete", "do", "else", "enum", "explicit", "extern", "false",
                         "for", "friend", "goto", "if", "inline", "namespace", "new", "noexcept",
                         "nullptr", "operator", "override", "private", "protected", "public",
                         "return", "sizeof", "static", "struct", "switch", "template", "this",
                         "throw", "true", "try", "typedef", "typename", "union", "using",
                         "virtual", "volatile", "while", "co_await", "co_return" };
        else if (language == QLatin1String("java"))
            keywords = { "abstract", "break", "case", "catch", "class", "const", "continue",
                         "default", "do", "else", "enum", "extends", "false", "final", "finally",
                         "for", "fun", "if", "implements", "import", "interface", "is", "let",
                         "new", "null", "object", "override", "package", "private", "protected",
                         "public", "return", "static", "super", "switch", "this", "throw",
                         "true", "try", "val", "var", "void", "when", "while", "func", "guard",
                         "struct", "using", "namespace" };
        else if (language == QLatin1String("js"))
            keywords = { "async", "await", "break", "case", "catch", "class", "const", "continue",
                         "default", "delete", "do", "else", "export", "extends", "false",
                         "finally", "for", "from", "function", "if", "import", "in", "instanceof",
                         "interface", "let", "new", "null", "of", "property", "readonly",
                         "required", "return", "signal", "super", "switch", "this", "throw",
                         "true", "try", "type", "typeof", "undefined", "var", "void", "while",
                         "yield", "component", "alias" };
        else if (language == QLatin1String("rust"))
            keywords = { "as", "async", "await", "break", "const", "continue", "crate", "dyn",
                         "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in",
                         "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
                         "self", "Self", "static", "struct", "super", "trait", "true", "type",
                         "unsafe", "use", "where", "while" };
        else if (language == QLatin1String("go"))
            keywords = { "break", "case", "chan", "const", "continue", "default", "defer", "else",
                         "fallthrough", "false", "for", "func", "go", "goto", "if", "import",
                         "interface", "map", "nil", "package", "range", "return", "select",
                         "struct", "switch", "true", "type", "var" };
        else
            keywords = { "abstract", "array", "as", "break", "case", "catch", "class", "const",
                         "continue", "default", "do", "echo", "else", "elseif", "extends",
                         "false", "final", "finally", "fn", "for", "foreach", "function", "if",
                         "implements", "include", "interface", "match", "namespace", "new",
                         "null", "private", "protected", "public", "require", "return",
                         "static", "switch", "throw", "true", "try", "use", "while" };
        addWords(keywords, "keyword");
        add(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*\\b"), "type");
        add(kNumber, "number");
        if (language == QLatin1String("cpp"))
            add(QStringLiteral("^\\s*#\\s*\\w+"), "keyword");
        add(kDoubleString, "string");
        add(kSingleString, "string");
        if (language == QLatin1String("js") || language == QLatin1String("go"))
            add(QStringLiteral("`[^`]*`"), "string");
        add(slashComment, "comment");
        if (language == QLatin1String("php"))
            add(hashComment, "comment");
        out.blockStart = QStringLiteral("/*");
        out.blockEnd = QStringLiteral("*/");
    } else if (language == QLatin1String("python") || language == QLatin1String("ruby")
               || language == QLatin1String("perl")) {
        QStringList keywords;
        if (language == QLatin1String("python"))
            keywords = { "and", "as", "assert", "async", "await", "break", "class", "continue",
                         "def", "del", "elif", "else", "except", "False", "finally", "for",
                         "from", "global", "if", "import", "in", "is", "lambda", "None",
                         "nonlocal", "not", "or", "pass", "raise", "return", "self", "True",
                         "try", "while", "with", "yield", "match", "case" };
        else if (language == QLatin1String("ruby"))
            keywords = { "alias", "and", "begin", "break", "case", "class", "def", "defined",
                         "do", "else", "elsif", "end", "ensure", "false", "for", "if", "in",
                         "module", "next", "nil", "not", "or", "redo", "require", "rescue",
                         "retry", "return", "self", "super", "then", "true", "undef", "unless",
                         "until", "when", "while", "yield" };
        else
            keywords = { "my", "our", "local", "sub", "if", "elsif", "else", "unless", "while",
                         "until", "for", "foreach", "return", "use", "package", "require",
                         "last", "next", "die", "print" };
        addWords(keywords, "keyword");
        add(QStringLiteral("^\\s*@[\\w.]+"), "type");
        add(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*\\b"), "type");
        add(kNumber, "number");
        add(kDoubleString, "string");
        add(kSingleString, "string");
        add(hashComment, "comment");
    } else if (language == QLatin1String("shell")) {
        addWords({ "if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done",
                   "case", "esac", "in", "function", "return", "local", "export", "readonly",
                   "declare", "set", "unset", "shift", "exit", "source", "echo", "printf",
                   "cd", "true", "false" }, "keyword");
        add(QStringLiteral("\\$\\{?[A-Za-z_][A-Za-z0-9_]*\\}?|\\$[0-9@#?*!$-]"), "type");
        add(kNumber, "number");
        add(kDoubleString, "string");
        add(QStringLiteral("'[^']*'"), "string");
        add(QStringLiteral("(?:^|\\s)(#.*)$"), "comment");
    } else if (language == QLatin1String("lua")) {
        addWords({ "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
                   "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
                   "true", "until", "while" }, "keyword");
        add(kNumber, "number");
        add(kDoubleString, "string");
        add(kSingleString, "string");
        add(QStringLiteral("--.*$"), "comment");
    } else if (language == QLatin1String("json")) {
        add(QStringLiteral("(\"(?:[^\"\\\\]|\\\\.)*\")\\s*:"), "type");
        add(QStringLiteral(":\\s*(\"(?:[^\"\\\\]|\\\\.)*\")"), "string");
        add(QStringLiteral("[\\[,]\\s*(\"(?:[^\"\\\\]|\\\\.)*\")"), "string");
        addWords({ "true", "false", "null" }, "keyword");
        add(kNumber, "number");
    } else if (language == QLatin1String("ini") || language == QLatin1String("yaml")) {
        if (language == QLatin1String("ini"))
            add(QStringLiteral("^\\s*\\[[^\\]]*\\]"), "keyword");
        add(QStringLiteral("^\\s*([A-Za-z0-9_.\\-]+)\\s*[=:]"), "type");
        addWords({ "true", "false", "yes", "no", "on", "off", "null" }, "keyword");
        add(kNumber, "number");
        add(kDoubleString, "string");
        add(kSingleString, "string");
        add(QStringLiteral("(?:^|\\s)([#;].*)$"), "comment");
    } else if (language == QLatin1String("css")) {
        add(QStringLiteral("^\\s*([^{}:;]+)\\s*\\{"), "keyword");
        add(QStringLiteral("([a-z-]+)\\s*:"), "type");
        add(QStringLiteral("#[0-9a-fA-F]{3,8}\\b"), "number");
        add(kNumber, "number");
        add(kDoubleString, "string");
        add(kSingleString, "string");
        out.blockStart = QStringLiteral("/*");
        out.blockEnd = QStringLiteral("*/");
    } else if (language == QLatin1String("markup")) {
        add(QStringLiteral("</?([A-Za-z_][\\w:.-]*)"), "keyword");
        add(QStringLiteral("\\s([A-Za-z_][\\w:.-]*)="), "type");
        add(kDoubleString, "string");
        add(kSingleString, "string");
        out.blockStart = QStringLiteral("<!--");
        out.blockEnd = QStringLiteral("-->");
    } else if (language == QLatin1String("sql")) {
        out.rules.append({ QRegularExpression(
            QStringLiteral("\\b(?:select|from|where|insert|into|values|update|set|delete|create|"
                           "table|drop|alter|index|join|left|right|inner|outer|on|group|by|"
                           "order|having|limit|and|or|not|null|as|distinct|union|primary|key|"
                           "references|default)\\b"),
            QRegularExpression::CaseInsensitiveOption), QStringLiteral("keyword") });
        add(kNumber, "number");
        add(kSingleString, "string");
        add(QStringLiteral("--.*$"), "comment");
        out.blockStart = QStringLiteral("/*");
        out.blockEnd = QStringLiteral("*/");
    } else if (language == QLatin1String("cmake")) {
        add(QStringLiteral("^\\s*([A-Za-z_]+)\\s*\\("), "keyword");
        add(QStringLiteral("\\$\\{[^}]*\\}"), "type");
        add(kDoubleString, "string");
        add(hashComment, "comment");
    }
    return out;
}

void SyntaxHighlighter::scheduleRebuild()
{
    if (m_rebuildQueued)
        return;
    m_rebuildQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_rebuildQueued = false;
        rebuild();
    }, Qt::QueuedConnection);
}

void SyntaxHighlighter::rebuild()
{
    // Anything that calls back in while the old highlighter is going away
    // waits for the next pass instead of deleting it a second time.
    if (m_rebuilding) {
        scheduleRebuild();
        return;
    }
    m_rebuilding = true;
    CodeHighlighter *old = m_highlighter.data();
    m_highlighter = nullptr;
    delete old;
    m_rebuilding = false;

    QTextDocument *document = m_document ? m_document->textDocument() : nullptr;
    QString firstLine;
    if (document)
        firstLine = document->firstBlock().text();
    const QString language = m_enabled ? languageFor(m_fileName, firstLine) : QString();
    if (language != m_language) {
        m_language = language;
        Q_EMIT languageChanged();
    }
    if (!document || language.isEmpty())
        return;

    QHash<QString, QTextCharFormat> formats;
    const auto format = [](const QColor &color, bool bold = false, bool italic = false) {
        QTextCharFormat f;
        f.setForeground(color);
        if (bold)
            f.setFontWeight(QFont::DemiBold);
        f.setFontItalic(italic);
        return f;
    };
    formats.insert(QStringLiteral("keyword"), format(m_keyword, true));
    formats.insert(QStringLiteral("string"), format(m_string));
    formats.insert(QStringLiteral("comment"), format(m_comment, false, true));
    formats.insert(QStringLiteral("number"), format(m_number));
    formats.insert(QStringLiteral("type"), format(m_type));
    m_highlighter = new CodeHighlighter(document, rulesFor(language), formats);
}
