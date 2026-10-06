#include "FileSortFilterModel.h"

#include "DirectoryModel.h"

FileSortFilterModel::FileSortFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // "file10" must sort after "file9", and case must not split the list into
    // two alphabets — this is the difference between feeling like a file
    // manager and feeling like `ls`.
    // Qt's C-locale collator ignores numeric mode. Use a Unicode collator
    // there too, while retaining the user's locale everywhere else.
    if (m_collator.locale().language() == QLocale::C)
        m_collator.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);

    setDynamicSortFilter(true);
    applySort();

    connect(this, &QAbstractItemModel::rowsInserted, this, &FileSortFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &FileSortFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &FileSortFilterModel::countChanged);
    // A full invalidate() reports its rescue of rows as layoutChanged, not as
    // inserts/removes — without this, Ctrl+H left `count` bindings stale.
    connect(this, &QAbstractItemModel::layoutChanged, this, &FileSortFilterModel::countChanged);

    // Media values arrive a few at a time; re-sort once they settle.
    m_extraResort.setSingleShot(true);
    m_extraResort.setInterval(300);
    connect(&m_extraResort, &QTimer::timeout, this, [this] {
        if (m_sortKey == ByExtra)
            invalidate();
    });
}

void FileSortFilterModel::setHiddenNames(const QStringList &names)
{
    const QSet<QString> next(names.cbegin(), names.cend());
    if (next == m_hiddenNames)
        return;
    beginFilterChange();
    m_hiddenNames = next;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT hiddenNamesChanged();
}

void FileSortFilterModel::setExtraSource(QObject *source)
{
    if (m_extraSource == source)
        return;
    if (m_extraSource)
        disconnect(m_extraSource, nullptr, this, nullptr);
    m_extraSource = source;
    if (m_extraSource && m_extraSource->metaObject()->indexOfSignal("changed()") >= 0)
        connect(m_extraSource, SIGNAL(changed()), &m_extraResort, SLOT(start()));
    Q_EMIT extraSortChanged();
    if (m_sortKey == ByExtra)
        invalidate();
}

void FileSortFilterModel::setExtraSortField(const QString &field)
{
    if (m_extraSortField == field)
        return;
    m_extraSortField = field;
    Q_EMIT extraSortChanged();
    if (m_sortKey == ByExtra)
        invalidate();
}

QVariant FileSortFilterModel::extraValue(const QModelIndex &index) const
{
    if (!m_extraSource || m_extraSortField.isEmpty())
        return {};
    QVariant value;
    QMetaObject::invokeMethod(m_extraSource.data(), "sortValue", Qt::DirectConnection,
                              Q_RETURN_ARG(QVariant, value),
                              Q_ARG(QString, index.data(DirectoryModel::FilePathRole).toString()),
                              Q_ARG(QString, m_extraSortField));
    return value;
}

void FileSortFilterModel::applySort()
{
    sort(0, m_sortDescending ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void FileSortFilterModel::setSortKey(SortKey key)
{
    if (m_sortKey == key)
        return;
    m_sortKey = key;
    Q_EMIT sortKeyChanged();
    invalidate();
    applySort();
}

void FileSortFilterModel::setSortDescending(bool descending)
{
    if (m_sortDescending == descending)
        return;
    m_sortDescending = descending;
    Q_EMIT sortDescendingChanged();
    applySort();
}

void FileSortFilterModel::setFoldersFirst(bool foldersFirst)
{
    if (m_foldersFirst == foldersFirst)
        return;
    m_foldersFirst = foldersFirst;
    Q_EMIT foldersFirstChanged();
    invalidate();
    applySort();
}

void FileSortFilterModel::setShowHidden(bool showHidden)
{
    if (m_showHidden == showHidden)
        return;
    m_showHidden = showHidden;
    Q_EMIT showHiddenChanged();
    // Not just the filter: the size sort orders folders by their *visible*
    // item count, so revealing hidden files can reorder folders too.
    invalidate();
    applySort();
}

void FileSortFilterModel::setNameFilter(const QString &filter)
{
    if (m_nameFilter == filter)
        return;
    // Qt 6.10's filter-change bracket: begin before the state changes,
    // end once filterAcceptsRow() would answer differently.
    beginFilterChange();
    m_nameFilter = filter;
    m_nameFilterError.clear();
    m_namePattern = namePattern(m_nameFilter, m_nameFilterMode, &m_nameFilterError);
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT nameFilterChanged();
}

void FileSortFilterModel::setNameFilterMode(const QString &mode)
{
    const QString clean = mode == QLatin1String("regex") ? mode : QStringLiteral("auto");
    if (m_nameFilterMode == clean)
        return;
    beginFilterChange();
    m_nameFilterMode = clean;
    m_nameFilterError.clear();
    m_namePattern = namePattern(m_nameFilter, m_nameFilterMode, &m_nameFilterError);
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT nameFilterChanged();
}

void FileSortFilterModel::setFoldersOnly(bool foldersOnly)
{
    if (m_foldersOnly == foldersOnly)
        return;
    beginFilterChange();
    m_foldersOnly = foldersOnly;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT foldersOnlyChanged();
}

bool FileSortFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!idx.isValid())
        return false;

    if (!m_showHidden) {
        // GIO's is-hidden covers dotfiles; is-backup covers editor droppings
        // like `notes.txt~`. Nautilus hides both behind the same toggle.
        if (idx.data(DirectoryModel::IsHiddenRole).toBool()
            || idx.data(DirectoryModel::IsBackupRole).toBool())
            return false;
    }

    if (m_foldersOnly && !idx.data(DirectoryModel::IsDirRole).toBool())
        return false;

    if (!m_hiddenNames.isEmpty() && m_hiddenNames.contains(idx.data(DirectoryModel::NameRole).toString()))
        return false;

    if (!m_nameFilter.trimmed().isEmpty()
        && (!m_nameFilterError.isEmpty()
            || !m_namePattern.match(idx.data(DirectoryModel::DisplayNameRole).toString()).hasMatch()))
        return false;

    return true;
}

