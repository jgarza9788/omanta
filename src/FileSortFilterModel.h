#pragma once

#include <QCollator>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QtQmlIntegration>

// Ordering and visibility. Kept separate from DirectoryModel so that toggling
// hidden files or changing the sort column never re-reads the filesystem.
class FileSortFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(SortKey sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortDescendingChanged)
    Q_PROPERTY(bool foldersFirst READ foldersFirst WRITE setFoldersFirst NOTIFY foldersFirstChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    // The in-folder filter (`/`), read by namePattern(): plain text, a glob
    // when it holds wildcards, a regex after `re:` or when nameFilterMode is
    // "regex". An invalid pattern hides everything and says why in
    // nameFilterError.
    Q_PROPERTY(QString nameFilter READ nameFilter WRITE setNameFilter NOTIFY nameFilterChanged)
    Q_PROPERTY(QString nameFilterMode READ nameFilterMode WRITE setNameFilterMode NOTIFY nameFilterChanged)
    Q_PROPERTY(QString nameFilterError READ nameFilterError NOTIFY nameFilterChanged)
    Q_PROPERTY(bool foldersOnly READ foldersOnly WRITE setFoldersOnly NOTIFY foldersOnlyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum SortKey {
        ByName,
        BySize,
        ByType,
        ByModified,
        ByCreated,
        ByAccessed,
        ByOwner,
        ByGroup,
        ByPermissions,
    };
    Q_ENUM(SortKey)

    explicit FileSortFilterModel(QObject *parent = nullptr);

    SortKey sortKey() const { return m_sortKey; }
    void setSortKey(SortKey key);

    bool sortDescending() const { return m_sortDescending; }
    void setSortDescending(bool descending);

    bool foldersFirst() const { return m_foldersFirst; }
    void setFoldersFirst(bool foldersFirst);

    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool showHidden);

    // The folder picker's view: directories and nothing else.
    bool foldersOnly() const { return m_foldersOnly; }
    void setFoldersOnly(bool foldersOnly);

    QString nameFilter() const { return m_nameFilter; }
    void setNameFilter(const QString &filter);
    QString nameFilterMode() const { return m_nameFilterMode; }
    void setNameFilterMode(const QString &mode);
    QString nameFilterError() const { return m_nameFilterError; }

    int count() const { return rowCount(); }

    // Row translation, needed whenever the view talks to the source model.
    Q_INVOKABLE int sourceRow(int proxyRow) const;
    Q_INVOKABLE int proxyRowForName(const QString &name) const;
    Q_INVOKABLE QVariant valueAt(int proxyRow, const QString &roleName) const;

    // Type-ahead: first row whose display name starts with `prefix`, searching
    // forward from `startRow` and wrapping, or -1.
    Q_INVOKABLE int findByPrefix(const QString &prefix, int startRow) const;

    // Select by pattern: the `name` of every visible row whose display name
    // matches (namePattern rules, `mode` "auto" or "regex"). Empty when the
    // pattern is invalid.
    Q_INVOKABLE QStringList namesMatching(const QString &pattern, const QString &mode) const;
    // Why `pattern` cannot be used, or "" when it can.
    Q_INVOKABLE static QString patternError(const QString &pattern, const QString &mode);

    // The one rule for "does this name match what was typed", shared with
    // SearchModel so the filter and the recursive search never disagree.
    // `mode` "regex" takes the pattern as a regular expression; anything
    // else is "auto": a `re:` prefix means regex, * ? or [ mean a glob over
    // the whole name, otherwise a case-insensitive substring. Regexes are
    // smart-case: insensitive unless the pattern has an upper-case letter.
    // An empty pattern gives an empty QString pattern (match everything);
    // an invalid one sets *error.
    static QRegularExpression namePattern(const QString &pattern, const QString &mode,
                                          QString *error = nullptr);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

Q_SIGNALS:
    void sortKeyChanged();
    void sortDescendingChanged();
    void foldersFirstChanged();
    void showHiddenChanged();
    void foldersOnlyChanged();
    void nameFilterChanged();
    void countChanged();

private:
    void applySort();

    SortKey m_sortKey = ByName;
    bool m_sortDescending = false;
    bool m_foldersFirst = true;
    bool m_showHidden = false;
    bool m_foldersOnly = false;
    QString m_nameFilter;
    QString m_nameFilterMode = QStringLiteral("auto");
    QRegularExpression m_namePattern;
    QString m_nameFilterError;
    QCollator m_collator;
};
