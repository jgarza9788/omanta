#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "BatchPermissions.h"
#include "DiskUsage.h"
#include "DuplicateFinder.h"
#include "FileOperations.h"
#include "FolderCompare.h"
#include "Frecency.h"
#include "GitStatus.h"
#include "MediaInfo.h"
#include "Platform.h"
#include "SavedSearches.h"
#include "Settings.h"
#include "ShellSession.h"
#include "SyntaxHighlighter.h"
#include "Tags.h"
#include "TestFixture.h"

#include <sys/stat.h>

// The pro features' engines, without a window: ranking, tagging, git
// folding, media reading, comparing, scanning, de-duplicating and the rest.
class TestProFeatures : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void frecencyRanksByMatchThenUse();
    void frecencyPersistsAndSkipsVirtualPlaces();
    void fuzzyScorePrefersWordStartsAndRuns();
    void tagsLiveInTheExtendedAttribute();
    void gitStatusFoldsOntoChildren();
    void gitStatusAgainstARealRepository();
    void mediaInfoReadsHeadersAndExif();
    void mediaInfoParsesFfprobe();
    void languageIsPickedByNameAndShebang();
    void compareFindsEveryKindOfDifference();
    void diskUsageTotalsAndLevels();
    void duplicatesAreFoundAndLinked();
    void batchPermissionsSetClearAndScope();
    void shellCdResolves();
    void shellRunsInTheFolder();
    void savedSearchesRoundTrip();
    void pathCompletionCompletesFolders();
    void verifyCatchesAMismatch();
    void settingsMarksAndProColumns();

private:
    QTemporaryDir m_config;
};

void TestProFeatures::init()
{
    QVERIFY(m_config.isValid());
    qputenv("OMANTA_PRO_DIR", m_config.path().toUtf8());
    qputenv("OMANTA_SETTINGS_FILE", m_config.filePath("settings").toUtf8());
    for (const char *name : { "frecency", "tags", "saved-searches", "settings" })
        QFile::remove(m_config.filePath(QString::fromLatin1(name)));
}

void TestProFeatures::frecencyRanksByMatchThenUse()
{
    TempTree tree;
    const QString projects = tree.makeDir("projects");
    const QString photos = tree.makeDir("photos/2026");
    const QString downloads = tree.makeDir("Downloads");

    Frecency frecency;
    for (int i = 0; i < 5; ++i)
        frecency.visit(photos);
    frecency.visit(projects);
    frecency.visit(downloads);

    // No query: the most used first.
    const QVariantList top = frecency.search(QString(), 10);
    QCOMPARE(top.size(), 3);
    QCOMPARE(top.first().toMap().value("path").toString(), photos);

    // A query: the match decides.
    const QVariantList proj = frecency.search("proj", 10);
    QVERIFY(!proj.isEmpty());
    QCOMPARE(proj.first().toMap().value("path").toString(), projects);
    QCOMPARE(proj.first().toMap().value("name").toString(), QStringLiteral("projects"));

    // Nothing that doesn't match, and gone folders are skipped.
    QVERIFY(frecency.search("zzzz", 10).isEmpty());
    QDir(downloads).removeRecursively();
    QVERIFY(frecency.search("downl", 10).isEmpty());
}

void TestProFeatures::frecencyPersistsAndSkipsVirtualPlaces()
{
    TempTree tree;
    const QString folder = tree.makeDir("work");
    {
        Frecency frecency;
        frecency.visit(folder);
        frecency.visit("trash:///");
        frecency.visit("smb://server/share");
    } // saved on destruction
    Frecency reread;
    QCOMPARE(reread.size(), 1);
    QCOMPARE(reread.search("work", 5).first().toMap().value("path").toString(), folder);

    reread.clear();
    QCOMPARE(reread.size(), 0);
}

void TestProFeatures::fuzzyScorePrefersWordStartsAndRuns()
{
    QCOMPARE(Frecency::fuzzyScore("xyz", "/home/me/projects"), -1);
    QVERIFY(Frecency::fuzzyScore("prj", "/home/me/projects") > 0);
    // A run of letters beats scattered ones.
    QVERIFY(Frecency::fuzzyScore("proj", "/home/me/projects")
            > Frecency::fuzzyScore("proj", "/p/r/o/j"));
    // The last component counts more than the path above it.
    QVERIFY(Frecency::fuzzyScore("docs", "/srv/docs")
            > Frecency::fuzzyScore("docs", "/docs/srv/archive"));
    // Case doesn't matter; spaces separate words.
    QVERIFY(Frecency::fuzzyScore("DL", "/home/me/Downloads") > 0);
    QVERIFY(Frecency::fuzzyScore("me dl", "/home/me/Downloads") > 0);
}

