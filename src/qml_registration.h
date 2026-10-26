#pragma once
class QQmlEngine;
class EditorController;
void registerEditorQmlTypes();
void setEditorQmlContext(QQmlEngine &engine,EditorController &editor);