bool FileSortFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (m_foldersFirst) {
        const bool leftDir = left.data(DirectoryModel::IsDirRole).toBool();
        const bool rightDir = right.data(DirectoryModel::IsDirRole).toBool();
        if (leftDir != rightDir) {
            // Folders lead regardless of direction, so a reversed sort flips
            // the files without flinging the folders to the bottom.
            return m_sortDescending ? rightDir : leftDir;
        }
    }

    switch (m_sortKey) {
    case BySize: {
        // Nautilus's compare_by_size: a folder's on-disk size is meaningless,
        // so folders group together (before files) and order by item count —
        // the count the user can see, so it follows the hidden toggle. Folders
        // whose count is unknown (-1) go after the counted ones.
        const bool leftDir = left.data(DirectoryModel::IsDirRole).toBool();
        const bool rightDir = right.data(DirectoryModel::IsDirRole).toBool();
        if (leftDir != rightDir)
            return leftDir;
        if (leftDir) {
            const int role = m_showHidden ? DirectoryModel::ItemCountAllRole
                                          : DirectoryModel::ItemCountRole;
            const int l = left.data(role).toInt();
            const int r = right.data(role).toInt();
            if (l != r) {
                if (l < 0 || r < 0)
                    return r < 0;
                return l < r;
            }
            break;
        }
        const qint64 l = left.data(DirectoryModel::SizeRole).toLongLong();
        const qint64 r = right.data(DirectoryModel::SizeRole).toLongLong();
        if (l != r)
            return l < r;
        break;
    }
    case ByModified: {
        const QDateTime l = left.data(DirectoryModel::ModifiedRole).toDateTime();
        const QDateTime r = right.data(DirectoryModel::ModifiedRole).toDateTime();
        if (l != r)
            return l < r;
        break;
    }
    case ByCreated: {
        const QDateTime l = left.data(DirectoryModel::CreatedRole).toDateTime();
        const QDateTime r = right.data(DirectoryModel::CreatedRole).toDateTime();
        if (l != r)
            return l < r;
        break;
    }
    case ByAccessed: {
        const QDateTime l = left.data(DirectoryModel::AccessedRole).toDateTime();
        const QDateTime r = right.data(DirectoryModel::AccessedRole).toDateTime();
        if (l != r)
            return l < r;
        break;
    }
    case ByType: {
        const QString l = left.data(DirectoryModel::TypeDescriptionRole).toString();
        const QString r = right.data(DirectoryModel::TypeDescriptionRole).toString();
        const int cmp = m_collator.compare(l, r);
        if (cmp != 0)
            return cmp < 0;
        break;
    }
    case ByOwner: {
        const QString l = left.data(DirectoryModel::OwnerRole).toString();
        const QString r = right.data(DirectoryModel::OwnerRole).toString();
        const int cmp = m_collator.compare(l, r);
        if (cmp != 0)
            return cmp < 0;
        break;
    }
    case ByGroup: {
        const QString l = left.data(DirectoryModel::GroupRole).toString();
        const QString r = right.data(DirectoryModel::GroupRole).toString();
        const int cmp = m_collator.compare(l, r);
        if (cmp != 0)
            return cmp < 0;
        break;
    }
    case ByPermissions: {
        // Plain string order — "drwx…" groups directories, as Nautilus.
        const QString l = left.data(DirectoryModel::PermissionsRole).toString();
        const QString r = right.data(DirectoryModel::PermissionsRole).toString();
        if (l != r)
            return l < r;
        break;
    }
    case ByExtra: {
        const QVariant l = extraValue(left);
        const QVariant r = extraValue(right);
        // Unknown values (not media, or not read yet) sort after known ones,
        // whichever way the column is sorted.
        if (l.isValid() != r.isValid())
            return m_sortDescending ? !l.isValid() : l.isValid();
        if (l.isValid()) {
            if (l.typeId() == QMetaType::QString) {
                const int cmp = m_collator.compare(l.toString(), r.toString());
                if (cmp != 0)
                    return cmp < 0;
            } else {
                const QPartialOrdering order = QVariant::compare(l, r);
                if (order == QPartialOrdering::Less)
                    return true;
                if (order == QPartialOrdering::Greater)
                    return false;
            }
        }
        break;
    }
    case ByName:
        break;
    }

    // Name is both the default key and the tie-breaker, so equal sizes or
    // timestamps still produce a stable, predictable order.
    const QString l = left.data(DirectoryModel::DisplayNameRole).toString();
    const QString r = right.data(DirectoryModel::DisplayNameRole).toString();
    // Nautilus's compare_by_display_name: names starting with "." or "#"
    // sort last, rather than first as the collator would put them (#21).
    const auto sortsLast = [](const QString &name) {
        return name.startsWith(QLatin1Char('.')) || name.startsWith(QLatin1Char('#'));
    };
    if (sortsLast(l) != sortsLast(r))
        return sortsLast(r);
    return m_collator.compare(l, r) < 0;
}