void TestProFeatures::tagsLiveInTheExtendedAttribute()
{
    TempTree tree(TempTree::UnderHome);
    const QString a = tree.writeFile("a.txt");
    const QString b = tree.writeFile("b.txt");

    // Some filesystems refuse user attributes; nothing to test there.
    if (!Tags::writeAttribute(a, { "probe" }))
        QSKIP("this filesystem has no user extended attributes");
    QVERIFY(Tags::writeAttribute(a, {}));

    Tags tags;
    QSignalSpy changed(&tags, &Tags::changed);
    tags.toggle({ a, b }, "Red");
    QCOMPARE(changed.count(), 1);
    QCOMPARE(Tags::readAttribute(a), QStringList{ "Red" });
    QVERIFY(tags.allHave({ a, b }, "Red"));
    QCOMPARE(tags.usedTags(), QStringList{ "Red" });

    tags.setTag({ a }, "Blue", true);
    QCOMPARE(Tags::readAttribute(a), (QStringList{ "Red", "Blue" }));
    QCOMPARE(tags.tagsFor(a), (QStringList{ "Red", "Blue" }));
    QCOMPARE(tags.pathsWithTag("Blue"), QStringList{ a });
    QCOMPARE(tags.pathsWithTag("Red"), (QStringList{ a, b }));
    QCOMPARE(tags.pathsWithTag(""), (QStringList{ a, b }));

    // Toggling when all have it removes it from all.
    tags.toggle({ a, b }, "Red");
    QVERIFY(!tags.allHave({ a }, "Red"));
    QCOMPARE(Tags::readAttribute(b), QStringList());

    // The index survives a restart, and moves are followed by the
    // attribute, which travels with the file.
    Tags reread;
    QCOMPARE(reread.pathsWithTag("Blue"), QStringList{ a });
    QFile::remove(a);
    QVERIFY(reread.pathsWithTag("Blue").isEmpty());

    QCOMPARE(Tags::colorFor("red"), QStringLiteral("#e5534b"));
    QCOMPARE(Tags::palette().size(), 7);
}

void TestProFeatures::gitStatusFoldsOntoChildren()
{
    // porcelain v1 -z: "XY path\0", a rename's old path as the next record.
    QByteArray out;
    out += QByteArray(" M src/main.cpp") + '\0';
    out += QByteArray("?? notes.txt") + '\0';
    out += QByteArray("A  src/new.cpp") + '\0';
    out += QByteArray("!! build/") + '\0';
    out += QByteArray("!! src/cache/x.o") + '\0';
    out += QByteArray("R  docs/new.md") + '\0' + QByteArray("docs/old.md") + '\0';
    out += QByteArray("UU conflict.txt") + '\0';

    QStringList ignored;
    const auto status = GitStatus::parse(out, "/repo", "/repo", &ignored);
    QCOMPARE(status.value("src"), QStringLiteral("modified")); // strongest inside
    QCOMPARE(status.value("notes.txt"), QStringLiteral("untracked"));
    QCOMPARE(status.value("build"), QStringLiteral("ignored"));
    QCOMPARE(status.value("docs"), QStringLiteral("modified"));
    QCOMPARE(status.value("conflict.txt"), QStringLiteral("conflict"));
    QCOMPARE(ignored, QStringList{ "build" });

    // Inside src/: its own children; the deep ignored file is a direct child
    // of src/cache, so here it shows on "cache".
    const auto inner = GitStatus::parse(out, "/repo", "/repo/src", &ignored);
    QCOMPARE(inner.value("main.cpp"), QStringLiteral("modified"));
    QCOMPARE(inner.value("new.cpp"), QStringLiteral("added"));
    QVERIFY(!inner.contains("notes.txt"));
    QVERIFY(ignored.isEmpty());
}

