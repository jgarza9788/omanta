#include "Tags.h"
#include "ProPaths.h"

#include <QFile>
#include <QFileInfo>

#include <sys/xattr.h>
#include <cerrno>

namespace {

constexpr const char *kAttribute = "user.xdg.tags";

struct PaletteEntry {
    const char *name;
    const char *color;
};

// Readable on light and dark themes alike; Finder's order.
constexpr PaletteEntry kPalette[] = {
    { "Red", "#e5534b" },
    { "Orange", "#e8883a" },
    { "Yellow", "#d9b33b" },
    { "Green", "#4caf6a" },
    { "Blue", "#4a8fe0" },
    { "Purple", "#a46fd6" },
    { "Gray", "#8b8f96" },
};

QStringList normalized(const QStringList &tags)
{
    QStringList out;
    for (const QString &tag : tags) {
        const QString clean = tag.trimmed();
        if (!clean.isEmpty() && !out.contains(clean))
            out.append(clean);
    }
    return out;
}

} // namespace

Tags::Tags(QObject *parent)
    : QObject(parent)
{
    load();
}

QString Tags::filePath() const
{
    return ProPaths::file(QStringLiteral("tags"));
}

void Tags::load()
{
    m_index.clear();
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0)
            continue;
        const QString path = line.left(tab);
        const QStringList tags = normalized(line.mid(tab + 1).split(QLatin1Char(',')));
        if (path.startsWith(QLatin1Char('/')) && !tags.isEmpty())
            m_index.insert(path, tags);
    }
}

void Tags::save()
{
    QByteArray data;
    for (auto it = m_index.cbegin(); it != m_index.cend(); ++it)
        data += it.key().toUtf8() + '\t' + it->join(QLatin1Char(',')).toUtf8() + '\n';
    ProPaths::write(filePath(), data);
}

QStringList Tags::palette()
{
    QStringList out;
    for (const PaletteEntry &entry : kPalette)
        out.append(QString::fromLatin1(entry.name));
    return out;
}

QString Tags::colorFor(const QString &tag)
{
    for (const PaletteEntry &entry : kPalette) {
        if (tag.compare(QLatin1String(entry.name), Qt::CaseInsensitive) == 0)
            return QString::fromLatin1(entry.color);
    }
    return QStringLiteral("#8b8f96");
}

QStringList Tags::readAttribute(const QString &path, bool *ok)
{
    if (ok)
        *ok = false;
    if (!path.startsWith(QLatin1Char('/')))
        return {};
    const QByteArray native = QFile::encodeName(path);
    char buffer[4096];
    const ssize_t length = getxattr(native.constData(), kAttribute, buffer, sizeof buffer);
    if (length < 0) {
        // No attribute is the normal case, and still a successful read.
        if (ok)
            *ok = errno == ENODATA;
        return {};
    }
    if (ok)
        *ok = true;
    return normalized(QString::fromUtf8(buffer, int(length)).split(QLatin1Char(',')));
}

bool Tags::writeAttribute(const QString &path, const QStringList &tags)
{
    if (!path.startsWith(QLatin1Char('/')))
        return false;
    const QByteArray native = QFile::encodeName(path);
    const QStringList clean = normalized(tags);
    if (clean.isEmpty()) {
        return removexattr(native.constData(), kAttribute) == 0 || errno == ENODATA;
    }
    const QByteArray value = clean.join(QLatin1Char(',')).toUtf8();
    return setxattr(native.constData(), kAttribute, value.constData(), size_t(value.size()), 0) == 0;
}

QStringList Tags::tagsFor(const QString &path)
{
    if (!path.startsWith(QLatin1Char('/')))
        return {};
    auto it = m_cache.constFind(path);
    if (it != m_cache.cend())
        return *it;
    // A folder full of files asks once each; the cache keeps the delegate
    // bindings from re-reading the attribute on every repaint.
    if (m_cache.size() > 20000)
        m_cache.clear();
    const QStringList tags = readAttribute(path);
    m_cache.insert(path, tags);
    return tags;
}

bool Tags::allHave(const QStringList &paths, const QString &tag)
{
    if (paths.isEmpty())
        return false;
    for (const QString &path : paths) {
        if (!tagsFor(path).contains(tag, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

void Tags::apply(const QString &path, const QStringList &tags)
{
    if (!writeAttribute(path, tags))
        return;
    m_cache.insert(path, normalized(tags));
    if (tags.isEmpty())
        m_index.remove(path);
    else
        m_index.insert(path, normalized(tags));
}

void Tags::setTag(const QStringList &paths, const QString &tag, bool on)
{
    for (const QString &path : paths) {
        QStringList tags = readAttribute(path);
        const bool has = tags.contains(tag, Qt::CaseInsensitive);
        if (on && !has)
            tags.append(tag);
        else if (!on && has)
            tags.removeIf([&tag](const QString &t) { return t.compare(tag, Qt::CaseInsensitive) == 0; });
        else
            continue;
        apply(path, tags);
    }
    save();
    ++m_revision;
    Q_EMIT changed();
}

void Tags::toggle(const QStringList &paths, const QString &tag)
{
    setTag(paths, tag, !allHave(paths, tag));
}

void Tags::clearTags(const QStringList &paths)
{
    for (const QString &path : paths)
        apply(path, {});
    save();
    ++m_revision;
    Q_EMIT changed();
}

QStringList Tags::pathsWithTag(const QString &tag)
{
    QStringList out;
    bool repaired = false;
    for (auto it = m_index.begin(); it != m_index.end();) {
        bool ok = false;
        // The attribute is the truth: a file moved by another app, or
        // re-tagged elsewhere, is dropped or corrected here.
        const QStringList now = readAttribute(it.key(), &ok);
        if (!QFileInfo::exists(it.key()) || (ok && now.isEmpty())) {
            m_cache.remove(it.key());
            it = m_index.erase(it);
            repaired = true;
            continue;
        }
        if (ok && now != *it) {
            *it = now;
            m_cache.insert(it.key(), now);
            repaired = true;
        }
        // No tag: every tagged file (the "Tags" crumb above a tag's view).
        if (tag.isEmpty() || it->contains(tag, Qt::CaseInsensitive))
            out.append(it.key());
        ++it;
    }
    if (repaired)
        save();
    out.sort();
    return out;
}

QStringList Tags::usedTags() const
{
    QStringList used;
    for (const QStringList &tags : m_index) {
        for (const QString &tag : tags) {
            if (!used.contains(tag, Qt::CaseInsensitive))
                used.append(tag);
        }
    }
    QStringList out;
    for (const QString &name : palette()) {
        if (used.contains(name, Qt::CaseInsensitive))
            out.append(name);
    }
    QStringList others;
    for (const QString &tag : std::as_const(used)) {
        if (!palette().contains(tag, Qt::CaseInsensitive))
            others.append(tag);
    }
    others.sort(Qt::CaseInsensitive);
    return out + others;
}
