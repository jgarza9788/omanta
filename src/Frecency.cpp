#include "Frecency.h"
#include "ProPaths.h"

#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

#include <algorithm>

Frecency::Frecency(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, &QTimer::timeout, this, &Frecency::save);
    load();
}

Frecency::~Frecency()
{
    if (m_dirty)
        save();
}

QString Frecency::filePath() const
{
    return ProPaths::file(QStringLiteral("frecency"));
}

void Frecency::load()
{
    m_entries.clear();
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() != 3 || !parts.at(2).startsWith(QLatin1Char('/')))
            continue;
        bool countOk = false;
        bool lastOk = false;
        Entry entry{ parts.at(0).toInt(&countOk), parts.at(1).toLongLong(&lastOk) };
        if (countOk && lastOk && entry.count > 0)
            m_entries.insert(parts.at(2), entry);
    }
}

void Frecency::save()
{
    m_dirty = false;
    // Past the cap, the weakest go first.
    if (m_entries.size() > kMaxEntries) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QList<QPair<double, QString>> ranked;
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
            ranked.append({ frecency(it->count, it->last, now), it.key() });
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });
        for (int i = kMaxEntries; i < ranked.size(); ++i)
            m_entries.remove(ranked.at(i).second);
    }
    QByteArray data;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        data += QByteArray::number(it->count) + '\t' + QByteArray::number(it->last) + '\t'
              + it.key().toUtf8() + '\n';
    }
    ProPaths::write(filePath(), data);
}

void Frecency::scheduleSave()
{
    m_dirty = true;
    m_saveTimer.start();
}

void Frecency::visit(const QString &path)
{
    if (!path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\n'))
        || path.contains(QLatin1Char('\t')))
        return;
    Entry &entry = m_entries[path];
    ++entry.count;
    entry.last = QDateTime::currentSecsSinceEpoch();
    scheduleSave();
}

void Frecency::clear()
{
    m_entries.clear();
    scheduleSave();
}

double Frecency::frecency(int count, qint64 lastVisit, qint64 now)
{
    const qint64 age = now - lastVisit;
    double weight = 0.25;
    if (age < 3600)
        weight = 4.0;
    else if (age < 86400)
        weight = 2.0;
    else if (age < 7 * 86400)
        weight = 0.5;
    return count * weight;
}

int Frecency::fuzzyScore(const QString &needle, const QString &haystack)
{
    if (needle.isEmpty())
        return 0;
    const QString n = needle.toLower();
    const QString h = haystack.toLower();
    // The last component is what people type, so a match there counts more.
    const int baseStart = int(h.lastIndexOf(QLatin1Char('/'))) + 1;

    // One greedy left-to-right pass from `from`; -1 when the letters run out.
    const auto pass = [&](int from) {
        int score = 0;
        int at = from;
        int previous = -2;
        for (const QChar c : n) {
            if (c == QLatin1Char(' '))
                continue; // "dl proj" reads as two words, not literal spaces
            const int found = int(h.indexOf(c, at));
            if (found < 0)
                return -1;
            int points = 1;
            if (found == previous + 1)
                points += 5; // a run of letters
            if (found == 0 || QStringLiteral("/-_. ").contains(h.at(found - 1)))
                points += 4; // a word start
            if (found >= baseStart)
                points += 3;
            score += points;
            previous = found;
            at = found + 1;
        }
        return score;
    };
    // Greedy from the start can spend the letters on a parent folder that
    // happens to share them; matching inside the last component alone is
    // tried too, and the better of the two counts.
    int score = std::max(pass(0), pass(baseStart));
    if (score < 0)
        return -1;
    const QString base = h.mid(baseStart);
    const QString compact = QString(n).remove(QLatin1Char(' '));
    if (base.startsWith(compact))
        score += 10; // the folder's name starts with what was typed
    // Shorter haystacks with the same letters are the closer match.
    return score * 100 / (100 + int(h.size()));
}

QVariantList Frecency::search(const QString &query, int limit) const
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    struct Hit {
        double score;
        QString path;
    };
    QList<Hit> hits;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        const double weight = frecency(it->count, it->last, now);
        if (query.trimmed().isEmpty()) {
            hits.append({ weight, it.key() });
            continue;
        }
        const int match = fuzzyScore(query, it.key());
        if (match < 0)
            continue;
        // The match decides first; how often you go there breaks near-ties.
        hits.append({ match * 10.0 + std::min(weight, 40.0), it.key() });
    }
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        return a.score != b.score ? a.score > b.score : a.path < b.path;
    });

    QVariantList out;
    for (const Hit &hit : std::as_const(hits)) {
        if (out.size() >= limit)
            break;
        if (!QFileInfo(hit.path).isDir())
            continue;
        QVariantMap row;
        row.insert(QStringLiteral("path"), hit.path);
        row.insert(QStringLiteral("name"), hit.path == QLatin1String("/")
                   ? hit.path : QFileInfo(hit.path).fileName());
        row.insert(QStringLiteral("score"), hit.score);
        out.append(row);
    }
    return out;
}