void TestProFeatures::gitStatusAgainstARealRepository()
{
    if (QStandardPaths::findExecutable("git").isEmpty())
        QSKIP("git is not installed");
    TempTree tree;
    const auto git = [&tree](const QStringList &args) {
        QProcess process;
        process.setWorkingDirectory(tree.path());
        process.start("git", QStringList{ "-c", "user.email=t@t", "-c", "user.name=t" } + args);
        return process.waitForFinished(10000) && process.exitCode() == 0;
    };
    QVERIFY(git({ "init", "-q" }));
    tree.writeFile("tracked.txt");
    tree.writeFile(".gitignore", 0);
    {
        QFile ignore(tree.filePath(".gitignore"));
        QVERIFY(ignore.open(QIODevice::WriteOnly));
        ignore.write("*.log\n");
    }
    QVERIFY(git({ "add", "tracked.txt", ".gitignore" }));
    QVERIFY(git({ "commit", "-q", "-m", "one" }));
    tree.writeFile("tracked.txt", 20); // modified
    tree.writeFile("fresh.txt");       // untracked
    tree.writeFile("debug.log");       // ignored

    GitStatus status;
    QSignalSpy changed(&status, &GitStatus::statusChanged);
    status.setFolder(tree.path());
    status.setEnabled(true);
    QTRY_VERIFY(status.isRepo());
    QCOMPARE(status.statusOf("tracked.txt"), QStringLiteral("modified"));
    QCOMPARE(status.statusOf("fresh.txt"), QStringLiteral("untracked"));
    QCOMPARE(status.statusOf("debug.log"), QStringLiteral("ignored"));
    QCOMPARE(status.ignoredNames(), QStringList{ "debug.log" });
    QVERIFY(!status.branch().isEmpty());

    // Outside a repository: nothing.
    TempTree plain;
    status.setFolder(plain.path());
    QTest::qWait(300);
    QVERIFY(!status.isRepo());
    QCOMPARE(status.statusOf("tracked.txt"), QString());
}

static QByteArray jpegWithExifDate(const QByteArray &date)
{
    // SOI, APP1 "Exif\0\0" with a little-endian TIFF: IFD0 holding only an
    // Exif pointer, the Exif IFD holding DateTimeOriginal, then EOI.
    QByteArray tiff("II*\0", 4);
    const auto u16 = [](QByteArray &b, quint16 v) { b.append(char(v & 0xff)).append(char(v >> 8)); };
    const auto u32 = [](QByteArray &b, quint32 v) {
        for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xff));
    };
    u32(tiff, 8);                  // IFD0 at 8
    u16(tiff, 1);                  // one entry
    u16(tiff, 0x8769); u16(tiff, 4); u32(tiff, 1); u32(tiff, 26); // → Exif IFD at 26
    u32(tiff, 0);                  // no next IFD
    u16(tiff, 1);                  // Exif IFD: one entry
    u16(tiff, 0x9003); u16(tiff, 2); u32(tiff, 20); u32(tiff, 44); // date at 44
    u32(tiff, 0);
    tiff += date + QByteArray(1, '\0');

    QByteArray app1 = QByteArray("Exif\0\0", 6) + tiff;
    QByteArray jpeg("\xFF\xD8", 2);
    jpeg += QByteArray("\xFF\xE1", 2);
    const int length = int(app1.size()) + 2;
    jpeg.append(char(length >> 8)).append(char(length & 0xff));
    jpeg += app1;
    jpeg += QByteArray("\xFF\xD9", 2);
    return jpeg;
}

void TestProFeatures::mediaInfoReadsHeadersAndExif()
{
    const QDateTime when = MediaInfo::exifDate(jpegWithExifDate("2024:07:14 18:30:05"));
    QCOMPARE(when, QDateTime(QDate(2024, 7, 14), QTime(18, 30, 5)));
    QVERIFY(!MediaInfo::exifDate("not a jpeg").isValid());
    QVERIFY(!MediaInfo::exifDate(QByteArray("\xFF\xD8\xFF\xDA\x00\x02", 6)).isValid());

    TempTree tree;
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(tree.filePath("pic.png")));
    const MediaInfo::Details details = MediaInfo::read(tree.filePath("pic.png"));
    QCOMPARE(details.width, 64);
    QCOMPARE(details.height, 48);
    QVERIFY(MediaInfo::interesting(tree.filePath("pic.png")));
    QVERIFY(!MediaInfo::interesting(tree.filePath("notes.txt")));

    QCOMPARE(MediaInfo::formatDuration(65), QStringLiteral("1:05"));
    QCOMPARE(MediaInfo::formatDuration(3725), QStringLiteral("1:02:05"));
    QCOMPARE(MediaInfo::formatDuration(-1), QString());

    // Asynchronous through the shared instance: unknown first, then known.
    MediaInfo *shared = MediaInfo::instance();
    const QString path = tree.filePath("pic.png");
    QCOMPARE(shared->text(path, "dimensions"), QString());
    QTRY_COMPARE(shared->text(path, "dimensions"), QStringLiteral("64 × 48"));
    QCOMPARE(shared->sortValue(path, "dimensions").toLongLong(), 64LL * 48);
}