int FileSortFilterModel::sourceRow(int proxyRow) const
{
    const QModelIndex idx = index(proxyRow, 0);
    return idx.isValid() ? mapToSource(idx).row() : -1;
}

int FileSortFilterModel::proxyRowForName(const QString &name) const
{
    // DirectoryModel answers from its hash; any other source (SearchModel)
    // gets a scan. Search results number at most a thousand, so the scan is
    // cheap where it happens.
    if (auto *source = qobject_cast<DirectoryModel *>(sourceModel())) {
        const int row = source->indexOfName(name);
        if (row < 0)
            return -1;
        const QModelIndex proxyIndex = mapFromSource(source->index(row));
        return proxyIndex.isValid() ? proxyIndex.row() : -1;
    }

    for (int row = 0; row < rowCount(); ++row) {
        if (index(row, 0).data(DirectoryModel::NameRole).toString() == name)
            return row;
    }
    return -1;
}

QVariant FileSortFilterModel::valueAt(int proxyRow, const QString &roleName) const
{
    const QModelIndex idx = index(proxyRow, 0);
    if (!idx.isValid())
        return {};
    // Ctrl+A over a few thousand files calls this once per row; a linear scan
    // building a QByteArray each time turns selection into a visible stall.
    static thread_local QHash<QString, int> roleIds;
    if (roleIds.isEmpty()) {
        const QHash<int, QByteArray> names = roleNames();
        for (auto it = names.cbegin(); it != names.cend(); ++it)
            roleIds.insert(QString::fromUtf8(it.value()), it.key());
    }

    const int role = roleIds.value(roleName, -1);
    return role < 0 ? QVariant{} : idx.data(role);
}

int FileSortFilterModel::findByPrefix(const QString &prefix, int startRow) const
{
    const int total = rowCount();
    if (total == 0 || prefix.isEmpty())
        return -1;

    for (int offset = 0; offset < total; ++offset) {
        const int row = (startRow + offset) % total;
        const QString name = index(row, 0).data(DirectoryModel::DisplayNameRole).toString();
        if (name.startsWith(prefix, Qt::CaseInsensitive))
            return row;
    }
    return -1;
}

QRegularExpression FileSortFilterModel::namePattern(const QString &raw, const QString &mode,
                                                    QString *error)
{
    QString pattern = raw.trimmed();
    bool regex = mode == QLatin1String("regex");
    if (pattern.startsWith(QLatin1String("re:"))) {
        regex = true;
        pattern = pattern.mid(3);
    }
    if (pattern.isEmpty())
        return QRegularExpression();

    QRegularExpression compiled;
    if (regex) {
        bool hasUpper = false;
        for (const QChar c : std::as_const(pattern))
            hasUpper = hasUpper || c.isUpper();
        compiled = QRegularExpression(pattern, hasUpper ? QRegularExpression::NoPatternOption
                                                        : QRegularExpression::CaseInsensitiveOption);
    } else if (pattern.contains(QLatin1Char('*')) || pattern.contains(QLatin1Char('?'))
               || pattern.contains(QLatin1Char('['))) {
        compiled = QRegularExpression(QRegularExpression::wildcardToRegularExpression(
                                          pattern, QRegularExpression::NonPathWildcardConversion),
                                      QRegularExpression::CaseInsensitiveOption);
    } else {
        compiled = QRegularExpression(QRegularExpression::escape(pattern),
                                      QRegularExpression::CaseInsensitiveOption);
    }
    if (!compiled.isValid() && error)
        *error = QStringLiteral("Invalid pattern: %1").arg(compiled.errorString());
    return compiled;
}

QStringList FileSortFilterModel::namesMatching(const QString &pattern, const QString &mode) const
{
    QString error;
    const QRegularExpression compiled = namePattern(pattern, mode, &error);
    QStringList names;
    if (compiled.pattern().isEmpty() || !error.isEmpty())
        return names;
    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex idx = index(row, 0);
        if (compiled.match(idx.data(DirectoryModel::DisplayNameRole).toString()).hasMatch())
            names.append(idx.data(DirectoryModel::NameRole).toString());
    }
    return names;
}

QString FileSortFilterModel::patternError(const QString &pattern, const QString &mode)
{
    QString error;
    namePattern(pattern, mode, &error);
    return error;
}
