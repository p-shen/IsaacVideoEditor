#include "editor_controller.h"
#include "qml_registration.h"
#include "mlt_engine.h"
#include "theme.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QAccessible>
#include <QtTest>

class InspectorUxTest : public QObject {
    Q_OBJECT
    QString fixture() const { return QDir::current().filePath("tests/fixtures/seek.mp4"); }
    QQuickWindow *load(QQmlApplicationEngine &engine,EditorController &editor) {
        setEditorQmlContext(engine,editor);
        engine.load(QUrl::fromLocalFile(QDir::current().filePath("qml/Main.qml")));
        auto *window=engine.rootObjects().isEmpty()?nullptr:qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(window) { window->requestActivate(); if(!QTest::qWaitForWindowExposed(window,2000)) return nullptr; }
        return window;
    }
    bool section(QQuickWindow *window,int index) {
        return QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,index),Q_ARG(QVariant,false));
    }
    QQuickItem *visualNamed(QQuickItem *item,const QString &name) {
        if(item->objectName()==name) return item;
        for(auto *child:item->childItems()) if(auto *found=visualNamed(child,name)) return found;
        return nullptr;
    }
    QQuickItem *named(QQuickWindow *window,const char *name) {
        return visualNamed(window->contentItem(),QString::fromLatin1(name));
    }
    bool fullyVisible(QQuickItem *item) {
        if(!item||!item->isVisible()||item->width()<1||item->height()<1) return false;
        bool attached=false; for(auto *parent=item;parent;parent=parent->parentItem()) if(parent==item->window()->contentItem()) attached=true;
        if(!attached) return false;
        const QRectF bounds(0,0,item->width(),item->height());
        if(!QRectF(0,0,item->window()->width(),item->window()->height()).adjusted(-1,-1,1,1).contains(item->mapRectToScene(bounds))) return false;
        for(auto *parent=item->parentItem();parent;parent=parent->parentItem()) {
            const bool panelBounds=parent->property("padding").isValid()&&parent->property("background").isValid();
            if((parent->clip()||panelBounds)&&!QRectF(0,0,parent->width(),parent->height()).adjusted(-1,-1,1,1).contains(item->mapRectToItem(parent,bounds))) return false;
        }
        return true;
    }
    bool visibleText(QQuickWindow *window,const QString &text) {
        for(auto *item:window->findChildren<QQuickItem*>()) if(item->isVisible()&&item->property("text").toString()==text) return true;
        return false;
    }
    QQuickItem *flickable(QQuickItem *child) {
        for(auto *item=child->parentItem();item;item=item->parentItem()) if(item->property("contentY").isValid()) return item;
        return nullptr;
    }
