// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "../src/launcher/qt_env_setup.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

class EnvVarGuard {
 public:
  explicit EnvVarGuard(const char* name)
      : m_name(name), m_hadValue(qEnvironmentVariableIsSet(name)), m_oldValue(qgetenv(name)) {}

  ~EnvVarGuard() {
    if (m_hadValue) {
      qputenv(m_name, m_oldValue);
    } else {
      qunsetenv(m_name);
    }
  }

  EnvVarGuard(const EnvVarGuard&) = delete;
  EnvVarGuard& operator=(const EnvVarGuard&) = delete;

 private:
  const char* m_name;
  bool m_hadValue;
  QByteArray m_oldValue;
};

bool createMockQtPrefix(const QString& prefixPath) {
  QDir root(prefixPath);
#ifdef Q_OS_WIN
  if (!root.mkpath(QStringLiteral("bin")))
    return false;
  QFile coreLib(root.filePath(QStringLiteral("bin/Qt6Core.dll")));
#elif defined(Q_OS_MACOS)
  if (!root.mkpath(QStringLiteral("lib")))
    return false;
  QFile coreLib(root.filePath(QStringLiteral("lib/libQt6Core.dylib")));
#else
  if (!root.mkpath(QStringLiteral("lib")))
    return false;
  QFile coreLib(root.filePath(QStringLiteral("lib/libQt6Core.so.6")));
#endif
  if (!coreLib.open(QIODevice::WriteOnly))
    return false;
  coreLib.close();

  return root.mkpath(QStringLiteral("plugins/platforms"));
}

}  // namespace

class TestQtEnvSetup : public QObject {
  Q_OBJECT

 private slots:
  void multiEntryQtPluginPathFindsValidEntry();
  void multiEntryQtPluginPathRejectsAllInvalid();
#ifndef Q_OS_WIN
  void emptyLibraryPathHasNoTrailingColon();
  void nonEmptyLibraryPathPrependsWithColon();
#endif
  void pathsWithSpacesAndUnicode();
  void explicitQtDirResolvesFromSubdirectory();
};

void TestQtEnvSetup::multiEntryQtPluginPathFindsValidEntry() {
  EnvVarGuard pluginGuard("QT_PLUGIN_PATH");

  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  QDir root(tempDir.path());
  QVERIFY(root.mkpath(QStringLiteral("empty_plugins")));
  QVERIFY(root.mkpath(QStringLiteral("valid_plugins/platforms")));

  const QString multiPath = root.filePath(QStringLiteral("empty_plugins")) + QDir::listSeparator() +
                            root.filePath(QStringLiteral("valid_plugins"));
  qputenv("QT_PLUGIN_PATH", multiPath.toLocal8Bit());

  const qtPilot::QtEnvironmentResult result =
      qtPilot::ensureQtEnvironment(QString(), QStringLiteral("/nonexistent/app"), true);

  QCOMPARE(result.source, QStringLiteral("existing environment variables"));
  QCOMPARE(result.applied, false);
}

void TestQtEnvSetup::multiEntryQtPluginPathRejectsAllInvalid() {
  EnvVarGuard pluginGuard("QT_PLUGIN_PATH");
  EnvVarGuard pathGuard("PATH");
#ifdef Q_OS_MACOS
  EnvVarGuard libGuard("DYLD_LIBRARY_PATH");
#elif !defined(Q_OS_WIN)
  EnvVarGuard libGuard("LD_LIBRARY_PATH");
#endif

  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  QDir root(tempDir.path());
  QVERIFY(root.mkpath(QStringLiteral("bad1")));
  QVERIFY(root.mkpath(QStringLiteral("bad2")));

  const QString multiPath = root.filePath(QStringLiteral("bad1")) + QDir::listSeparator() +
                            root.filePath(QStringLiteral("bad2"));
  qputenv("QT_PLUGIN_PATH", multiPath.toLocal8Bit());

  const qtPilot::QtEnvironmentResult result =
      qtPilot::ensureQtEnvironment(QString(), QStringLiteral("/nonexistent/app"), true);

  QVERIFY(result.source != QStringLiteral("existing environment variables"));
}

#ifndef Q_OS_WIN
void TestQtEnvSetup::emptyLibraryPathHasNoTrailingColon() {
  EnvVarGuard pluginGuard("QT_PLUGIN_PATH");
#ifdef Q_OS_MACOS
  const char* libVar = "DYLD_LIBRARY_PATH";
#else
  const char* libVar = "LD_LIBRARY_PATH";
#endif
  EnvVarGuard libGuard(libVar);

  qunsetenv("QT_PLUGIN_PATH");
  qunsetenv(libVar);

  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  QVERIFY(createMockQtPrefix(tempDir.path()));

  const qtPilot::QtEnvironmentResult result =
      qtPilot::ensureQtEnvironment(tempDir.path(), QStringLiteral("/nonexistent/app"), true);

  QCOMPARE(result.applied, true);
  const QString updatedLibPath = QString::fromLocal8Bit(qgetenv(libVar));
  QCOMPARE(updatedLibPath, result.binPath);
  QVERIFY(!updatedLibPath.endsWith(QLatin1Char(':')));
}

