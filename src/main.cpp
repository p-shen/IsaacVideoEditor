#include "editor_controller.h"
#include "qml_registration.h"
#include "mlt_engine.h"
#include "theme.h"
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <mlt++/Mlt.h>
int main(int argc,char *argv[]) {
    QApplication app(argc,argv);
    app.setApplicationName("IsaacVideoEditor");
    app.setOrganizationName("IsaacVideoEditor");
    QQuickStyle::setStyle("Basic");
    applyAppTheme(app);
    app.setWindowIcon(QIcon(":/isaac-logo.png"));
    Media::initializeMlt();
    registerEditorQmlTypes();
    int result=1;
    {
        EditorController editor;
        editor.enableRecovery();
        QQmlApplicationEngine engine;
        setEditorQmlContext(engine,editor);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        if(!engine.rootObjects().isEmpty()) {
            const auto args=app.arguments();
            if(args.size()>1&&!args[1].startsWith('-')) editor.loadFrom(args[1]);
            result=app.exec();
        }
    }
    Mlt::Factory::close();
    return result;
}
