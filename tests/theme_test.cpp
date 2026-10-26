#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QFontDatabase>
#include <QFontInfo>
#include <cmath>
#include <QPalette>
#include <QStyleHints>
#include <QtTest>

class ThemeTest : public QObject {
    Q_OBJECT
private slots:
    void followsSystemColorScheme();
    void bundledTypographyAvailable();
    void readableTextInBothAppearances();
};

void ThemeTest::followsSystemColorScheme() {
    auto &app = *qobject_cast<QApplication *>(QCoreApplication::instance());
    applyAppTheme(app);
    app.styleHints()->colorSchemeChanged(Qt::ColorScheme::Light);
    QCOMPARE(app.palette().color(QPalette::Window), QColor("#F2F2F4"));
    QVERIFY(!app.styleSheet().contains("@window@"));
    app.styleHints()->colorSchemeChanged(Qt::ColorScheme::Dark);
    QCOMPARE(app.palette().color(QPalette::Window), QColor("#18191D"));
}

void ThemeTest::bundledTypographyAvailable() {
    auto &app = *qobject_cast<QApplication *>(QCoreApplication::instance());
    const auto originalSize = app.font().pointSizeF();
    applyAppTheme(app);
    QVERIFY(QFontDatabase::families().contains("Plus Jakarta Sans"));
    QCOMPARE(QFontInfo(app.font()).family(), QString("Plus Jakarta Sans"));
    QCOMPARE(app.font().pointSizeF(), originalSize);
}

void ThemeTest::readableTextInBothAppearances() {
    const auto luminance = [](const QColor &c) {
        const auto linear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
    };
    auto &app = *qobject_cast<QApplication *>(QCoreApplication::instance());
    for (auto scheme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
        setAppColorScheme(app, scheme);
        const auto palette = app.palette();
        for (auto pair : {std::pair{QPalette::Text,QPalette::Base}, std::pair{QPalette::WindowText,QPalette::Window},
                          std::pair{QPalette::ButtonText,QPalette::Button}, std::pair{QPalette::HighlightedText,QPalette::Highlight},
                          std::pair{QPalette::PlaceholderText,QPalette::Base}, std::pair{QPalette::PlaceholderText,QPalette::AlternateBase}}) {
            const auto fg = luminance(palette.color(pair.first));
            const auto bg = luminance(palette.color(pair.second));
            QVERIFY2((std::max(fg,bg)+0.05)/(std::min(fg,bg)+0.05) >= 4.5, "Normal and secondary text need 4.5:1 contrast in both appearances.");
        }
        const auto edge = luminance(palette.color(QPalette::Shadow));
        const auto field = luminance(palette.color(QPalette::AlternateBase));
        QVERIFY2((std::max(edge,field)+0.05)/(std::min(edge,field)+0.05) >= 3.0, "Enabled field boundaries need 3:1 contrast.");
    }
}

QTEST_MAIN(ThemeTest)
#include "theme_test.moc"
