#include "core/EditOps.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace bc::edit {
namespace {

constexpr Tick kHuge = std::numeric_limits<Tick>::max() / 4;

Tick scaled(Tick t, double speed)
{
    return static_cast<Tick>(std::llround(static_cast<double>(t) * speed));
}

QString remapLink(QHash<QString, QString>& map, const QString& linkId)
{
    if (linkId.isEmpty()) return QString();
    auto it = map.find(linkId);
    if (it != map.end()) return it.value();
    const QString fresh = makeId(QStringLiteral("link"));
    map.insert(linkId, fresh);
    return fresh;
}

// Room available to grow the head (towards earlier time) from the media.
Tick headRoom(const Clip& c, const MediaRef* media)
{
    if (c.isGenerator() || !media || media->isStillImage()) return kHuge;
    if (!c.reverse) return static_cast<Tick>(static_cast<double>(c.sourceIn) / c.speed);
    const double total = static_cast<double>(media->duration - c.sourceIn) / c.speed;
    return std::max<Tick>(0, static_cast<Tick>(total) - c.duration);
}

Tick tailRoom(const Clip& c, const MediaRef* media)
{
    if (c.isGenerator() || !media || media->isStillImage()) return kHuge;
    if (c.reverse) return static_cast<Tick>(static_cast<double>(c.sourceIn) / c.speed);
    const double total = static_cast<double>(media->duration - c.sourceIn) / c.speed;
    return std::max<Tick>(0, static_cast<Tick>(total) - c.duration);
}

void applyHead(Clip& c, Tick d)
{
    c.start += d;
    c.duration -= d;
    if (!c.reverse) c.sourceIn = std::max<Tick>(0, c.sourceIn + scaled(d, c.speed));
    shiftKeyframes(c, -d);
}

void applyTail(Clip& c, Tick d)
{
    c.duration += d;
    if (c.reverse) c.sourceIn = std::max<Tick>(0, c.sourceIn - scaled(d, c.speed));
}

QString splitClip(Sequence& seq, int ti, int ci, Tick t, QHash<QString, QString>& linkMap)
{
    Track& track = seq.tracks[ti];
    Clip& c = track.clips[ci];
    const Tick offset = t - c.start;
    if (offset <= 0 || t >= c.end()) return QString();

    Clip right = c;
    right.id = makeId(QStringLiteral("clip"));
    right.start = t;
    right.duration = c.end() - t;
    if (!c.reverse) {
        right.sourceIn = c.sourceIn + scaled(offset, c.speed);
    } else {
        right.sourceIn = c.sourceIn;
        c.sourceIn = c.sourceIn + scaled(right.duration, c.speed);
    }
    right.inTransition = {};
    right.linkId = remapLink(linkMap, c.linkId);
    shiftKeyframes(right, -offset);

    c.duration = offset;
    c.outTransition = {};

    const QString id = right.id;
    track.clips.insert(ci + 1, right);
    return id;
}

Tick maxLeftShiftIgnoring(const Sequence& seq, Tick at, const QSet<int>& tracks,
                          const QSet<QString>& ignore)
{
    Tick result = kHuge;
    for (int ti : tracks) {
        const Track* track = seq.track(ti);
        if (!track) continue;
        Tick prevEnd = 0;
        Tick firstStart = kHuge;
        for (const auto& c : track->clips) {
            if (ignore.contains(c.id)) continue;
            if (c.start >= at)
                firstStart = std::min(firstStart, c.start);
            else
                prevEnd = std::max(prevEnd, c.end());
        }
        if (firstStart == kHuge) continue;
        result = std::min(result, std::max<Tick>(0, firstStart - prevEnd));
    }
    return result;
}

void shiftFromIgnoring(Sequence& seq, Tick at, Tick delta, const QSet<int>& tracks,
                       const QSet<QString>& ignore)
{
    if (delta == 0) return;
    for (int ti : tracks) {
        Track* track = seq.track(ti);
        if (!track || track->locked) continue;
        for (auto& c : track->clips)
            if (c.start >= at && !ignore.contains(c.id)) c.start += delta;
        track->sortClips();
    }
}

void sortAll(Sequence& seq)
{
    for (auto& t : seq.tracks) t.sortClips();
    seq.recomputeDuration();
}

struct Located {
    int track = -1;
    int index = -1;
};

Located locate(const Sequence& seq, const QString& id)
{
    for (int ti = 0; ti < seq.tracks.size(); ++ti) {
        const int ci = seq.tracks[ti].indexOf(id);
        if (ci >= 0) return {ti, ci};
    }
    return {};
}

} // namespace