void TestProFeatures::mediaInfoParsesFfprobe()
{
    const QByteArray song = R"({"streams":[{"codec_type":"audio"}],
        "format":{"format_name":"mp3","duration":"215.4",
                  "tags":{"ARTIST":"The Band","album":"Songs"}}})";
    const MediaInfo::Details audio = MediaInfo::parseFfprobe(song);
    QCOMPARE(audio.seconds, 215.4);
    QCOMPARE(audio.artist, QStringLiteral("The Band"));
    QCOMPARE(audio.album, QStringLiteral("Songs"));

    const QByteArray clip = R"({"streams":[{"codec_type":"video","width":1920,"height":1080}],
        "format":{"format_name":"mov,mp4","duration":"12.0",
                  "tags":{"creation_time":"2025-03-01T10:00:00.000000Z"}}})";
    const MediaInfo::Details video = MediaInfo::parseFfprobe(clip);
    QCOMPARE(video.width, 1920);
    QCOMPARE(video.seconds, 12.0);
    QVERIFY(video.taken.isValid());

    // A still picture's token duration isn't a playing time.
    const QByteArray still = R"({"streams":[{"codec_type":"video","width":10,"height":20}],
        "format":{"format_name":"image2","duration":"0.04"}})";
    QCOMPARE(MediaInfo::parseFfprobe(still).seconds, -1.0);
}

void TestProFeatures::languageIsPickedByNameAndShebang()
{
    QCOMPARE(SyntaxHighlighter::languageFor("main.cpp"), QStringLiteral("cpp"));
    QCOMPARE(SyntaxHighlighter::languageFor("Main.qml"), QStringLiteral("js"));
    QCOMPARE(SyntaxHighlighter::languageFor("setup.PY"), QStringLiteral("python"));
    QCOMPARE(SyntaxHighlighter::languageFor("CMakeLists.txt"), QStringLiteral("cmake"));
    QCOMPARE(SyntaxHighlighter::languageFor("PKGBUILD"), QStringLiteral("shell"));
    QCOMPARE(SyntaxHighlighter::languageFor("notes.txt"), QString());
    QCOMPARE(SyntaxHighlighter::languageFor("run", "#!/usr/bin/env python3"), QStringLiteral("python"));
    QCOMPARE(SyntaxHighlighter::languageFor("run", "#!/bin/bash"), QStringLiteral("shell"));
    QVERIFY(!SyntaxHighlighter::rulesFor("rust").rules.isEmpty());
    QVERIFY(SyntaxHighlighter::rulesFor("").rules.isEmpty());
}

