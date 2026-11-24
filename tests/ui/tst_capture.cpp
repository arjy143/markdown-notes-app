#include "TestUtil.h"
#include "ui/CaptureWindow.h"
#include "ui/Drawing.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTimer>

using namespace mn;
using namespace mn::ui;
using namespace Qt::StringLiterals;

namespace {

// A canvas showing a 1600×1000 sketch at half size, its top-left at (8, 8).
void showAtHalfSize(DrawingCanvas& c)
{
    c.resize(816, 516);
    c.show();
    QVERIFY(QTest::qWaitForWindowExposed(&c));
    QCOMPARE(c.viewRect(), QRectF(8, 8, 800, 500));
}

void drag(QWidget* w, QPoint from, QPoint to, int steps = 4)
{
    QTest::mousePress(w, Qt::LeftButton, {}, from);
    for (int i = 1; i <= steps; ++i)
        QTest::mouseMove(w, from + (to - from) * i / steps);
    QTest::mouseRelease(w, Qt::LeftButton, {}, to);
}

bool isDark(QRgb c)
{
    return qGray(c) < 80;
}

QImage solid(QSize size, QColor c)
{
    QImage img(size, QImage::Format_RGB32);
    img.fill(c);
    return img;
}

// Runs `f` on the drawing dialog once it opens.
void onDrawingDialog(const std::function<void(DrawingDialog*)>& f)
{
    QTimer::singleShot(0, [f] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* d = qobject_cast<DrawingDialog*>(w); d != nullptr && d->isVisible()) {
                f(d);
                return;
            }
        }
        QFAIL("no drawing dialog");
    });
}

} // namespace