// ------------------------------------------------------------------ context

const MediaRef* Context::find(const QString& mediaId) const
{
    if (!media || mediaId.isEmpty()) return nullptr;
    for (const auto& m : *media)
        if (m.id == mediaId) return &m;
    return nullptr;
}

Context Context::forSequence(const Sequence& seq, const QVector<MediaRef>& media)
{
    Context ctx;
    ctx.media = &media;
    ctx.frame = seq.rate.ticksPerFrame();
    return ctx;
}

// ------------------------------------------------------------------ queries

QStringList expandLinks(const Sequence& seq, const QStringList& ids)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString& id : ids) {
        for (const QString& linked : seq.linkedClips(id)) {
            if (seen.contains(linked)) continue;
            seen.insert(linked);
            out << linked;
        }
    }
    return out;
}

void headLimits(const Sequence& seq, int ti, int ci, const Context& ctx,
                Tick& minDelta, Tick& maxDelta, bool ignoreNeighbours)
{
    const Track& track = seq.tracks[ti];
    const Clip& c = track.clips[ci];
    maxDelta = c.duration - std::max<Tick>(1, ctx.frame);
    minDelta = -std::min(headRoom(c, ctx.find(c.mediaId)), c.start);
    if (!ignoreNeighbours && ci > 0)
        minDelta = std::max(minDelta, track.clips[ci - 1].end() - c.start);
    if (maxDelta < 0) maxDelta = 0;
    if (minDelta > 0) minDelta = 0;
}

void tailLimits(const Sequence& seq, int ti, int ci, const Context& ctx,
                Tick& minDelta, Tick& maxDelta, bool ignoreNeighbours)
{
    const Track& track = seq.tracks[ti];
    const Clip& c = track.clips[ci];
    minDelta = -(c.duration - std::max<Tick>(1, ctx.frame));
    maxDelta = tailRoom(c, ctx.find(c.mediaId));
    if (!ignoreNeighbours && ci + 1 < track.clips.size())
        maxDelta = std::min(maxDelta, track.clips[ci + 1].start - c.end());
    if (maxDelta < 0) maxDelta = 0;
    if (minDelta > 0) minDelta = 0;
}