private slots:
    void initTestCase() {
        qputenv("SDL_AUDIODRIVER","dummy"); Media::initializeMlt(); QQuickStyle::setStyle("Basic");
        applyAppTheme(*qobject_cast<QApplication*>(qApp)); registerEditorQmlTypes();
    }
    void init() { QTest::failOnWarning(QRegularExpression(".*")); }
    void sectionsKeepIndependentScrollPositions() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); editor.setMusicPath(fixture()); window->resize(1320,560);
        QVERIFY(section(window,1)); auto *gain=named(window,"audioGainField"); QVERIFY(gain);
        auto *scroll=flickable(gain); QVERIFY(scroll); QTest::qWait(50);
        if(auto *details=named(window,"audioTimingDetails")) details->setProperty("expanded",true);
        QTest::qWait(50); const double position=std::min(100.0,scroll->property("contentHeight").toDouble()-scroll->height());
        QVERIFY(position>0); QVERIFY(scroll->setProperty("contentY",position));
        QVERIFY(section(window,3)); auto *textScroll=flickable(named(window,"textContentField")); QVERIFY(textScroll);
        QTRY_COMPARE(textScroll->property("contentY").toDouble(),0.0);
        QVERIFY(section(window,1)); QTRY_COMPARE(scroll->property("contentY").toDouble(),position);
    }
    void trimUsesIntegratedFrameStepsAndEscape() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); editor.setTrimStart(.25); QVERIFY(section(window,0));
        auto *start=named(window,"trimStartField"); QVERIFY(start); start->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_Up); QCOMPARE(qRound(editor.project().clips[0].inSeconds*60),16);
        const auto history=editor.undoText(); start->setProperty("text","1.5"); QTest::keyClick(window,Qt::Key_Escape);
        QCOMPARE(qRound(editor.project().clips[0].inSeconds*60),16); QCOMPARE(editor.undoText(),history);
    }
    void audioShowsOnlyItsSelectedSource() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); editor.setMusicPath(fixture()); editor.selectAudio(2); QVERIFY(section(window,1));
        QCoreApplication::processEvents();
        QVERIFY2(!visibleText(window,"No narration"),"Music controls must not be placed beneath an unrelated narration form");
        auto *gain=named(window,"audioGainField"); QVERIFY(gain); gain->forceActiveFocus(); gain->setProperty("text","-18.0");
        QVERIFY(QMetaObject::invokeMethod(gain,"editingFinished")); QCOMPARE(editor.project().musicDb,-18.0); QCOMPARE(editor.project().voiceAudio.gainDb,0.0);
        auto *timing=named(window,"audioTimingDetails"); QVERIFY(timing); QVERIFY(!timing->property("expanded").toBool());
        auto *fade=named(window,"audioFadeInField"); QVERIFY(fade); QVERIFY(!fade->isVisible());
        timing->setProperty("expanded",true); QTRY_VERIFY(fade->isVisible()); fade->forceActiveFocus(); fade->setProperty("text","0.5");
        QVERIFY(QMetaObject::invokeMethod(fade,"editingFinished")); QCOMPARE(editor.project().musicFadeInFrames,30);
    }
    void transitionPresetRequiresAnExplicitCut() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture(),fixture()})); QVERIFY(section(window,2));
        QQuickItem *preset=named(window,"transitionPresetButton");
        if(!preset) for(auto *item:window->findChildren<QQuickItem*>()) if(item->property("text").toString()=="Dissolve · 30f") preset=item;
        QVERIFY(preset); QVERIFY2(!preset->isEnabled(),"A transition preset must not silently target the first cut");
        const auto before=editor.project().transitionFramesBefore(1); QMetaObject::invokeMethod(preset,"clicked");
        QCOMPARE(editor.project().transitionFramesBefore(1),before);
        editor.selectTransition(1); QTRY_VERIFY(preset->isEnabled());
        auto *duration=named(window,"transitionDurationField"); QVERIFY(duration); duration->forceActiveFocus(); duration->setProperty("text","0.25");
        QVERIFY(QMetaObject::invokeMethod(duration,"editingFinished")); QCOMPARE(editor.project().transitionFramesBefore(1),15);
        editor.undo(); QCOMPARE(editor.project().transitionFramesBefore(1),before);
    }
    void transformStartsWithBasicControlsAndOptionalPrecision() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); QVERIFY(section(window,4)); QCoreApplication::processEvents();
        QVERIFY2(!visibleText(window,"Previous"),"Animation navigation should be hidden until requested");
        auto *width=named(window,"canvasWidthField"); QVERIFY(width); QVERIFY(!width->isVisible());
        auto *scale=named(window,"transformScaleField"); QVERIFY(scale); QCOMPARE(scale->property("suffix").toString(),QString("%"));
        scale->forceActiveFocus(); scale->setProperty("text","125"); QVERIFY(QMetaObject::invokeMethod(scale,"editingFinished"));
        QCOMPARE(editor.project().clips[0].transform.scale.value,1.25);
        auto *animation=named(window,"transformAnimationDetails"); QVERIFY(animation); animation->setProperty("expanded",true);
        QTRY_VERIFY(animation->property("expanded").toBool());
    }
    void textCanBeCreatedInItsEmptyInspector() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); QVERIFY(section(window,3));
        auto *add=named(window,"textEmptyAddButton"); QVERIFY2(add,"Empty Text needs an Add text action"); QVERIFY(add->isVisible());
        QVERIFY(QMetaObject::invokeMethod(add,"clicked")); QCOMPARE(editor.project().textOverlays.size(),1);
        auto *placement=named(window,"textPlacementDetails"); QVERIFY(placement); QVERIFY(!placement->property("expanded").toBool());
        auto *attachment=named(window,"textAttachmentDetails"); QVERIFY(attachment); QVERIFY(!attachment->property("expanded").toBool());
    }
    void emptyCaptionsDoNotShowInactiveEditingActions() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); QVERIFY(section(window,5)); QCoreApplication::processEvents();
        QVERIFY2(!visibleText(window,"Edit caption"),"Empty captions should offer creation instead of disabled editing controls");
        auto *add=named(window,"addCaptionButton"); QVERIFY(add); QVERIFY(add->isVisible());
        QVERIFY(QMetaObject::invokeMethod(add,"clicked")); QCOMPARE(editor.project().captions.size(),1);
        auto *duration=named(window,"captionDurationField"); QVERIFY(duration); QCOMPARE(duration->property("suffix").toString(),QString("s"));
        duration->forceActiveFocus(); duration->setProperty("text","1.5"); QVERIFY(QMetaObject::invokeMethod(duration,"editingFinished"));
        QCOMPARE(editor.project().captions[0].durationFrames,90);
    }
    void staleClipEditCannotCommitToAnotherClip() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture(),fixture()})); editor.selectClip(0); QVERIFY(section(window,0));
        auto *start=named(window,"trimStartField"); QVERIFY(start); start->forceActiveFocus(); start->setProperty("text","0.5");
        editor.selectClip(1); start->setProperty("text","0.5"); QVERIFY(QMetaObject::invokeMethod(start,"editingFinished"));
        QCOMPARE(editor.project().clips[0].inSeconds,0.0); QCOMPARE(editor.project().clips[1].inSeconds,0.0);
    }
    void staleAudioEditCannotCommitToAnotherSource() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); editor.setMusicPath(fixture()); editor.setVoicePath(fixture()); editor.selectAudio(2); QVERIFY(section(window,1));
        auto *gain=named(window,"audioGainField"); QVERIFY(gain); const auto music=editor.project().musicDb;
        gain->forceActiveFocus(); gain->setProperty("text","-7"); editor.selectAudio(1); gain->setProperty("text","-7");
        QVERIFY(QMetaObject::invokeMethod(gain,"editingFinished")); QCOMPARE(editor.project().musicDb,music); QCOMPARE(editor.project().voiceAudio.gainDb,0.0);
    }
    void bulkTransitionEditsWaitForApplyAndCanUndo() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture(),fixture(),fixture()})); editor.setCrossfade(.5); editor.setTransition(1,"dissolve",12); editor.setTransition(2,"cut",0); QVERIFY(section(window,2));
        QVERIFY(editor.project().clips[2].hardCutBefore);
        auto *group=named(window,"transitionBulkDetails"); QVERIFY(group); group->setProperty("expanded",true);
        auto *duration=named(window,"transitionBulkDurationField"); QVERIFY(duration); duration->forceActiveFocus(); duration->setProperty("text","0.25");
        QVERIFY(QMetaObject::invokeMethod(duration,"editingFinished")); QCOMPARE(editor.project().clips[1].transitionFrames,12); QCOMPARE(editor.project().clips[2].transitionFrames,0);
        auto *apply=named(window,"transitionBulkApplyButton"); QVERIFY(apply); QVERIFY(QMetaObject::invokeMethod(apply,"clicked"));
        QCOMPARE(editor.project().crossfadeFrames,15);
        QCOMPARE(editor.project().transitionFramesBefore(1),15); QCOMPARE(editor.project().transitionFramesBefore(2),15);
        QVERIFY(editor.notice().contains("2 existing cuts"));QVERIFY(editor.property("noticeCanUndo").toBool());
        auto *undo=named(window,"noticeUndoButton");QVERIFY(undo);QTRY_VERIFY(undo->isVisible());
        QVERIFY(undo->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(undo,"clicked"));
        QCOMPARE(editor.project().clips[1].transitionFrames,12); QCOMPARE(editor.project().clips[2].transitionFrames,0);
        QCOMPARE(editor.project().transitionFramesBefore(1),12); QCOMPARE(editor.project().transitionFramesBefore(2),0);
        QVERIFY(!editor.property("noticeCanUndo").toBool());QVERIFY(editor.notice().isEmpty());
        editor.redo(); QCOMPARE(editor.project().transitionFramesBefore(1),15); QCOMPARE(editor.project().transitionFramesBefore(2),15);
    }
    void shortenedCrossfadesShowActualDuration() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);
        QVERIFY(editor.importFiles({fixture(),fixture(),fixture(),fixture(),fixture()}));QVERIFY(editor.trimClip(1,0,.5));
        editor.setCrossfade(.5);editor.selectTransition(1);QVERIFY(section(window,2));
        auto *duration=named(window,"transitionDurationField");QVERIFY(duration);QCOMPARE(duration->property("text").toString(),QString("0.233"));
        auto *note=named(window,"transitionShorteningNote");QVERIFY(note);QTRY_VERIFY(note->isVisible());
        QVERIFY(note->property("text").toString().contains("0.5"));QVERIFY(note->property("text").toString().contains("0.233"));
        editor.selectTransition(3);QCOMPARE(duration->property("text").toString(),QString("0.500"));QVERIFY(!note->isVisible());
        editor.selectTransition(1);
        auto *group=named(window,"transitionBulkDetails");QVERIFY(group);group->setProperty("expanded",true);
        auto *apply=named(window,"transitionBulkApplyButton");QVERIFY(apply);
        const auto captures=qEnvironmentVariable("ISAAC_CROSSFADE_SCREENSHOTS");
        if(!captures.isEmpty()) {
            QDir().mkpath(captures);
            for(bool dark:{false,true}) {
                setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);
                window->resize(1320,860);QTest::qWait(100);
                QVERIFY(window->grabWindow().save(captures+(dark?"/crossfades-dark.png":"/crossfades-light.png")));
            }
        }
        window->resize(760,560);auto font=qApp->font();font.setPointSize(24);window->setProperty("font",font);
        QTest::qWait(50);QVERIFY(section(window,2));
        group=named(window,"transitionBulkDetails");QVERIFY(group);group->setProperty("expanded",true);
        apply=named(window,"transitionBulkApplyButton");QVERIFY(apply);
        apply->forceActiveFocus();QTest::qWait(150);
        if(!captures.isEmpty())QVERIFY(window->grabWindow().save(captures+"/crossfades-narrow.png"));
        QTRY_VERIFY(fullyVisible(apply));
        QVERIFY(editor.trimClip(1,0,1./60));editor.selectTransition(1);QVERIFY(section(window,2));
        duration=named(window,"transitionDurationField");note=named(window,"transitionShorteningNote");QVERIFY(duration);QVERIFY(note);
        QCOMPARE(duration->property("text").toString(),QString("0.000"));QVERIFY(note->isVisible());
        QVERIFY(note->property("text").toString().contains("0.000 s"));
    }
    void canvasPreviewKeepsActionsOutsideCollapsedSettings() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); QVERIFY(section(window,4));
        auto *group=named(window,"canvasDetails"); QVERIFY(group); group->setProperty("expanded",true);
        auto *choice=named(window,"canvasPresetChoice"); QVERIFY(choice); choice->setProperty("currentIndex",4); QVERIFY(QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,4)));
        auto *width=named(window,"canvasWidthField"); auto *height=named(window,"canvasHeightField"); QVERIFY(width); QVERIFY(height); QTRY_VERIFY(width->isVisible());
        width->setProperty("text","1080"); height->setProperty("text","1920");
        auto *preview=named(window,"previewCanvasButton"); QVERIFY(preview); QTRY_VERIFY(preview->isEnabled()); QVERIFY(QMetaObject::invokeMethod(preview,"clicked"));
        QVERIFY(editor.property("visualPreviewActive").toBool()); group->setProperty("expanded",false);
        auto *apply=named(window,"applyCanvasButton"); auto *cancel=named(window,"cancelCanvasButton"); QVERIFY(apply); QVERIFY(cancel);
        QVERIFY(apply->isVisible()); QVERIFY(cancel->isVisible()); QVERIFY(QMetaObject::invokeMethod(cancel,"clicked"));
        QVERIFY(!editor.property("visualPreviewActive").toBool()); QCOMPARE(editor.project().canvasWidth,0); QCOMPARE(editor.project().canvasHeight,0);
    }
    void allPanelsRemainReachableWithLargeText() {
        EditorController editor; QQmlApplicationEngine engine; QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        auto *window=load(engine,editor); QVERIFY(window); QVERIFY(editor.importFiles({fixture(),fixture()}));
        editor.setMusicPath(fixture()); editor.setVoicePath(fixture()); editor.addText(); editor.updateText(0,{{"text","A day at the park"}}); editor.addCaption();
        const auto directory=qEnvironmentVariable("ISAAC_INSPECTOR_SCREENSHOTS"); if(!directory.isEmpty()) QDir().mkpath(directory);
        const QStringList names={"clip","audio","transitions","text","transform","captions"};
        const auto select=[&](int index){ if(index==0||index==4)editor.selectClip(0);else if(index==1)editor.selectAudio(2);else if(index==2)editor.selectTransition(1);else if(index==3)editor.selectText(0);else editor.selectCaption(0); return section(window,index); };
        if(!directory.isEmpty()) for(bool dark:{false,true}) {
            setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);
            for(int index=0;index<6;++index) { QVERIFY(select(index)); window->requestUpdate(); QTest::qWait(80); QVERIFY(window->grabWindow().save(directory+"/"+names[index]+(dark?"-dark.png":"-light.png"))); }
        }
        window->resize(760,560); auto font=qApp->font(); font.setPointSize(24); window->setProperty("font",font); QTest::qWait(50);
        const char *controls[]={"trimStartField","audioSourceChoice","transitionDurationField","textContentField","transformScaleField","captionStartField"};
        for(int index=0;index<6;++index) {
            QVERIFY(select(index)); auto *control=named(window,controls[index]); QVERIFY(control); control->forceActiveFocus();
            QTest::qWait(100);
            QTRY_VERIFY2(fullyVisible(control),qPrintable(names[index]+" primary control is clipped at large text"));
            if(!directory.isEmpty()) {
                window->requestUpdate(); QTest::qWait(80); window->grabWindow();
                window->requestUpdate(); QTest::qWait(100);
                QVERIFY(window->grabWindow().save(directory+"/"+names[index]+"-large.png")); QVERIFY(fullyVisible(control));
            }
        }
        QVERIFY(select(3)); auto *toggle=named(window,"textPlacementDetailsToggle"); QVERIFY(toggle); toggle->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Space);
        auto *placement=named(window,"textPlacementDetails"); QVERIFY(placement); QTRY_VERIFY(placement->property("expanded").toBool());
        auto *position=named(window,"textPositionXField"); QVERIFY(position); position->forceActiveFocus(); QTRY_VERIFY(fullyVisible(position));
        QCOMPARE(warnings.count(),0);
    }
    void focusingPositionFieldsPreservesPreciseAndBatchValues() {
        for(bool caption:{false,true}) {
            EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window); QVERIFY(editor.importFiles({fixture()}));
            if(caption) {
                editor.addCaption(); editor.styleCaptions({{"x",.234567},{"y",.345678},{"fontSize",54}});
                editor.addCaption(); editor.styleCaptions({{"x",.812345},{"y",.723456},{"fontSize",84}});
                editor.selectCaption(0); editor.selectCaption(1,Qt::ControlModifier); QVERIFY(section(window,5));
                named(window,"captionAppearanceDetails")->setProperty("expanded",true); named(window,"captionPlacementDetails")->setProperty("expanded",true);
            } else {
                editor.addText(); editor.updateText(0,{{"x",.234567},{"y",.345678},{"fontSize",54}}); QVERIFY(section(window,3));
                named(window,"textPlacementDetails")->setProperty("expanded",true);
            }
            auto *x=named(window,caption?"captionPositionXField":"textPositionXField"); auto *y=named(window,caption?"captionPositionYField":"textPositionYField"); auto *size=named(window,caption?"captionSizeField":"textSizeField"); QVERIFY(x); QVERIFY(y); QVERIFY(size);
            const auto history=editor.undoText(); x->forceActiveFocus(); size->forceActiveFocus(); y->forceActiveFocus(); x->forceActiveFocus();
            if(caption) {
                QCOMPARE(editor.project().captions[0].x,.234567); QCOMPARE(editor.project().captions[0].y,.345678); QCOMPARE(editor.project().captions[0].fontSize,54);
                QCOMPARE(editor.project().captions[1].x,.812345); QCOMPARE(editor.project().captions[1].y,.723456); QCOMPARE(editor.project().captions[1].fontSize,84);
            } else { QCOMPARE(editor.project().textOverlays[0].x,.234567); QCOMPARE(editor.project().textOverlays[0].y,.345678); }
            QCOMPARE(editor.undoText(),history);
        }
    }
    void selectingCaptionsDoesNotCopyAppearance() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window); QVERIFY(editor.importFiles({fixture()}));
        editor.addCaption(); editor.styleCaptions({{"bold",false},{"shadow",false},{"fade",true}});
        editor.addCaption(); editor.styleCaptions({{"bold",true},{"shadow",true},{"fade",false}});
        editor.selectCaption(0); const auto history=editor.undoText(); editor.selectCaption(1,Qt::ControlModifier);
        QVERIFY(!editor.project().captions[0].bold); QVERIFY(!editor.project().captions[0].shadow); QVERIFY(editor.project().captions[0].fade);
        QVERIFY(editor.project().captions[1].bold); QVERIFY(editor.project().captions[1].shadow); QVERIFY(!editor.project().captions[1].fade); QCOMPARE(editor.undoText(),history);
    }
    void audioAcceptsItsRoundedMaximumFade() {
        EditorController editor; QQmlApplicationEngine engine; auto *window=load(engine,editor); QVERIFY(window); QVERIFY(editor.importFiles({fixture()}));
        editor.setTrimLength(61.0/60); editor.setMusicPath(fixture()); editor.selectAudio(2); editor.updateAudio(2,{{"fadeInFrames",61}}); QVERIFY(section(window,1));
        named(window,"audioTimingDetails")->setProperty("expanded",true); auto *fade=named(window,"audioFadeInField"); QVERIFY(fade);
        QCOMPARE(fade->property("text").toString(),QString("1.017")); QVERIFY(fade->property("acceptableInput").toBool());
        fade->forceActiveFocus(); QVERIFY(QMetaObject::invokeMethod(fade,"editingFinished")); QCOMPARE(editor.project().musicFadeInFrames,61);
    }
};
QTEST_MAIN(InspectorUxTest)
#include "inspector_ux_test.moc"
