#include "TestUtil.h"
#include "core/vault/TextFile.h"

using namespace mn;
using namespace Qt::StringLiterals;

class TestTextFile : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void roundTripsEveryEncodingFixture_data()
    {
        QTest::addColumn<QString>("name");
        for (const char* n : {"bom.md", "crlf.md", "mixed.md", "no-trailing-newline.md", "nbsp.md",
                              "lone-cr.md", "empty.md", "double-bom.md"})
            QTest::newRow(n) << QString::fromLatin1(n);
    }
    void roundTripsEveryEncodingFixture()
    {
        QFETCH(QString, name);
        const QString path = test::fixture(u"encoding/"_s + name);
        const QByteArray before = test::readBytes(path);
        const LoadedText t = loadTextFile(path);
        QCOMPARE(t.status, LoadStatus::Ok);
        QVERIFY(t.roundTrips);
        QCOMPARE(encodeText(t.text, t.format, t.raw), before);
        // Loading never modifies the file.
        QCOMPARE(test::readBytes(path), before);
    }

    void detectsFormat()
    {
        const LoadedText bom = loadTextFile(test::fixture(u"encoding/bom.md"_s));
        QVERIFY(bom.format.bom);
        QCOMPARE(bom.text, u"# BOM\nText\n"_s);

        const LoadedText crlf = loadTextFile(test::fixture(u"encoding/crlf.md"_s));
        QCOMPARE(crlf.format.eol, LineEnding::CRLF);
        QVERIFY(!crlf.format.mixedEol);
        QCOMPARE(crlf.text, u"# CRLF\nLine two\n"_s);

        const LoadedText mixed = loadTextFile(test::fixture(u"encoding/mixed.md"_s));
        QVERIFY(mixed.format.mixedEol);

        const LoadedText noNl = loadTextFile(test::fixture(u"encoding/no-trailing-newline.md"_s));
        QVERIFY(!noNl.text.endsWith(u'\n'));

        const LoadedText cr = loadTextFile(test::fixture(u"encoding/lone-cr.md"_s));
        QVERIFY(cr.format.loneCr);

        const LoadedText twoBoms = loadTextFile(test::fixture(u"encoding/double-bom.md"_s));
        QVERIFY(twoBoms.format.bom);
        QVERIFY(twoBoms.text.startsWith(QChar(0xFEFF)));
    }

    void refusesToEditInvalidOrBinary()
    {
        const LoadedText bad = loadTextFile(test::fixture(u"encoding/invalid-utf8.md"_s));
        QCOMPARE(bad.status, LoadStatus::InvalidUtf8);
        QVERIFY(!bad.editable());
        const LoadedText bin = loadTextFile(test::fixture(u"encoding/binary.md"_s));
        QCOMPARE(bin.status, LoadStatus::Binary);
        QVERIFY(!bin.editable());
        const LoadedText missing = loadTextFile(test::fixture(u"encoding/does-not-exist.md"_s));
        QCOMPARE(missing.status, LoadStatus::NotFound);
    }

    void editsKeepFormat()
    {
        const LoadedText crlf = loadTextFile(test::fixture(u"encoding/crlf.md"_s));
        QCOMPARE(encodeText(QString(crlf.text + u"added\n"_s), crlf.format),
                 QByteArray("# CRLF\r\nLine two\r\nadded\r\n"));
        const LoadedText bom = loadTextFile(test::fixture(u"encoding/bom.md"_s));
        QCOMPARE(encodeText(u"x\n"_s, bom.format), QByteArray("\xEF\xBB\xBFx\n"));
    }

    void mixedEndingsKeepUnchangedLines()
    {
        const LoadedText t = loadTextFile(test::fixture(u"encoding/mixed.md"_s));
        // Edit only the third line: the others keep their own endings.
        QString edited = t.text;
        edited.replace(u"line three"_s, u"line 3"_s);
        QCOMPARE(encodeText(edited, t.format, t.raw),
                 QByteArray("line one\r\nline two\nline 3\r\nline four\n"));
        // Appending at the end keeps every earlier ending.
        QCOMPARE(encodeText(QString(t.text + u"five\n"_s), t.format, t.raw).left(t.raw.size()), t.raw);
    }

    void atomicWrite()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + u"/note.md"_s;
        QVERIFY(writeFileAtomic(path, "first").ok);
        QCOMPARE(test::readBytes(path), QByteArray("first"));
        QVERIFY(writeFileAtomic(path, "second").ok);
        QCOMPARE(test::readBytes(path), QByteArray("second"));
        // No temp files left behind.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);
    }

    void atomicWriteFailureLeavesFileAlone()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + u"/missing-folder/note.md"_s;
        const WriteResult r = writeFileAtomic(path, "data");
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(!QFile::exists(path));
    }

    void statReportsMissing()
    {
        QVERIFY(!statFile(test::fixture(u"encoding/nope.md"_s)).exists());
        QVERIFY(statFile(test::fixture(u"encoding/bom.md"_s)).exists());
    }
};

QTEST_GUILESS_MAIN(TestTextFile)
#include "tst_textfile.moc"
