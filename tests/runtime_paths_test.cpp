#include "runtime_paths.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class RuntimePathsTest : public QObject {
    Q_OBJECT
private slots:
    void developmentBuildKeepsSystemDiscovery() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto paths = bundledRuntimePaths(directory.path());
        QVERIFY(paths.executables.isEmpty());
        QVERIFY(paths.mltRepository.isEmpty());
        QVERIFY(paths.mltData.isEmpty());
    }

    void discoversBundles_data() {
        QTest::addColumn<QString>("binaryDirectory");
        QTest::addColumn<QString>("repository");
        QTest::addColumn<QString>("data");
        QTest::newRow("mac") << "My Editor.app/Contents/MacOS"
            << "My Editor.app/Contents/PlugIns/mlt-7" << "My Editor.app/Contents/Resources/mlt-7";
        QTest::newRow("linux") << "My Editor.AppDir/usr/bin"
            << "My Editor.AppDir/usr/lib/mlt-7" << "My Editor.AppDir/usr/share/mlt-7";
        QTest::newRow("windows") << "My Editor"
            << "My Editor/lib/mlt-7" << "My Editor/share/mlt-7";
    }

    void discoversBundles() {
        QFETCH(QString, binaryDirectory);
        QFETCH(QString, repository);
        QFETCH(QString, data);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root(directory.path());
        QVERIFY(root.mkpath(binaryDirectory));
        QVERIFY(root.mkpath(repository));
        QVERIFY(root.mkpath(data));
        const auto paths = bundledRuntimePaths(root.filePath(binaryDirectory));
        QCOMPARE(paths.executables, root.filePath(binaryDirectory));
        QCOMPARE(paths.mltRepository, root.filePath(repository));
        QCOMPARE(paths.mltData, root.filePath(data));
    }

    void incompleteBundleKeepsSystemDiscovery() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root(directory.path());
        QVERIFY(root.mkpath("lib/mlt-7"));
        QVERIFY(bundledRuntimePaths(directory.path()).mltRepository.isEmpty());
    }
};

QTEST_GUILESS_MAIN(RuntimePathsTest)
#include "runtime_paths_test.moc"