void TestProFeatures::compareFindsEveryKindOfDifference()
{
    TempTree left;
    TempTree right;
    left.writeFile("same.txt", 10);
    right.writeFile("same.txt", 10);
    left.writeFile("only-left.txt");
    right.writeFile("only-right.txt");
    left.writeFile("sub/deep.txt");      // a folder only on the left: one row
    left.writeFile("both/a.txt", 5);
    right.writeFile("both/a.txt", 7);    // same time, different size
    left.writeFile("newer.txt");
    right.writeFile("newer.txt");
    const QDateTime old = QDateTime::currentDateTime().addDays(-2);
    left.setModified("same.txt", old);
    right.setModified("same.txt", old);
    left.setModified("both/a.txt", old);
    right.setModified("both/a.txt", old);
    right.setModified("newer.txt", old);   // left is newer

    bool truncated = true;
    const auto entries = FolderCompare::compare(left.path(), right.path(), [] { return false; },
                                                FolderCompare::kLimit, &truncated);
    QVERIFY(!truncated);
    QHash<QString, QString> states;
    for (const auto &entry : entries)
        states.insert(entry.path, entry.state);
    QCOMPARE(states.value("same.txt"), QStringLiteral("same"));
    QCOMPARE(states.value("only-left.txt"), QStringLiteral("onlyLeft"));
    QCOMPARE(states.value("only-right.txt"), QStringLiteral("onlyRight"));
    QCOMPARE(states.value("sub"), QStringLiteral("onlyLeft"));
    QVERIFY(!states.contains("sub/deep.txt"));
    QVERIFY(!states.contains("both")); // a folder on both sides is its contents
    QCOMPARE(states.value("both/a.txt"), QStringLiteral("differ"));
    QCOMPARE(states.value("newer.txt"), QStringLiteral("newerLeft"));
    // Differences lead, identical files trail.
    QCOMPARE(entries.last().state, QStringLiteral("same"));

    // The QML-facing object does the same on a thread.
    FolderCompare compare;
    compare.setLeftPath(left.path());
    compare.setRightPath(right.path());
    compare.start();
    QVERIFY(compare.running());
    QTRY_VERIFY(!compare.running());
    QCOMPARE(compare.counts().value("onlyLeft").toInt(), 2);
    QCOMPARE(compare.entries().size(), entries.size());

    compare.setRightPath(left.path());
    compare.start();
    QVERIFY(!compare.error().isEmpty());
}

void TestProFeatures::diskUsageTotalsAndLevels()
{
    TempTree tree;
    tree.writeFile("big/a.bin", 200 * 1024);
    tree.writeFile("big/inner/b.bin", 100 * 1024);
    tree.writeFile("small.txt", 10);

    const auto nodes = DiskUsage::scan(tree.path(), [] { return false; }, {});
    QVERIFY(!nodes.isEmpty());
    QVERIFY(nodes.first().size >= 300 * 1024);
    QCOMPARE(nodes.first().files, 3);

    DiskUsage usage;
    usage.start(tree.path());
    QTRY_VERIFY(!usage.running());
    QVERIFY(usage.error().isEmpty());
    const QVariantList top = usage.items();
    QVERIFY(!top.isEmpty());
    QCOMPARE(top.first().toMap().value("name").toString(), QStringLiteral("big"));
    QVERIFY(top.first().toMap().value("isDir").toBool());

    usage.enter(tree.filePath("big"));
    QCOMPARE(usage.currentPath(), tree.filePath("big"));
    QCOMPARE(usage.items().first().toMap().value("name").toString(), QStringLiteral("a.bin"));
    usage.up();
    QCOMPARE(usage.currentPath(), tree.path());

    usage.start("smb://server/share");
    QVERIFY(!usage.error().isEmpty());
}

