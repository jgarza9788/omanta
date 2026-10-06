#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>

// Where the pro features keep their small state files: next to the other
// stores in ~/.config/omanta, or under OMANTA_PRO_DIR (the tests point it at
// a temporary directory so a run never touches the real home).
namespace ProPaths {

inline QString file(const QString &name)
{
    const QString override = qEnvironmentVariable("OMANTA_PRO_DIR");
    const QString dir = override.isEmpty()
        ? QDir::homePath() + QStringLiteral("/.config/omanta") : override;
    return dir + QLatin1Char('/') + name;
}

// Atomic, private write: a crash mid-save leaves the old file, and nobody
// else on the machine reads where you have been.
inline bool write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(data);
    return file.commit();
}

} // namespace ProPaths