void TestQtEnvSetup::nonEmptyLibraryPathPrependsWithColon() {
  EnvVarGuard pluginGuard("QT_PLUGIN_PATH");
#ifdef Q_OS_MACOS
  const char* libVar = "DYLD_LIBRARY_PATH";
#else
  const char* libVar = "LD_LIBRARY_PATH";
#endif
  EnvVarGuard libGuard(libVar);

  qunsetenv("QT_PLUGIN_PATH");
  qputenv(libVar, QByteArray("/existing/custom/lib"));

  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  QVERIFY(createMockQtPrefix(tempDir.path()));

  const qtPilot::QtEnvironmentResult result =
      qtPilot::ensureQtEnvironment(tempDir.path(), QStringLiteral("/nonexistent/app"), true);

  QCOMPARE(result.applied, true);
  const QString updatedLibPath = QString::fromLocal8Bit(qgetenv(libVar));
  QCOMPARE(updatedLibPath, result.binPath + QStringLiteral(":/existing/custom/lib"));
}
#endif

void TestQtEnvSetup::pathsWithSpacesAndUnicode() {
  EnvVarGuard pluginGuard("QT_PLUGIN_PATH");
  EnvVarGuard pathGuard("PATH");
#ifdef Q_OS_MACOS
  EnvVarGuard libGuard("DYLD_LIBRARY_PATH");
  qunsetenv("DYLD_LIBRARY_PATH");
#elif !defined(Q_OS_WIN)
  EnvVarGuard libGuard("LD_LIBRARY_PATH");
  qunsetenv("LD_LIBRARY_PATH");
#endif

  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  const QString unicodePrefix =
      QDir(tempDir.path()).filePath(QString::fromStdU16String(u"Qt 6.12 \u6D4B\u8BD5/prefix"));
  QVERIFY(createMockQtPrefix(unicodePrefix));

  qunsetenv("QT_PLUGIN_PATH");
  const qtPilot::QtEnvironmentResult result =
      qtPilot::ensureQtEnvironment(unicodePrefix, QStringLiteral("/nonexistent/app"), true);

  QCOMPARE(result.applied, true);
  QCOMPARE(result.qtDir, QDir(unicodePrefix).absolutePath());
  QCOMPARE(qEnvironmentVariable("QT_PLUGIN_PATH"), QDir::toNativeSeparators(result.pluginsPath));

  // Also verify multi-entry QT_PLUGIN_PATH works with spaces and Unicode.
  QDir root(tempDir.path());
  QVERIFY(root.mkpath(QStringLiteral("other dir")));
  const QString multiUnicode =
      root.filePath(QStringLiteral("other dir")) + QDir::listSeparator() + result.pluginsPath;
#ifdef Q_OS_WIN
  QVERIFY(
      SetEnvironmentVariableW(L"QT_PLUGIN_PATH", reinterpret_cast<LPCWSTR>(multiUnicode.utf16())));
#else
  qputenv("QT_PLUGIN_PATH", multiUnicode.toLocal8Bit());
#endif

  const qtPilot::QtEnvironmentResult envResult =
      qtPilot::ensureQtEnvironment(QString(), QStringLiteral("/nonexistent/app"), true);
  QCOMPARE(envResult.source, QStringLiteral("existing environment variables"));
  QCOMPARE(envResult.applied, false);
}

void TestQtEnvSetup::explicitQtDirResolvesFromSubdirectory() {
  EnvVarGuard pluginGuard("QT_PLUGIN_PATH");
  EnvVarGuard pathGuard("PATH");
#ifdef Q_OS_MACOS
  EnvVarGuard libGuard("DYLD_LIBRARY_PATH");
#elif !defined(Q_OS_WIN)
  EnvVarGuard libGuard("LD_LIBRARY_PATH");
#endif

  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  QVERIFY(createMockQtPrefix(tempDir.path()));

  const QString pluginsSubdir = QDir(tempDir.path()).filePath(QStringLiteral("plugins"));
  const qtPilot::QtEnvironmentResult result =
      qtPilot::ensureQtEnvironment(pluginsSubdir, QStringLiteral("/nonexistent/app"), true);

  QCOMPARE(result.applied, true);
  QCOMPARE(result.qtDir, QDir(tempDir.path()).absolutePath());
}

QTEST_GUILESS_MAIN(TestQtEnvSetup)
#include "test_qt_env_setup.moc"