void TestProFeatures::duplicatesAreFoundAndLinked()
{
    TempTree tree;
    const QByteArray contents(100 * 1024, 'q');
    for (const char *name : { "one.bin", "copy/two.bin", "copy/three.bin" }) {
        QDir().mkpath(QFileInfo(tree.filePath(name)).absolutePath());
        QFile file(tree.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
    // Same size, different contents: not a duplicate.
    {
        QFile file(tree.filePath("other.bin"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(100 * 1024, 'z'));
    }
    tree.writeFile("tiny-a.txt", 3);
    tree.writeFile("tiny-b.txt", 3);

    const auto groups = DuplicateFinder::find(tree.path(), false, [] { return false; }, {});
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.first().paths.size(), 3); // the biggest waste first
    QCOMPARE(groups.first().size, qint64(100 * 1024));

    DuplicateFinder finder;
    const QString keep = groups.first().paths.first();
    const QStringList extras = groups.first().paths.mid(1);
    QCOMPARE(finder.replaceWithLinks(keep, extras), 2);
    QVERIFY(finder.linkErrors().isEmpty());
    struct stat a, b;
    QVERIFY(::stat(QFile::encodeName(keep).constData(), &a) == 0);
    QVERIFY(::stat(QFile::encodeName(extras.first()).constData(), &b) == 0);
    QCOMPARE(a.st_ino, b.st_ino);

    // Hard links to one file are not duplicates of each other.
    const auto after = DuplicateFinder::find(tree.path(), false, [] { return false; }, {});
    QCOMPARE(after.size(), 1);

    finder.start(tree.path());
    QTRY_VERIFY(!finder.running());
    QCOMPARE(finder.groups().size(), 1);
    QCOMPARE(finder.wastedBytes(), qint64(3));
}

void TestProFeatures::batchPermissionsSetClearAndScope()
{
    TempTree tree;
    const QString dir = tree.makeDir("folder");
    const QString file = tree.writeFile("folder/file.txt");
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    ::chmod(QFile::encodeName(dir).constData(), 0700);

    BatchPermissions::Request request;
    request.paths = { dir };
    request.setBits = 0044;   // group/others read
    request.clearBits = 0002;
    request.recursive = true;
    request.scope = "files";
    const auto outcome = BatchPermissions::run(request, [] { return false; });
    QCOMPARE(outcome.failed, 0);
    QCOMPARE(outcome.changed, 1);
    struct stat st;
    QVERIFY(::stat(QFile::encodeName(file).constData(), &st) == 0);
    QCOMPARE(int(st.st_mode & 0777), 0644);
    QVERIFY(::stat(QFile::encodeName(dir).constData(), &st) == 0);
    QCOMPARE(int(st.st_mode & 0777), 0700); // folders were out of scope

    request.scope = "folders";
    request.setBits = 0055;
    request.clearBits = 0;
    QCOMPARE(BatchPermissions::run(request, [] { return false; }).changed, 1);
    QVERIFY(::stat(QFile::encodeName(dir).constData(), &st) == 0);
    QCOMPARE(int(st.st_mode & 0777), 0755);

    request.owner = "no-such-user-omanta";
    QCOMPARE(BatchPermissions::run(request, [] { return false; }).failed, 1);

    BatchPermissions batch;
    QSignalSpy finished(&batch, &BatchPermissions::finished);
    batch.apply({ file }, 0, 0044, false, "all", QString(), QString());
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(batch.changedCount(), 1);
    QVERIFY(::stat(QFile::encodeName(file).constData(), &st) == 0);
    QCOMPARE(int(st.st_mode & 0777), 0600);
}

void TestProFeatures::shellCdResolves()
{
    TempTree tree;
    tree.makeDir("a/b");
    QCOMPARE(ShellSession::resolveCd("a", tree.path()), tree.filePath("a"));
    QCOMPARE(ShellSession::resolveCd("a/b/..", tree.path()), tree.filePath("a"));
    QCOMPARE(ShellSession::resolveCd("\"a/b\"", tree.path()), tree.filePath("a/b"));
    QCOMPARE(ShellSession::resolveCd("", tree.path()), QDir::homePath());
    QCOMPARE(ShellSession::resolveCd("~", tree.path()), QDir::homePath());
    QCOMPARE(ShellSession::resolveCd("missing", tree.path()), QString());
}

void TestProFeatures::shellRunsInTheFolder()
{
    TempTree tree;
    tree.writeFile("hello.txt");
    tree.makeDir("sub");
    ShellSession shell;
    shell.setDirectory(tree.path());
    shell.run("ls; echo done-$((40+2))");
    QTRY_VERIFY(!shell.running());
    QVERIFY(shell.output().contains("hello.txt"));
    QVERIFY(shell.output().contains("done-42"));
    QCOMPARE(shell.history(), QStringList{ "ls; echo done-$((40+2))" });

    shell.run("false");
    QTRY_VERIFY(!shell.running());
    QVERIFY(shell.output().contains("[exit 1]"));

    QSignalSpy cd(&shell, &ShellSession::changeDirectoryRequested);
    shell.run("cd sub");
    QCOMPARE(cd.count(), 1);
    QCOMPARE(cd.first().first().toString(), tree.filePath("sub"));
    shell.run("cd nowhere");
    QCOMPARE(cd.count(), 1);
    QVERIFY(shell.output().contains("no such folder"));

    shell.run("clear");
    QCOMPARE(shell.output(), QString());
}

void TestProFeatures::savedSearchesRoundTrip()
{
    SavedSearches searches;
    QSignalSpy changed(&searches, &SavedSearches::changed);
    const QString id = searches.save({ { "name", "Photos" }, { "folder", "/home" },
                                       { "query", "*.jpg" }, { "content", false } });
    QVERIFY(!id.isEmpty());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(searches.find(id).value("query").toString(), QStringLiteral("*.jpg"));
    // Not a local folder, or no name: refused.
    QVERIFY(searches.save({ { "name", "x" }, { "folder", "smb://a" } }).isEmpty());
    QVERIFY(searches.save({ { "name", " " }, { "folder", "/tmp" } }).isEmpty());
    // Same name replaces, keeping the id.
    QCOMPARE(searches.save({ { "name", "Photos" }, { "folder", "/home" }, { "query", "*.png" } }), id);
    SavedSearches reread;
    QCOMPARE(reread.searches().size(), 1);
    QCOMPARE(reread.find(id).value("query").toString(), QStringLiteral("*.png"));
    reread.remove(id);
    QVERIFY(SavedSearches().searches().isEmpty());
}

void TestProFeatures::pathCompletionCompletesFolders()
{
    TempTree tree;
    tree.makeDir("Documents");
    tree.makeDir("Downloads");
    tree.makeDir("Music");
    tree.makeDir(".config");
    tree.writeFile("Music.txt");
    Platform platform;

    QVariantMap one = platform.completePath("Mu", tree.path());
    QCOMPARE(one.value("text").toString(), QStringLiteral("Music/"));
    QVERIFY(one.value("matches").toStringList().isEmpty());

    QVariantMap two = platform.completePath(tree.path() + "/do", tree.path());
    QCOMPARE(two.value("text").toString(), tree.path() + "/Do");
    QCOMPARE(two.value("matches").toStringList(), (QStringList{ "Documents", "Downloads" }));

    QCOMPARE(platform.completePath(".co", tree.path()).value("text").toString(),
             QStringLiteral(".config/"));
    QCOMPARE(platform.completePath("zz", tree.path()).value("text").toString(), QStringLiteral("zz"));
    QCOMPARE(platform.completePath("smb://host/sh", tree.path()).value("text").toString(),
             QStringLiteral("smb://host/sh"));
    QCOMPARE(platform.completePath("~", tree.path()).value("text").toString(), QStringLiteral("~/"));
}

void TestProFeatures::verifyCatchesAMismatch()
{
    TempTree tree;
    const QString a = tree.writeFile("a.txt", 50);
    const QString b = tree.writeFile("b.txt", 50);
    const QString c = tree.writeFile("c.txt", 51);
    QCOMPARE(FileOperations::mismatchedCopies({ { a, b } }), QStringList());
    QCOMPARE(FileOperations::mismatchedCopies({ { a, c } }), QStringList{ c });
    QCOMPARE(FileOperations::mismatchedCopies({ { a, tree.filePath("gone") } }),
             QStringList{ tree.filePath("gone") });

    // End to end: a copy with verify on reports its check.
    FileOperations operations;
    operations.setVerifyCopies(true);
    QSignalSpy verified(&operations, &FileOperations::verificationFinished);
    const QString target = tree.makeDir("target");
    operations.copy({ a, c }, target);
    QTRY_COMPARE(verified.count(), 1);
    QCOMPARE(verified.first().at(0).toInt(), 2);
    QVERIFY(verified.first().at(1).toStringList().isEmpty());
}

void TestProFeatures::settingsMarksAndProColumns()
{
    Settings settings;
    QCOMPARE(settings.proFeatures(), false);
    QCOMPARE(settings.verifyCopies(), false);
    QCOMPARE(settings.transferSpeedLimit(), QStringLiteral("off"));
    QCOMPARE(settings.showGitStatus(), true);

    settings.setMark("a", "/tmp");
    settings.setMark("b", "/home");
    settings.setMark("1", "/nope");      // not a letter
    settings.setMark("c", "relative");   // not absolute
    QCOMPARE(Settings().mark("a"), QStringLiteral("/tmp"));
    QCOMPARE(Settings().marks().size(), 2);
    settings.setMark("a", QString());
    QCOMPARE(Settings().mark("a"), QString());

    settings.setProListColumns({ "album", "bogus", "dimensions", "album" });
    QCOMPARE(Settings().proListColumns(), (QStringList{ "dimensions", "album" }));
    settings.setTransferSpeedLimit("7");
    QCOMPARE(Settings().transferSpeedLimit(), QStringLiteral("off"));
}

QTEST_MAIN(TestProFeatures)
#include "tst_profeatures.moc"
