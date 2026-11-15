#pragma once
#include <QDir>
#include <QFile>
#include <QString>
#include <windows.h>
#include <shellapi.h>

inline bool unpackResource(const QString& resource, const QString& path)
{
    QFile::setPermissions(path, QFile::WriteOwner);
    QFile::remove(path);
    if (!QFile::copy(resource, path))
        return false;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return true;
}

// GetLastError() is ERROR_CANCELLED if the user declined.
inline HANDLE runElevated(const QString& program, const QString& parameters = {})
{
    const QString file = QDir::toNativeSeparators(program);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = reinterpret_cast<LPCWSTR>(file.utf16());
    info.lpParameters = parameters.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(parameters.utf16());
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) ? info.hProcess : nullptr;
}