QVector<Tick> editPoints(const Sequence& seq, bool targetedOnly)
{
    QVector<Tick> points{0};
    for (const auto& t : seq.tracks) {
        if (targetedOnly && !t.targeted) continue;
        for (const auto& c : t.clips) {
            points << c.start << c.end();
        }
    }
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

Tick maxLeftShift(const Sequence& seq, Tick at, const QSet<int>& tracks)
{
    return maxLeftShiftIgnoring(seq, at, tracks, {});
}

QSet<int> rippleTracks(const Sequence& seq, const QSet<int>& origin)
{
    QSet<int> result;
    for (int i = 0; i < seq.tracks.size(); ++i) {
        const Track& t = seq.tracks[i];
        if (t.locked) continue;
        if (origin.contains(i) || t.syncLock) result.insert(i);
    }
    return result;
}

// ------------------------------------------------------------ primitives

void shiftKeyframes(Clip& clip, Tick delta)
{
    if (delta == 0) return;
    for (auto& e : clip.effects)
        for (auto& p : e.params)
            for (auto& k : p.keys) k.time += delta;
}

void clearRange(Sequence& seq, int ti, Tick from, Tick to, QHash<QString, QString>& linkMap)
{
    Track* track = seq.track(ti);
    if (!track || track->locked || to <= from) return;

    for (int i = track->clips.size() - 1; i >= 0; --i) {
        Clip& c = track->clips[i];
        if (c.end() <= from || c.start >= to) continue;

        if (c.start >= from && c.end() <= to) {
            track->clips.removeAt(i);
            continue;
        }
        if (c.start < from && c.end() > to) {
            splitClip(seq, ti, i, to, linkMap);
            Clip& left = track->clips[i];
            applyTail(left, from - left.end());
            left.outTransition = {};
            continue;
        }
        if (c.start < from) {
            applyTail(c, from - c.end());
            c.outTransition = {};
        } else {
            applyHead(c, to - c.start);
            c.inTransition = {};
        }
    }
}

void shiftFrom(Sequence& seq, Tick at, Tick delta, const QSet<int>& tracks)
{
    shiftFromIgnoring(seq, at, delta, tracks, {});
}

// ------------------------------------------------------------------- edits

QStringList splitAt(Sequence& seq, Tick time, const QStringList& onlyClips, const Context& ctx)
{
    QStringList created;
    QHash<QString, QString> linkMap;
    const Tick minimum = std::max<Tick>(1, ctx.frame);

    QStringList scope = onlyClips.isEmpty() ? QStringList() : expandLinks(seq, onlyClips);

    for (int ti = 0; ti < seq.tracks.size(); ++ti) {
        Track& track = seq.tracks[ti];
        if (track.locked) continue;
        const int ci = track.indexAt(time);
        if (ci < 0) continue;
        const Clip& c = track.clips[ci];
        if (!scope.isEmpty() && !scope.contains(c.id)) continue;
        if (time - c.start < minimum || c.end() - time < minimum) continue;
        const QString id = splitClip(seq, ti, ci, time, linkMap);
        if (!id.isEmpty()) created << id;
    }
    sortAll(seq);
    return created;
}

bool removeClips(Sequence& seq, const QStringList& ids, bool ripple)
{
    struct Range { Tick from; Tick to; };
    QVector<Range> ranges;
    QSet<int> origin;

    for (const QString& id : ids) {
        const Located at = locate(seq, id);
        if (at.track < 0 || seq.tracks[at.track].locked) continue;
        const Clip& c = seq.tracks[at.track].clips[at.index];
        ranges.push_back({c.start, c.end()});
        origin.insert(at.track);
        seq.tracks[at.track].clips.removeAt(at.index);
    }
    if (ranges.isEmpty()) return false;

    if (ripple) {
        std::sort(ranges.begin(), ranges.end(),
                  [](const Range& a, const Range& b) { return a.from < b.from; });
        QVector<Range> merged;
        for (const auto& r : ranges) {
            if (!merged.isEmpty() && r.from <= merged.last().to)
                merged.last().to = std::max(merged.last().to, r.to);
            else
                merged.push_back(r);
        }
        const QSet<int> tracks = rippleTracks(seq, origin);
        for (int i = merged.size() - 1; i >= 0; --i) {
            const Tick length = merged[i].to - merged[i].from;
            const Tick shift = std::min(length, maxLeftShift(seq, merged[i].to, tracks));
            if (shift > 0) shiftFrom(seq, merged[i].to, -shift, tracks);
        }
    }
    sortAll(seq);
    return true;
}

bool closeGapAt(Sequence& seq, int ti, Tick time)
{
    Track* track = seq.track(ti);
    if (!track || track->locked || track->clipAt(time)) return false;

    Tick prevEnd = 0;
    Tick nextStart = kHuge;
    for (const auto& c : track->clips) {
        if (c.end() <= time) prevEnd = std::max(prevEnd, c.end());
        if (c.start >= time) nextStart = std::min(nextStart, c.start);
    }
    if (nextStart == kHuge) return false;

    const QSet<int> tracks = rippleTracks(seq, {ti});
    const Tick shift = std::min(nextStart - prevEnd, maxLeftShift(seq, nextStart, tracks));
    if (shift <= 0) return false;
    shiftFrom(seq, nextStart, -shift, tracks);
    sortAll(seq);
    return true;
}

bool closeAllGaps(Sequence& seq)
{
    // Removes stretches where no unlocked track has any content.
    QVector<QPair<Tick, Tick>> spans;
    for (const auto& t : seq.tracks)
        for (const auto& c : t.clips) spans.push_back({c.start, c.end()});
    if (spans.isEmpty()) return false;
    std::sort(spans.begin(), spans.end());

    QVector<QPair<Tick, Tick>> gaps;
    Tick cursor = 0;
    for (const auto& s : spans) {
        if (s.first > cursor) gaps.push_back({cursor, s.first});
        cursor = std::max(cursor, s.second);
    }
    if (gaps.isEmpty()) return false;

    QSet<int> all;
    for (int i = 0; i < seq.tracks.size(); ++i)
        if (!seq.tracks[i].locked) all.insert(i);

    for (int i = gaps.size() - 1; i >= 0; --i) {
        const Tick shift = std::min(gaps[i].second - gaps[i].first,
                                    maxLeftShift(seq, gaps[i].second, all));
        if (shift > 0) shiftFrom(seq, gaps[i].second, -shift, all);
    }
    sortAll(seq);
    return true;
}

bool place(Sequence& seq, QVector<Placement> placements, PlaceMode mode)
{
    if (placements.isEmpty()) return false;
    for (const auto& p : placements) {
        const Track* t = seq.track(p.trackIndex);
        if (!t || t->locked || p.clip.duration <= 0) return false;
    }

    QHash<QString, QString> linkMap;

    if (mode == PlaceMode::Insert) {
        Tick from = kHuge;
        Tick to = 0;
        QSet<int> origin;
        for (const auto& p : placements) {
            from = std::min(from, p.clip.start);
            to = std::max(to, p.clip.end());
            origin.insert(p.trackIndex);
        }
        const QSet<int> tracks = rippleTracks(seq, origin);
        for (int ti : tracks) {
            const int ci = seq.tracks[ti].indexAt(from);
            if (ci >= 0 && seq.tracks[ti].clips[ci].start < from)
                splitClip(seq, ti, ci, from, linkMap);
        }
        shiftFrom(seq, from, to - from, tracks);
    }

    for (const auto& p : placements) {
        clearRange(seq, p.trackIndex, p.clip.start, p.clip.end(), linkMap);
        seq.tracks[p.trackIndex].clips.push_back(p.clip);
        seq.tracks[p.trackIndex].sortClips();
    }
    sortAll(seq);
    return true;
}

bool moveClips(Sequence& seq, const QStringList& ids, Tick delta, int laneDelta,
               PlaceMode mode, const Context& ctx)
{
    Q_UNUSED(ctx);
    QVector<Placement> placements;
    Tick minStart = kHuge;

    for (const QString& id : ids) {
        const Located at = locate(seq, id);
        if (at.track < 0) continue;
        if (seq.tracks[at.track].locked) return false;
        const Clip& c = seq.tracks[at.track].clips[at.index];
        minStart = std::min(minStart, c.start);

        const Track& src = seq.tracks[at.track];
        const int target = seq.trackForLane(src.kind, seq.laneOf(at.track) + laneDelta);
        if (target < 0 || seq.tracks[target].locked) return false;
        placements.push_back({target, c});
    }
    if (placements.isEmpty()) return false;

    delta = std::max(delta, -minStart);
    if (delta == 0 && laneDelta == 0) return false;

    for (const QString& id : ids) {
        const Located at = locate(seq, id);
        if (at.track >= 0) seq.tracks[at.track].clips.removeAt(at.index);
    }
    for (auto& p : placements) p.clip.start += delta;

    return place(seq, placements, mode);
}

bool trim(Sequence& seq, const QString& clipId, bool head, Tick delta, TrimMode mode,
          const Context& ctx, bool includeLinked)
{
    const Located main = locate(seq, clipId);
    if (main.track < 0 || seq.tracks[main.track].locked) return false;
    const Clip mainClip = seq.tracks[main.track].clips[main.index];
    const Tick edge = head ? mainClip.start : mainClip.end();

    // Linked clips that share the edge are trimmed together.
    QVector<Located> targets{main};
    if (includeLinked && !mainClip.linkId.isEmpty()) {
        for (const QString& id : seq.linkedClips(clipId)) {
            if (id == clipId) continue;
            const Located at = locate(seq, id);
            if (at.track < 0 || seq.tracks[at.track].locked) continue;
            const Clip& c = seq.tracks[at.track].clips[at.index];
            if ((head ? c.start : c.end()) == edge) targets.push_back(at);
        }
    }

    QSet<QString> targetIds;
    QSet<int> origin;
    for (const auto& t : targets) {
        targetIds.insert(seq.tracks[t.track].clips[t.index].id);
        origin.insert(t.track);
    }

    Tick lo = -kHuge;
    Tick hi = kHuge;

    if (mode == TrimMode::Roll) {
        bool any = false;
        for (const auto& t : targets) {
            const Track& track = seq.tracks[t.track];
            const int other = head ? t.index - 1 : t.index + 1;
            if (other < 0 || other >= track.clips.size()) continue;
            const Clip& a = head ? track.clips[other] : track.clips[t.index];
            const Clip& b = head ? track.clips[t.index] : track.clips[other];
            if (a.end() != b.start) continue;
            any = true;
            const int ai = head ? other : t.index;
            const int bi = ai + 1;
            Tick aMin, aMax, bMin, bMax;
            tailLimits(seq, t.track, ai, ctx, aMin, aMax, true);
            headLimits(seq, t.track, bi, ctx, bMin, bMax, true);
            lo = std::max({lo, aMin, bMin});
            hi = std::min({hi, aMax, bMax});
        }
        if (!any) mode = TrimMode::Normal;
        else {
            delta = std::clamp(delta, lo, hi);
            if (delta == 0) return false;
            for (const auto& t : targets) {
                Track& track = seq.tracks[t.track];
                const int ai = head ? t.index - 1 : t.index;
                const int bi = ai + 1;
                if (ai < 0 || bi >= track.clips.size()) continue;
                if (track.clips[ai].end() != track.clips[bi].start) continue;
                applyTail(track.clips[ai], delta);
                applyHead(track.clips[bi], delta);
            }
            sortAll(seq);
            return true;
        }
    }

    for (const auto& t : targets) {
        Tick mn, mx;
        if (head) headLimits(seq, t.track, t.index, ctx, mn, mx, mode == TrimMode::Ripple);
        else tailLimits(seq, t.track, t.index, ctx, mn, mx, mode == TrimMode::Ripple);
        lo = std::max(lo, mn);
        hi = std::min(hi, mx);
    }

    if (mode == TrimMode::Ripple) {
        const QSet<int> tracks = rippleTracks(seq, origin);
        // Head trim keeps the start fixed; shrinking pulls later material left.
        const Tick pull = maxLeftShiftIgnoring(seq, edge, tracks, targetIds);
        if (head) {
            // Head ripple cannot be limited by the timeline start (start is fixed).
            lo = -kHuge;
            for (const auto& t : targets) {
                const Clip& c = seq.tracks[t.track].clips[t.index];
                lo = std::max(lo, -headRoom(c, ctx.find(c.mediaId)));
            }
            hi = std::min(hi, pull);
        } else {
            lo = std::max(lo, -pull);
        }
    }

    if (lo > hi) return false;
    delta = std::clamp(delta, lo, hi);
    if (delta == 0) return false;

    if (mode == TrimMode::Ripple) {
        const QSet<int> tracks = rippleTracks(seq, origin);
        const Tick oldEdge = edge;
        for (const auto& t : targets) {
            Clip& c = seq.tracks[t.track].clips[t.index];
            if (head) {
                // Start stays put: consume source at the head, shorten the clip.
                if (!c.reverse) c.sourceIn = std::max<Tick>(0, c.sourceIn + scaled(delta, c.speed));
                c.duration -= delta;
                shiftKeyframes(c, -delta);
            } else {
                applyTail(c, delta);
            }
        }
        shiftFromIgnoring(seq, oldEdge, head ? -delta : delta, tracks, targetIds);
    } else {
        for (const auto& t : targets) {
            Clip& c = seq.tracks[t.track].clips[t.index];
            if (head) applyHead(c, delta);
            else applyTail(c, delta);
        }
    }
    sortAll(seq);
    return true;
}

bool slip(Sequence& seq, const QStringList& ids, Tick delta, const Context& ctx)
{
    bool changed = false;
    for (const QString& id : expandLinks(seq, ids)) {
        const Located at = locate(seq, id);
        if (at.track < 0 || seq.tracks[at.track].locked) continue;
        Clip& c = seq.tracks[at.track].clips[at.index];
        const MediaRef* media = ctx.find(c.mediaId);
        if (c.isGenerator() || !media || media->isStillImage()) continue;
        const Tick maxIn = std::max<Tick>(0, media->duration - c.sourceSpan());
        // Dragging right reveals earlier material, as in Premiere.
        const Tick move = scaled(delta, c.speed) * (c.reverse ? 1 : -1);
        const Tick newIn = std::clamp<Tick>(c.sourceIn + move, 0, maxIn);
        if (newIn != c.sourceIn) {
            c.sourceIn = newIn;
            changed = true;
        }
    }
    return changed;
}

bool slide(Sequence& seq, const QStringList& ids, Tick delta, const Context& ctx)
{
    const QStringList all = expandLinks(seq, ids);
    Tick lo = -kHuge;
    Tick hi = kHuge;
    QVector<Located> targets;

    for (const QString& id : all) {
        const Located at = locate(seq, id);
        if (at.track < 0 || seq.tracks[at.track].locked) return false;
        targets.push_back(at);
        const Track& track = seq.tracks[at.track];
        const Clip& c = track.clips[at.index];

        if (at.index > 0) {
            const Clip& prev = track.clips[at.index - 1];
            if (prev.end() == c.start) {
                Tick mn, mx;
                tailLimits(seq, at.track, at.index - 1, ctx, mn, mx, true);
                lo = std::max(lo, mn);
                hi = std::min(hi, mx);
            } else {
                lo = std::max(lo, prev.end() - c.start);
            }
        } else {
            lo = std::max(lo, -c.start);
        }
        if (at.index + 1 < track.clips.size()) {
            const Clip& next = track.clips[at.index + 1];
            if (next.start == c.end()) {
                Tick mn, mx;
                headLimits(seq, at.track, at.index + 1, ctx, mn, mx, true);
                lo = std::max(lo, mn);
                hi = std::min(hi, mx);
            } else {
                hi = std::min(hi, next.start - c.end());
            }
        }
    }
    if (targets.isEmpty() || lo > hi) return false;
    delta = std::clamp(delta, lo, hi);
    if (delta == 0) return false;

    for (const auto& at : targets) {
        Track& track = seq.tracks[at.track];
        Clip& c = track.clips[at.index];
        const bool prevAdj = at.index > 0 && track.clips[at.index - 1].end() == c.start;
        const bool nextAdj = at.index + 1 < track.clips.size()
                             && track.clips[at.index + 1].start == c.end();
        if (prevAdj) applyTail(track.clips[at.index - 1], delta);
        if (nextAdj) applyHead(track.clips[at.index + 1], delta);
        c.start += delta;
    }
    sortAll(seq);
    return true;
}

bool rateStretch(Sequence& seq, const QString& clipId, bool head, Tick newEdge, const Context& ctx)
{
    const Located main = locate(seq, clipId);
    if (main.track < 0 || seq.tracks[main.track].locked) return false;
    const Clip mainClip = seq.tracks[main.track].clips[main.index];
    const Tick edge = head ? mainClip.start : mainClip.end();
    const Tick minimum = std::max<Tick>(1, ctx.frame);

    QVector<Located> targets{main};
    for (const QString& id : seq.linkedClips(clipId)) {
        if (id == clipId) continue;
        const Located at = locate(seq, id);
        if (at.track < 0 || seq.tracks[at.track].locked) continue;
        const Clip& c = seq.tracks[at.track].clips[at.index];
        if ((head ? c.start : c.end()) == edge && c.duration == mainClip.duration) targets.push_back(at);
    }

    // Respect neighbours on every target track.
    for (const auto& at : targets) {
        const Track& track = seq.tracks[at.track];
        if (head && at.index > 0) newEdge = std::max(newEdge, track.clips[at.index - 1].end());
        if (!head && at.index + 1 < track.clips.size())
            newEdge = std::min(newEdge, track.clips[at.index + 1].start);
    }
    newEdge = std::max<Tick>(0, newEdge);

    const Tick newDuration = head ? mainClip.end() - newEdge : newEdge - mainClip.start;
    if (newDuration < minimum || newDuration == mainClip.duration) return false;
    const double speed = std::clamp(static_cast<double>(mainClip.sourceSpan())
                                        / static_cast<double>(newDuration), 0.05, 20.0);

    for (const auto& at : targets) {
        Clip& c = seq.tracks[at.track].clips[at.index];
        const Tick end = c.end();
        c.speed = speed;
        c.duration = newDuration;
        if (head) c.start = end - newDuration;
    }
    sortAll(seq);
    return true;
}

bool setSpeed(Sequence& seq, const QStringList& ids, double speed, bool reverse, bool ripple,
              const Context& ctx)
{
    speed = std::clamp(speed, 0.05, 20.0);
    const QStringList all = expandLinks(seq, ids);

    // Process later clips first so ripple shifts never move an unprocessed clip.
    QVector<QPair<Tick, QString>> order;
    for (const QString& id : all) {
        const Clip* c = seq.findClip(id);
        if (c) order.push_back({c->start, id});
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    bool changed = false;
    for (const auto& entry : order) {
        const Located at = locate(seq, entry.second);
        if (at.track < 0 || seq.tracks[at.track].locked) continue;
        Track& track = seq.tracks[at.track];
        Clip& c = track.clips[at.index];
        const Tick span = c.sourceSpan();
        Tick newDuration = std::max<Tick>(std::max<Tick>(1, ctx.frame),
                                          static_cast<Tick>(static_cast<double>(span) / speed));
        const Tick oldEnd = c.end();
        const Tick growth = newDuration - c.duration;

        if (!ripple && at.index + 1 < track.clips.size())
            newDuration = std::min(newDuration, track.clips[at.index + 1].start - c.start);

        c.speed = speed;
        c.reverse = reverse;
        if (ripple) {
            const QSet<int> tracks = rippleTracks(seq, {at.track});
            Tick shift = growth;
            if (shift < 0) shift = -std::min(-shift, maxLeftShiftIgnoring(seq, oldEnd, tracks, {c.id}));
            c.duration = newDuration;
            shiftFromIgnoring(seq, oldEnd, shift, tracks, {c.id});
        } else {
            c.duration = newDuration;
        }
        changed = true;
    }
    sortAll(seq);
    return changed;
}

bool liftRange(Sequence& seq, Tick from, Tick to)
{
    if (to <= from) return false;
    QHash<QString, QString> linkMap;
    bool any = false;
    for (int ti = 0; ti < seq.tracks.size(); ++ti) {
        const Track& t = seq.tracks[ti];
        if (t.locked || !t.targeted) continue;
        clearRange(seq, ti, from, to, linkMap);
        any = true;
    }
    sortAll(seq);
    return any;
}

bool extractRange(Sequence& seq, Tick from, Tick to)
{
    if (!liftRange(seq, from, to)) return false;
    QSet<int> origin;
    for (int ti = 0; ti < seq.tracks.size(); ++ti)
        if (!seq.tracks[ti].locked && seq.tracks[ti].targeted) origin.insert(ti);
    const QSet<int> tracks = rippleTracks(seq, origin);
    const Tick shift = std::min(to - from, maxLeftShift(seq, to, tracks));
    if (shift > 0) shiftFrom(seq, to, -shift, tracks);
    sortAll(seq);
    return true;
}

bool rippleTrimToTime(Sequence& seq, Tick time, bool trimHeadSide)
{
    const QVector<Tick> points = editPoints(seq, true);
    if (trimHeadSide) {
        Tick prev = -1;
        for (Tick p : points)
            if (p < time) prev = p;
        if (prev < 0) return false;
        return extractRange(seq, prev, time);
    }
    for (Tick p : points)
        if (p > time) return extractRange(seq, time, p);
    return false;
}

bool setTransition(Sequence& seq, const QString& clipId, bool head, TransitionType type, Tick duration)
{
    const Located at = locate(seq, clipId);
    if (at.track < 0 || seq.tracks[at.track].locked) return false;
    Track& track = seq.tracks[at.track];
    Transition tr{type, type == TransitionType::None ? 0 : duration};

    if (head) {
        track.clips[at.index].inTransition = tr;
        return true;
    }
    if (at.index + 1 < track.clips.size()
        && track.clips[at.index + 1].start == track.clips[at.index].end()) {
        track.clips[at.index + 1].inTransition = tr;
        track.clips[at.index].outTransition = {};
        return true;
    }
    track.clips[at.index].outTransition = tr;
    return true;
}

bool applyDefaultTransition(Sequence& seq, Tick time, const QStringList& selection,
                            TrackKind kind, TransitionType type, Tick duration)
{
    bool changed = false;
    if (!selection.isEmpty()) {
        for (const QString& id : selection) {
            int ti = -1;
            const Clip* c = seq.findClip(id, &ti);
            if (!c || seq.tracks[ti].kind != kind) continue;
            changed |= setTransition(seq, id, true, type, duration);
            changed |= setTransition(seq, id, false, type, duration);
        }
        return changed;
    }

    for (int ti = 0; ti < seq.tracks.size(); ++ti) {
        Track& track = seq.tracks[ti];
        if (track.locked || !track.targeted || track.kind != kind || track.clips.isEmpty()) continue;

        // Nearest clip boundary on this track.
        Tick best = kHuge;
        int bestIndex = -1;
        bool bestHead = true;
        for (int ci = 0; ci < track.clips.size(); ++ci) {
            const Clip& c = track.clips[ci];
            if (std::llabs(c.start - time) < best) { best = std::llabs(c.start - time); bestIndex = ci; bestHead = true; }
            if (std::llabs(c.end() - time) < best) { best = std::llabs(c.end() - time); bestIndex = ci; bestHead = false; }
        }
        if (bestIndex < 0) continue;
        changed |= setTransition(seq, track.clips[bestIndex].id, bestHead, type, duration);
    }
    return changed;
}

QVector<ClipboardItem> copy(const Sequence& seq, const QStringList& ids)
{
    QVector<ClipboardItem> items;
    Tick minStart = kHuge;
    for (const QString& id : ids) {
        int ti = -1;
        const Clip* c = seq.findClip(id, &ti);
        if (!c) continue;
        ClipboardItem item;
        item.kind = seq.tracks[ti].kind;
        item.lane = seq.laneOf(ti);
        item.clip = *c;
        minStart = std::min(minStart, c->start);
        items.push_back(item);
    }
    for (auto& item : items) item.clip.start -= minStart;
    return items;
}

QStringList paste(Sequence& seq, const QVector<ClipboardItem>& items, Tick at, PlaceMode mode)
{
    QStringList ids;
    if (items.isEmpty()) return ids;
    QHash<QString, QString> linkMap;
    QVector<Placement> placements;

    for (const auto& item : items) {
        int ti = seq.trackForLane(item.kind, item.lane);
        while (ti < 0) {
            addTrack(seq, item.kind);
            ti = seq.trackForLane(item.kind, item.lane);
        }
        Clip c = item.clip;
        c.id = makeId(QStringLiteral("clip"));
        c.start = at + item.clip.start;
        c.linkId = remapLink(linkMap, item.clip.linkId);
        for (auto& e : c.effects) e.id = makeId(QStringLiteral("fx"));
        ids << c.id;
        placements.push_back({ti, c});
    }
    if (!place(seq, placements, mode)) return {};
    return ids;
}

bool setEnabled(Sequence& seq, const QStringList& ids, bool enabled)
{
    bool changed = false;
    for (const QString& id : ids) {
        int ti = -1;
        Clip* c = seq.findClip(id, &ti);
        if (!c || seq.tracks[ti].locked || c->enabled == enabled) continue;
        c->enabled = enabled;
        changed = true;
    }
    return changed;
}

bool link(Sequence& seq, const QStringList& ids)
{
    if (ids.size() < 2) return false;
    const QString linkId = makeId(QStringLiteral("link"));
    for (const QString& id : ids)
        if (Clip* c = seq.findClip(id)) c->linkId = linkId;
    return true;
}

bool unlink(Sequence& seq, const QStringList& ids)
{
    bool changed = false;
    for (const QString& id : ids) {
        Clip* c = seq.findClip(id);
        if (c && !c->linkId.isEmpty()) {
            c->linkId.clear();
            changed = true;
        }
    }
    return changed;
}

int addTrack(Sequence& seq, TrackKind kind)
{
    Track t;
    t.kind = kind;
    t.id = makeId(kind == TrackKind::Video ? QStringLiteral("vt") : QStringLiteral("at"));
    int index;
    if (kind == TrackKind::Video) {
        seq.tracks.insert(0, t);
        index = 0;
    } else {
        seq.tracks.push_back(t);
        index = seq.tracks.size() - 1;
    }
    seq.renameTracks();
    return index;
}

bool removeTrack(Sequence& seq, int ti)
{
    if (!seq.track(ti)) return false;
    const TrackKind kind = seq.tracks[ti].kind;
    int sameKind = 0;
    for (const auto& t : seq.tracks)
        if (t.kind == kind) ++sameKind;
    if (sameKind <= 1) return false;   // keep at least one of each kind
    seq.tracks.removeAt(ti);
    seq.renameTracks();
    seq.recomputeDuration();
    return true;
}

QString addMarker(Sequence& seq, Tick time, const QString& name)
{
    Marker m;
    m.id = makeId(QStringLiteral("mk"));
    m.time = time;
    m.name = name;
    seq.markers.push_back(m);
    std::sort(seq.markers.begin(), seq.markers.end(),
              [](const Marker& a, const Marker& b) { return a.time < b.time; });
    return m.id;
}

bool removeMarkerAt(Sequence& seq, Tick time, Tick tolerance)
{
    for (int i = 0; i < seq.markers.size(); ++i) {
        if (std::llabs(seq.markers[i].time - time) <= tolerance) {
            seq.markers.removeAt(i);
            return true;
        }
    }
    return false;
}

bool validate(const Sequence& seq, QString* problem)
{
    for (const auto& t : seq.tracks) {
        for (int i = 0; i < t.clips.size(); ++i) {
            const Clip& c = t.clips[i];
            if (c.duration <= 0) {
                if (problem) *problem = QStringLiteral("%1: clip %2 has non-positive duration").arg(t.name, c.id);
                return false;
            }
            if (c.start < 0) {
                if (problem) *problem = QStringLiteral("%1: clip %2 starts before zero").arg(t.name, c.id);
                return false;
            }
            if (i > 0 && t.clips[i - 1].end() > c.start) {
                if (problem) *problem = QStringLiteral("%1: clips %2 and %3 overlap").arg(t.name, t.clips[i - 1].id, c.id);
                return false;
            }
        }
    }
    return true;
}

} // namespace bc::edit
