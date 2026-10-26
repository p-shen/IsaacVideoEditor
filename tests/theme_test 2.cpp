#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleHints>
#include <QtTest>

class ThemeTest : public QObject {
    Q_OBJECT
private slots:
    void followsSystemColorScheme();
};

void ThemeTest::followsSystemColorScheme() {
    auto &app = *qobject_cast<QApplication *>(QCoreApplication::instance());
    applyAppTheme(app);
    app.styleHints()->colorSchemeChanged(Qt::ColorScheme::Light);
    QCOMPARE(app.palette().color(QPalette::Window), QColor("#F5F7FA"));
    QVERIFY(!app.styleSheet().contains("@window@"));
    app.styleHints()->colorSchemeChanged(Qt::ColorScheme::Dark);
    QCOMPARE(app.palette().color(QPalette::Window), QColor("#18191C"));
}

QTEST_MAIN(ThemeTest)
#include "theme_test.moc"
