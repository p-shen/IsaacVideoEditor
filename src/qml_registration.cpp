#include "qml_registration.h"
#include "editor_controller.h"
#include "media_cache.h"
#include "preview_item.h"
#include "quick_timeline.h"
#include "update_checker.h"
#include <QQmlContext>
#include <QGuiApplication>
#include <QFont>
#include <QQmlEngine>
#include <qqml.h>
void registerEditorQmlTypes() {
    static bool registered=false;
    if(registered) return; registered=true;
    qmlRegisterType<VideoPreview>("Isaac",1,0,"VideoPreview");
    qmlRegisterType<CachedFrame>("Isaac",1,0,"CachedFrame");
    qmlRegisterType<QuickTimeline>("Isaac",1,0,"QuickTimeline");
    qmlRegisterType<UpdateChecker>("Isaac",1,0,"UpdateChecker");
    qmlRegisterUncreatableType<EditorController>("Isaac",1,0,"EditorController","Created by the application");
    qmlRegisterUncreatableType<MediaCache>("Isaac",1,0,"MediaCache","Created by the editing controller");
}
void setEditorQmlContext(QQmlEngine &engine,EditorController &editor) {
    QQmlEngine::setObjectOwnership(&editor,QQmlEngine::CppOwnership);
    engine.rootContext()->setContextProperty("applicationFont",QGuiApplication::font());
    engine.rootContext()->setContextProperty("editor",&editor);
    engine.rootContext()->setContextProperty("derivedMedia",editor.mediaCache());
}
