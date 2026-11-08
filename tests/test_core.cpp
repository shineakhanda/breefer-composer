// Headless tests for the timeline model and edit engine. No GUI, no FFmpeg.
// Run: build/breefer_tests.exe  (exit code 0 = all passed)

#include "core/EditOps.h"
#include "core/Evaluate.h"
#include "core/Project.h"
#include "io/Inflate.h"
#include "io/Interchange.h"
#include "io/ProjectIO.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cstdio>
#include <functional>
#include <vector>

using namespace bc;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++g_checks;                                                                     \
        if (!(cond)) {                                                                  \
            ++g_failures;                                                               \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        ++g_checks;                                                                     \
        const auto va = (a);                                                            \
        const auto vb = (b);                                                            \
        if (!(va == vb)) {                                                              \
            ++g_failures;                                                               \
            std::printf("  FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, \
                        #a, #b, static_cast<long long>(va), static_cast<long long>(vb)); \
        }                                                                               \
    } while (0)

const FrameRate kRate{25, 1};
const Tick F = kRate.ticksPerFrame();   // one frame

Tick frames(int n) { return F * n; }

struct Fixture {
    QVector<MediaRef> media;
    Sequence seq;
    edit::Context ctx;

    Fixture()
    {
        MediaRef m;
        m.id = QStringLiteral("m1");
        m.name = QStringLiteral("clip.mp4");
        m.duration = frames(250);   // 10 s
        m.hasVideo = m.hasAudio = true;
        m.rate = kRate;
        media.push_back(m);

        seq = makeDefaultSequence(QStringLiteral("Test"), 1920, 1080, kRate);
        ctx = edit::Context::forSequence(seq, media);
    }

    int v1() const { return seq.trackForLane(TrackKind::Video, 1); }
    int v2() const { return seq.trackForLane(TrackKind::Video, 2); }
    int a1() const { return seq.trackForLane(TrackKind::Audio, 1); }

    // Adds a linked V1/A1 pair: [start, start+len) using media from `in`.
    QString addPair(int startFrame, int lenFrames, int inFrame = 0)
    {
        const QString link = makeId(QStringLiteral("link"));
        Clip v;
        v.id = makeId(QStringLiteral("v"));
        v.mediaId = QStringLiteral("m1");
        v.start = frames(startFrame);
        v.duration = frames(lenFrames);
        v.sourceIn = frames(inFrame);
        v.linkId = link;
        Clip a = v;
        a.id = makeId(QStringLiteral("a"));
        seq.tracks[v1()].clips.push_back(v);
        seq.tracks[a1()].clips.push_back(a);
        seq.tracks[v1()].sortClips();
        seq.tracks[a1()].sortClips();
        seq.recomputeDuration();
        return v.id;
    }

    const Clip& clip(int track, int index) const { return seq.tracks[track].clips[index]; }
};

void test(const char* name, const std::function<void()>& fn)
{
    const int before = g_failures;
    fn();
    std::printf("%s %s\n", g_failures == before ? "[ ok ]" : "[FAIL]", name);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    test("timebase divides NTSC rates exactly", [] {
        for (FrameRate r : {FrameRate{24000, 1001}, FrameRate{30000, 1001}, FrameRate{60000, 1001},
                            FrameRate{24, 1}, FrameRate{25, 1}, FrameRate{50, 1}}) {
            CHECK((kTicksPerSecond * r.den) % r.num == 0);
        }
    });

    test("drop-frame timecode skips ;00 and ;01 at minute boundaries", [] {
        const FrameRate ntsc{30000, 1001};
        CHECK(formatTimecode(ntsc.ticksForFrames(1800), ntsc, true) == QStringLiteral("00:01:00:02"));
        CHECK(formatTimecode(ntsc.ticksForFrames(17982), ntsc, true) == QStringLiteral("00:10:00:00"));
    });

    test("track lane numbering follows Premiere (V1 nearest audio)", [] {
        Fixture f;
        CHECK_EQ(f.seq.laneOf(f.v1()), 1);
        CHECK_EQ(f.seq.laneOf(f.v2()), 2);
        CHECK(f.seq.tracks[f.v1()].name == QStringLiteral("V1"));
        CHECK(f.seq.tracks[f.a1()].name == QStringLiteral("A1"));
    });

    test("binary-search lookup finds clip under time", [] {
        Fixture f;
        f.addPair(0, 50);
        f.addPair(100, 50);
        const Track& t = f.seq.tracks[f.v1()];
        CHECK_EQ(t.indexAt(frames(10)), 0);
        CHECK_EQ(t.indexAt(frames(75)), -1);
        CHECK_EQ(t.indexAt(frames(100)), 1);
        CHECK_EQ(t.indexAt(frames(150)), -1);
    });

    test("split keeps source continuity and relinks halves", [] {
        Fixture f;
        f.addPair(0, 100, 20);
        const QStringList created = edit::splitAt(f.seq, frames(40), {}, f.ctx);
        CHECK_EQ(created.size(), 2);
        const Clip& l = f.clip(f.v1(), 0);
        const Clip& r = f.clip(f.v1(), 1);
        CHECK_EQ(l.duration, frames(40));
        CHECK_EQ(r.start, frames(40));
        CHECK_EQ(r.sourceIn, frames(60));
        CHECK(r.linkId == f.clip(f.a1(), 1).linkId);
        CHECK(r.linkId != l.linkId);
        CHECK(edit::validate(f.seq));
    });

    test("ripple delete closes gap on synced tracks", [] {
        Fixture f;
        const QString first = f.addPair(0, 50);
        f.addPair(50, 50);
        edit::removeClips(f.seq, edit::expandLinks(f.seq, {first}), true);
        CHECK_EQ(f.seq.tracks[f.v1()].clips.size(), 1);
        CHECK_EQ(f.clip(f.v1(), 0).start, 0);
        CHECK_EQ(f.clip(f.a1(), 0).start, 0);
        CHECK(edit::validate(f.seq));
    });

    test("ripple delete never overlaps a clip spanning the gap on another track", [] {
        Fixture f;
        const QString first = f.addPair(0, 50);
        f.addPair(50, 50);
        // V2 holds a title spanning frames 30..70 — it must limit the ripple.
        Clip title;
        title.id = QStringLiteral("t");
        title.generator = GeneratorKind::Title;
        title.start = frames(30);
        title.duration = frames(40);
        f.seq.tracks[f.v2()].clips.push_back(title);
        f.seq.tracks[f.v2()].clips.push_back([&] { Clip c = title; c.id = "t2"; c.start = frames(80); c.duration = frames(10); return c; }());
        f.seq.tracks[f.v2()].sortClips();
        edit::removeClips(f.seq, edit::expandLinks(f.seq, {first}), true);
        CHECK(edit::validate(f.seq));
        CHECK_EQ(f.clip(f.v2(), 1).start, frames(70));    // could only move 10 frames
        CHECK_EQ(f.clip(f.v1(), 0).start, frames(40));
    });

    test("normal trim is limited by media and neighbours, linked follows", [] {
        Fixture f;
        const QString a = f.addPair(20, 30, 10);
        f.addPair(60, 50);
        // Extend head by 30 frames: only 10 frames of media handle exist.
        edit::trim(f.seq, a, true, -frames(30), edit::TrimMode::Normal, f.ctx);
        CHECK_EQ(f.clip(f.v1(), 0).start, frames(10));
        CHECK_EQ(f.clip(f.v1(), 0).sourceIn, 0);
        // Extend tail by 30: neighbour at 60 stops it at +10.
        edit::trim(f.seq, f.clip(f.v1(), 0).id, false, frames(30), edit::TrimMode::Normal, f.ctx);
        CHECK_EQ(f.clip(f.v1(), 0).end(), frames(60));
        CHECK_EQ(f.clip(f.a1(), 0).end(), frames(60));
        CHECK(edit::validate(f.seq));
    });

    test("ripple trim tail shifts downstream on every synced track", [] {
        Fixture f;
        const QString a = f.addPair(0, 50);
        f.addPair(50, 50, 100);
        edit::trim(f.seq, a, false, -frames(20), edit::TrimMode::Ripple, f.ctx);
        CHECK_EQ(f.clip(f.v1(), 0).duration, frames(30));
        CHECK_EQ(f.clip(f.v1(), 1).start, frames(30));
        CHECK_EQ(f.clip(f.a1(), 1).start, frames(30));
        CHECK(edit::validate(f.seq));
    });

    test("ripple trim head keeps start and consumes source", [] {
        Fixture f;
        const QString a = f.addPair(0, 50);
        f.addPair(50, 50, 100);
        edit::trim(f.seq, a, true, frames(10), edit::TrimMode::Ripple, f.ctx);
        CHECK_EQ(f.clip(f.v1(), 0).start, 0);
        CHECK_EQ(f.clip(f.v1(), 0).sourceIn, frames(10));
        CHECK_EQ(f.clip(f.v1(), 1).start, frames(40));
        CHECK(edit::validate(f.seq));
    });

    test("roll moves the cut without changing total length", [] {
        Fixture f;
        const QString a = f.addPair(0, 50, 0);
        f.addPair(50, 50, 100);
        const Tick before = f.seq.duration;
        edit::trim(f.seq, a, false, frames(15), edit::TrimMode::Roll, f.ctx);
        CHECK_EQ(f.clip(f.v1(), 0).duration, frames(65));
        CHECK_EQ(f.clip(f.v1(), 1).start, frames(65));
        CHECK_EQ(f.clip(f.v1(), 1).sourceIn, frames(115));
        CHECK_EQ(f.seq.duration, before);
        CHECK(edit::validate(f.seq));
    });

    test("slip changes source only, clamped to media", [] {
        Fixture f;
        const QString a = f.addPair(10, 50, 100);
        edit::slip(f.seq, {a}, -frames(30), f.ctx);
        CHECK_EQ(f.clip(f.v1(), 0).sourceIn, frames(130));
        CHECK_EQ(f.clip(f.v1(), 0).start, frames(10));
        edit::slip(f.seq, {a}, -frames(500), f.ctx);
        CHECK_EQ(f.clip(f.v1(), 0).sourceIn, frames(200));   // 250 - 50
        CHECK_EQ(f.clip(f.a1(), 0).sourceIn, frames(200));
    });

    test("slide adjusts both neighbours", [] {
        Fixture f;
        f.addPair(0, 50, 0);
        const QString mid = f.addPair(50, 50, 100);
        f.addPair(100, 50, 100);
        edit::slide(f.seq, {mid}, frames(10), f.ctx);
        CHECK_EQ(f.clip(f.v1(), 0).duration, frames(60));
        CHECK_EQ(f.clip(f.v1(), 1).start, frames(60));
        CHECK_EQ(f.clip(f.v1(), 2).start, frames(110));
        CHECK_EQ(f.clip(f.v1(), 2).sourceIn, frames(110));
        CHECK(edit::validate(f.seq));
    });

    test("overwrite move trims what it lands on", [] {
        Fixture f;
        f.addPair(0, 100);
        const QString b = f.addPair(150, 50);
        edit::moveClips(f.seq, edit::expandLinks(f.seq, {b}), -frames(100), 0,
                        edit::PlaceMode::Overwrite, f.ctx);
        CHECK(edit::validate(f.seq));
        CHECK_EQ(f.seq.tracks[f.v1()].clips.size(), 2);
        CHECK_EQ(f.clip(f.v1(), 0).duration, frames(50));
        CHECK_EQ(f.clip(f.v1(), 1).start, frames(50));
    });

    test("lane move mirrors linked audio", [] {
        Fixture f;
        const QString a = f.addPair(0, 50);
        edit::moveClips(f.seq, edit::expandLinks(f.seq, {a}), 0, 1, edit::PlaceMode::Overwrite, f.ctx);
        CHECK_EQ(f.seq.tracks[f.v2()].clips.size(), 1);
        CHECK_EQ(f.seq.tracks[f.seq.trackForLane(TrackKind::Audio, 2)].clips.size(), 1);
    });

    test("insert placement ripples later clips right", [] {
        Fixture f;
        f.addPair(0, 100);
        Clip c;
        c.id = QStringLiteral("ins");
        c.mediaId = QStringLiteral("m1");
        c.start = frames(40);
        c.duration = frames(20);
        edit::place(f.seq, {{f.v1(), c}}, edit::PlaceMode::Insert);
        CHECK(edit::validate(f.seq));
        CHECK_EQ(f.seq.tracks[f.v1()].clips.size(), 3);
        CHECK_EQ(f.clip(f.v1(), 2).start, frames(60));
        CHECK_EQ(f.clip(f.a1(), 1).start, frames(60));   // audio split + rippled too
        CHECK_EQ(f.seq.duration, frames(120));
    });

    test("extract removes in/out range and ripples", [] {
        Fixture f;
        f.addPair(0, 100);
        CHECK(edit::extractRange(f.seq, frames(20), frames(30)));
        CHECK(edit::validate(f.seq));
        CHECK_EQ(f.seq.duration, frames(90));
        CHECK_EQ(f.clip(f.v1(), 1).start, frames(20));
        CHECK_EQ(f.clip(f.v1(), 1).sourceIn, frames(30));
    });

    test("copy/paste creates fresh ids and links", [] {
        Fixture f;
        const QString a = f.addPair(0, 50);
        const auto items = edit::copy(f.seq, edit::expandLinks(f.seq, {a}));
        const QStringList ids = edit::paste(f.seq, items, frames(200), edit::PlaceMode::Overwrite);
        CHECK_EQ(ids.size(), 2);
        CHECK(!ids.contains(a));
        const Clip* pv = f.seq.findClip(ids[0]);
        CHECK(pv && pv->start == frames(200));
        CHECK(pv && pv->linkId != f.clip(f.v1(), 0).linkId);
    });

    test("speed change rescales duration and ripples", [] {
        Fixture f;
        const QString a = f.addPair(0, 50);
        f.addPair(50, 50, 100);
        edit::setSpeed(f.seq, {a}, 2.0, false, true, f.ctx);
        CHECK_EQ(f.clip(f.v1(), 0).duration, frames(25));
        CHECK_EQ(f.clip(f.v1(), 1).start, frames(25));
        CHECK(edit::validate(f.seq));
    });

    test("reverse clip maps first frame to last source frame", [] {
        Clip c;
        c.start = 0;
        c.duration = frames(10);
        c.sourceIn = frames(5);
        c.reverse = true;
        CHECK(c.sourceTime(0) > frames(14) - F);
        CHECK(c.sourceTime(frames(10) - 1) < frames(6));
    });

    test("cross dissolve evaluates both sides around the cut", [] {
        Fixture f;
        f.addPair(0, 50);
        const QString b = f.addPair(50, 50, 100);
        edit::setTransition(f.seq, b, true, TransitionType::CrossDissolve, frames(10));
        const Track& t = f.seq.tracks[f.v1()];
        const TrackEval mid = evaluateTrack(t, frames(50));
        CHECK(mid.isTransition());
        CHECK(std::abs(mid.progress - 0.5) < 1e-6);
        CHECK(evaluateTrack(t, frames(44)).clip != nullptr);
        CHECK(evaluateTrack(t, frames(45)).isTransition());
        CHECK(!evaluateTrack(t, frames(55)).isTransition());
        // Constant power: gains at the midpoint are equal and sum of squares == 1.
        const Track& at = f.seq.tracks[f.a1()];
        Sequence copy = f.seq;
        edit::setTransition(copy, f.clip(f.a1(), 1).id, true, TransitionType::CrossDissolve, frames(10));
        const Track& ct = copy.tracks[f.a1()];
        const double g0 = clipAudioGain(ct, 0, frames(50));
        const double g1 = clipAudioGain(ct, 1, frames(50));
        CHECK(std::abs(g0 * g0 + g1 * g1 - 1.0) < 1e-6);
        Q_UNUSED(at);
    });

    test("one-sided fade in", [] {
        Fixture f;
        const QString a = f.addPair(0, 50);
        edit::setTransition(f.seq, a, true, TransitionType::DipToBlack, frames(10));
        const Track& t = f.seq.tracks[f.v1()];
        CHECK(std::abs(evaluateTrack(t, frames(5)).fade - 0.5) < 1e-6);
        CHECK(std::abs(evaluateTrack(t, frames(20)).fade - 1.0) < 1e-6);
    });

    test("Q/W ripple trim to playhead", [] {
        Fixture f;
        f.addPair(0, 100);
        CHECK(edit::rippleTrimToTime(f.seq, frames(30), true));
        CHECK_EQ(f.seq.duration, frames(70));
        CHECK_EQ(f.clip(f.v1(), 0).sourceIn, frames(30));
        CHECK(edit::rippleTrimToTime(f.seq, frames(50), false));
        CHECK_EQ(f.seq.duration, frames(50));
    });

    test("locked tracks are never modified", [] {
        Fixture f;
        const QString a = f.addPair(0, 50);
        f.addPair(50, 50);
        f.seq.tracks[f.a1()].locked = true;
        edit::removeClips(f.seq, edit::expandLinks(f.seq, {a}), true);
        CHECK_EQ(f.seq.tracks[f.a1()].clips.size(), 2);
        CHECK(edit::validate(f.seq));
    });

    test("close all gaps", [] {
        Fixture f;
        f.addPair(10, 20);
        f.addPair(50, 20);
        CHECK(edit::closeAllGaps(f.seq));
        CHECK_EQ(f.clip(f.v1(), 0).start, 0);
        CHECK_EQ(f.clip(f.v1(), 1).start, frames(20));
    });

    test("project round-trips through JSON", [] {
        Fixture f;
        const QString a = f.addPair(0, 50);
        edit::setTransition(f.seq, a, true, TransitionType::CrossDissolve, frames(12));
        edit::addMarker(f.seq, frames(7), QStringLiteral("Beat"));
        f.seq.inPoint = frames(3);
        f.seq.outPoint = frames(9);
        Clip title;
        title.id = QStringLiteral("title");
        title.generator = GeneratorKind::Title;
        title.title.text = QStringLiteral("Hello");
        title.duration = frames(10);
        f.seq.tracks[f.v2()].clips.push_back(title);

        Project p;
        p.id = QStringLiteral("p");
        p.media = f.media;
        p.sequences.push_back(f.seq);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("rt.bcproj"));
        QString error;
        CHECK(io::saveProject(p, path, &error));
        Project q;
        CHECK(io::loadProject(path, q, &error));
        const Sequence& s = q.sequences.first();
        CHECK_EQ(s.markers.size(), 1);
        CHECK_EQ(s.inPoint, frames(3));
        const Clip* t = s.findClip(QStringLiteral("title"));
        CHECK(t && t->title.text == QStringLiteral("Hello"));
        const Clip* c = s.findClip(a);
        CHECK(c && c->inTransition.type == TransitionType::CrossDissolve);
        CHECK(c && !c->linkId.isEmpty());
    });

    test("OTIO export/import round-trip keeps gaps and timing", [] {
        Fixture f;
        f.media[0].path = QStringLiteral("C:/media/clip.mp4");
        f.addPair(10, 40, 5);      // starts after a 10-frame gap
        f.addPair(80, 20, 100);    // 30-frame gap before it
        Project p;
        p.media = f.media;
        p.sequences.push_back(f.seq);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("rt.otio"));
        QString error;
        CHECK(io::exportOtio(f.seq, p, path, &error));
        Sequence back;
        QVector<MediaRef> media;
        QStringList warnings;
        CHECK(io::importTimeline(path, back, media, &warnings, &error));
        const int v1 = back.trackForLane(TrackKind::Video, 1);
        CHECK(v1 >= 0);
        if (v1 >= 0) {
            CHECK_EQ(back.tracks[v1].clips.size(), 2);
            if (back.tracks[v1].clips.size() == 2) {
                CHECK_EQ(back.tracks[v1].clips[0].start, frames(10));
                CHECK_EQ(back.tracks[v1].clips[0].sourceIn, frames(5));
                CHECK_EQ(back.tracks[v1].clips[1].start, frames(80));
                CHECK_EQ(back.tracks[v1].clips[1].duration, frames(20));
            }
        }
        CHECK_EQ(media.size(), 1);
        CHECK(edit::validate(back));
    });

    test("FCP7 XML export/import round-trip", [] {
        Fixture f;
        f.media[0].path = QStringLiteral("C:/media/clip.mp4");
        f.addPair(0, 50, 0);
        f.addPair(60, 25, 100);
        Project p;
        p.media = f.media;
        p.sequences.push_back(f.seq);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("rt.xml"));
        QString error;
        CHECK(io::exportFcp7Xml(f.seq, p, path, &error));
        Sequence back;
        QVector<MediaRef> media;
        CHECK(io::importTimeline(path, back, media, nullptr, &error));
        if (!error.isEmpty()) std::printf("  import error: %s\n", qPrintable(error));
        const int v1 = back.trackForLane(TrackKind::Video, 1);
        CHECK(v1 >= 0 && back.tracks[v1].clips.size() == 2);
        if (v1 >= 0 && back.tracks[v1].clips.size() == 2) {
            CHECK_EQ(back.tracks[v1].clips[1].start, frames(60));
            CHECK_EQ(back.tracks[v1].clips[1].sourceIn, frames(100));
        }
        CHECK_EQ(back.rate.num, 25);
    });

    test("nest moves clips into a new sequence and leaves linked nest clips", [] {
        Fixture f;
        const QString a = f.addPair(10, 40);
        const QString b = f.addPair(60, 20, 100);
        Sequence nested;
        const QStringList created = edit::nest(f.seq, edit::expandLinks(f.seq, {a, b}), nested, QStringLiteral("Nest 1"));
        CHECK_EQ(created.size(), 2);
        CHECK(edit::validate(f.seq));
        CHECK(edit::validate(nested));
        CHECK_EQ(f.seq.tracks[f.v1()].clips.size(), 1);
        const Clip& n = f.clip(f.v1(), 0);
        CHECK(n.generator == GeneratorKind::Nest);
        CHECK(n.nestedSequenceId == nested.id);
        CHECK_EQ(n.start, frames(10));
        CHECK_EQ(n.duration, frames(70));
        CHECK(!n.linkId.isEmpty() && n.linkId == f.clip(f.a1(), 0).linkId);
        const int nv1 = nested.trackForLane(TrackKind::Video, 1);
        CHECK(nv1 >= 0 && nested.tracks[nv1].clips.size() == 2);
        if (nv1 >= 0 && nested.tracks[nv1].clips.size() == 2) CHECK_EQ(nested.tracks[nv1].clips[1].start, frames(50));
    });

    test("project media paths are portable", [] {
        QTemporaryDir dir;
        QFile beside(dir.filePath(QStringLiteral("moved.mp4")));   // exists beside the project
        CHECK(beside.open(QIODevice::WriteOnly));
        beside.close();
        Project p;
        p.id = QStringLiteral("p");
        MediaRef rel;  rel.id = QStringLiteral("m1"); rel.path = QStringLiteral("clips/a.mp4"); rel.name = QStringLiteral("a");
        MediaRef moved; moved.id = QStringLiteral("m2"); moved.path = QStringLiteral("D:/gone/elsewhere/moved.mp4"); moved.name = QStringLiteral("m");
        p.media = {rel, moved};
        p.sequences.push_back(makeDefaultSequence());
        const QString file = dir.filePath(QStringLiteral("t.bcproj"));
        QString error;
        CHECK(io::saveProject(p, file, &error));
        Project q;
        CHECK(io::loadProject(file, q, &error));
        CHECK(q.media[0].path == QDir::cleanPath(dir.filePath(QStringLiteral("clips/a.mp4"))));
        CHECK(q.media[1].path == QDir::cleanPath(dir.filePath(QStringLiteral("moved.mp4"))));
    });

    test("gzip/DEFLATE decoder handles stored, fixed and dynamic blocks", [] {
        // Fixed-Huffman stream for "hello hello hello" (zlib level 1 output, no header).
        const QByteArray fixed = QByteArray::fromHex("cb48cdc9c957c8409000");
        QByteArray out;
        QString error;
        CHECK(io::inflateRaw(fixed, 0, out, &error));
        CHECK(out == QByteArray("hello hello hello"));
        // Stored block.
        QByteArray stored = QByteArray::fromHex("010500faff") + QByteArray("abcde");
        CHECK(io::inflateRaw(stored, 0, out, &error));
        CHECK(out == QByteArray("abcde"));
    });

    test("Premiere .prproj import follows the object graph", [] {
        const QString path = QStringLiteral(BC_TEST_DATA "/sample.prproj");
        QVector<Sequence> sequences;
        QVector<MediaRef> media;
        QStringList warnings;
        QString error;
        CHECK(io::importPremiereProject(path, sequences, media, &warnings, &error));
        if (!error.isEmpty()) std::printf("  error: %s\n", qPrintable(error));
        CHECK_EQ(sequences.size(), 1);
        if (sequences.isEmpty()) return;
        const Sequence& s = sequences.first();
        CHECK(s.name == QStringLiteral("Main Edit"));
        CHECK_EQ(s.rate.num, 25);
        CHECK_EQ(s.width, 1920);
        CHECK_EQ(media.size(), 1);
        const int v1 = s.trackForLane(TrackKind::Video, 1);
        const int a1 = s.trackForLane(TrackKind::Audio, 1);
        CHECK(v1 >= 0 && a1 >= 0);
        if (v1 < 0 || a1 < 0) return;
        CHECK_EQ(s.tracks[v1].clips.size(), 2);
        if (s.tracks[v1].clips.size() == 2) {
            CHECK_EQ(s.tracks[v1].clips[0].sourceIn, frames(10));
            CHECK_EQ(s.tracks[v1].clips[1].start, frames(60));
            CHECK(std::abs(s.tracks[v1].clips[1].speed - 2.0) < 1e-9);
            CHECK(!s.tracks[v1].clips[0].linkId.isEmpty());
            CHECK(s.tracks[v1].clips[0].linkId == s.tracks[a1].clips[0].linkId);
        }
        CHECK(!warnings.isEmpty());   // the transition is reported
        CHECK(edit::validate(s));
    });

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
