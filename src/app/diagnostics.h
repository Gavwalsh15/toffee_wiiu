#pragma once
class QGuiApplication;
class QQmlApplicationEngine;

namespace app {
int runWithDiagnostics(QGuiApplication& application, QQmlApplicationEngine& engine);
} // namespace app