class TestCapture : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void penStrokesDrawUndoAndRedo()
    {
        DrawingCanvas c(Sketch::blank({1600, 1000}));
        showAtHalfSize(c);
        QSignalSpy changed(&c, &DrawingCanvas::changed);
        drag(&c, {108, 108}, {308, 108});
        QCOMPARE(c.sketch().strokes.size(), 1);
        QCOMPARE(changed.size(), 1);
        QVERIFY(c.isModified());
        const QImage img = c.sketch().render();
        QCOMPARE(img.size(), QSize(1600, 1000));
        QVERIFY(isDark(img.pixel(400, 200))); // widget (208, 108) in image pixels
        QCOMPARE(img.pixel(400, 400), qRgb(255, 255, 255));

        c.undo();
        QVERIFY(c.sketch().strokes.isEmpty());
        QVERIFY(!c.isModified());
        c.redo();
        QCOMPARE(c.sketch().strokes.size(), 1);
        c.clear();
        QVERIFY(c.sketch().strokes.isEmpty());
        c.undo();
        QCOMPARE(c.sketch().strokes.size(), 1);
    }

    void lineWidthLooksTheSameAtAnyZoom()
    {
        // A 3200×2000 image shown at quarter size: a 4 px line is 16 image pixels.
        DrawingCanvas c(Sketch::over(solid({3200, 2000}, Qt::white)));
        c.resize(816, 516);
        c.show();
        QVERIFY(QTest::qWaitForWindowExposed(&c));
        c.setPenWidth(4);
        drag(&c, {100, 100}, {200, 200});
        QCOMPARE(c.sketch().strokes.value(0).width, 16.0);
        QCOMPARE(c.sketch().strokes.value(0).points.first(), QPointF(368, 368));
    }

    void rectanglesAndEmptyClicks()
    {
        DrawingCanvas c(Sketch::blank({1600, 1000}));
        showAtHalfSize(c);
        c.setTool(Stroke::Tool::Rectangle);
        c.setColor(Qt::red);
        drag(&c, {108, 108}, {208, 208});
        QCOMPARE(c.sketch().strokes.size(), 1);
        QCOMPARE(c.sketch().strokes[0].points.size(), 2);
        const QImage img = c.sketch().render();
        QCOMPARE(qRed(img.pixel(200, 300)), 255); // left edge
        QVERIFY(qGreen(img.pixel(200, 300)) < 80);
        QCOMPARE(img.pixel(300, 300), qRgb(255, 255, 255)); // inside stays clear
        // A click without dragging doesn't make an empty rectangle.
        QTest::mouseClick(&c, Qt::LeftButton, {}, {400, 300});
        QCOMPARE(c.sketch().strokes.size(), 1);
    }

    void annotationKeepsTheImage()
    {
        Sketch s = Sketch::over(solid({300, 200}, Qt::red));
        Stroke st;
        st.color = Qt::black;
        st.width = 6;
        st.points = {QPointF(10, 100), QPointF(290, 100)};
        s.strokes.append(st);
        const QImage img = s.render();
        QCOMPARE(img.size(), QSize(300, 200));
        QVERIFY(isDark(img.pixel(150, 100)));
        QCOMPARE(img.pixel(150, 20), qRgb(255, 0, 0));
        QCOMPARE(s.render(0.5).size(), QSize(150, 100));
    }

    void highlighterDoesNotDarkenWhereItCrosses()
    {
        Sketch s = Sketch::blank({200, 200});
        Stroke st;
        st.tool = Stroke::Tool::Highlighter;
        st.color = QColor(0xfb, 0xbc, 0x04, 110);
        st.width = 20;
        st.points = {QPointF(20, 20), QPointF(180, 180), QPointF(180, 20), QPointF(20, 180)};
        s.strokes.append(st);
        const QImage img = s.render();
        const QRgb crossing = img.pixel(100, 100);
        const QRgb single = img.pixel(60, 60);
        QVERIFY(crossing != qRgb(255, 255, 255));
        QVERIFY2(std::abs(qBlue(crossing) - qBlue(single)) <= 3, "the crossing is darker");
    }

    void pastedImagesAreListedAndSaved()
    {
        CaptureWindow box(u"Inbox.md"_s);
        box.show();
        auto* mime = new QMimeData;
        mime->setImageData(solid({40, 30}, Qt::blue));
        QApplication::clipboard()->setMimeData(mime);
        box.editor()->paste();
        QCOMPARE(box.imageCount(), 1);
        QVERIFY(box.editor()->toPlainText().isEmpty());
        QVERIFY(box.strip()->isVisible());
        QCOMPARE(box.strip()->count(), 1);

        const QDateTime at(QDate(2025, 3, 4), QTime(9, 5, 7));
        const QList<CaptureFile> files = box.files(at);
        QCOMPARE(files.size(), 1);
        QCOMPARE(files[0].baseName, u"Pasted image 20250304090507"_s);
        QCOMPARE(files[0].suffix, u"png"_s);
        QImage back;
        QVERIFY(back.loadFromData(files[0].bytes, "PNG"));
        QCOMPARE(back.size(), QSize(40, 30));
        QCOMPARE(back.pixel(5, 5), qRgb(0, 0, 255));

        // Enter submits even without text.
        QSignalSpy submitted(&box, &CaptureWindow::submitted);
        QTest::keyClick(box.editor(), Qt::Key_Return);
        QCOMPARE(submitted.size(), 1);

        // Delete in the strip removes it.
        box.strip()->setCurrentRow(0);
        QTest::keyClick(box.strip(), Qt::Key_Delete);
        QCOMPARE(box.imageCount(), 0);
        QVERIFY(!box.strip()->isVisible());
        QTest::keyClick(box.editor(), Qt::Key_Return);
        QCOMPARE(submitted.size(), 1); // nothing to capture
    }

    void imageFilesKeepTheirBytesUntilDrawnOn()
    {
        QTemporaryDir dir;
        const QString jpg = dir.path() + u"/photo.jpg"_s;
        QVERIFY(solid({64, 48}, Qt::green).save(jpg, "JPG"));
        const QByteArray original = test::readBytes(jpg);

        CaptureWindow box(u"Inbox.md"_s);
        box.show();
        auto* mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(jpg)});
        QApplication::clipboard()->setMimeData(mime);
        box.editor()->paste();
        QCOMPARE(box.imageCount(), 1);
        QList<CaptureFile> files = box.files(QDateTime::currentDateTime());
        QCOMPARE(files[0].baseName, u"photo"_s);
        QCOMPARE(files[0].suffix, u"jpg"_s);
        QCOMPARE(files[0].bytes, original);

        // Draw on it: saved as a PNG under the same name, at full size.
        onDrawingDialog([](DrawingDialog* d) {
            DrawingCanvas* c = d->canvas();
            const QRectF v = c->viewRect();
            drag(c, v.topLeft().toPoint() + QPoint(5, 5), v.center().toPoint());
            d->accept();
        });
        box.annotate(0);
        QCOMPARE(box.image(0).strokes.size(), 1);
        files = box.files(QDateTime::currentDateTime());
        QCOMPARE(files[0].baseName, u"photo"_s);
        QCOMPARE(files[0].suffix, u"png"_s);
        QImage back;
        QVERIFY(back.loadFromData(files[0].bytes, "PNG"));
        QCOMPARE(back.size(), QSize(64, 48));

        // Reopened, the stroke can still be undone; cancelling keeps what was there.
        onDrawingDialog([](DrawingDialog* d) {
            QVERIFY(!d->canvas()->isModified());
            d->canvas()->undo();
            QVERIFY(d->canvas()->sketch().strokes.isEmpty());
            QVERIFY(d->canvas()->isModified());
            d->done(QDialog::Rejected); // skips the discard question
        });
        box.annotate(0);
        QCOMPARE(box.image(0).strokes.size(), 1);
    }

    void newDrawingsAreAdded()
    {
        CaptureWindow box(u"Inbox.md"_s);
        box.show();
        onDrawingDialog([](DrawingDialog* d) {
            QCOMPARE(d->canvas()->sketch().size, QSize(1600, 1000));
            d->accept(); // nothing drawn: nothing added
        });
        box.draw();
        QCOMPARE(box.imageCount(), 0);
        onDrawingDialog([](DrawingDialog* d) {
            DrawingCanvas* c = d->canvas();
            drag(c, c->viewRect().center().toPoint(), c->viewRect().center().toPoint() + QPoint(40, 10));
            d->accept();
        });
        box.draw();
        QCOMPARE(box.imageCount(), 1);
        const QList<CaptureFile> files = box.files(QDateTime(QDate(2025, 3, 4), QTime(9, 5)));
        QCOMPARE(files[0].baseName, u"Drawing 20250304090500"_s);
        QCOMPARE(files[0].suffix, u"png"_s);
    }
};

QTEST_MAIN(TestCapture)
#include "tst_capture.moc"
