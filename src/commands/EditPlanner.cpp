#include "EditPlanner.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace sampler::commands
{
namespace
{
constexpr double eps = EditPlanner::epsilon;

bool sameBounds(const Span& a, const Span& b)
{
    return a.track == b.track && std::abs(a.start - b.start) < eps && std::abs(a.length - b.length) < eps
           && std::abs(a.offset - b.offset) < eps;
}

/** Applies the no-overlap rule of `winner` to the spans on its track. */
void cut(std::vector<Span>& working, const Span& winner, const TimeMap& time)
{
    std::vector<Span> pieces;
    for (auto it = working.begin(); it != working.end();)
    {
        auto& s = *it;
        const bool overlaps = s.track == winner.track && s.start < winner.end() - eps && s.end() > winner.start + eps;
        if (! overlaps)
        {
            ++it;
            continue;
        }
        if (s.start >= winner.start - eps && s.end() <= winner.end() + eps)
        {
            it = working.erase(it); // fully covered
            continue;
        }
        if (s.start < winner.start - eps && s.end() > winner.end() + eps)
        {
            // The winner lands inside: the part after it becomes a clip of its own.
            Span right = EditPlanner::withStart(s, winner.end(), time);
            right.copyOf = s.id.isNotEmpty() ? s.id : s.copyOf;
            right.id = {};
            right.tag = -1;
            pieces.push_back(right);
            s.length = winner.start - s.start;
        }
        else if (s.start < winner.start - eps)
        {
            s.length = winner.start - s.start; // the end is covered
        }
        else
        {
            s = EditPlanner::withStart(s, winner.end(), time); // the start is covered
        }
        ++it;
    }
    working.insert(working.end(), pieces.begin(), pieces.end());
}

Plan diff(const std::vector<Span>& existing, const std::vector<Span>& working)
{
    Plan plan;
    std::map<juce::String, const Span*> before;
    for (const auto& s : existing)
        before[s.id] = &s;

    std::map<juce::String, bool> kept;
    for (const auto& s : working)
    {
        if (s.id.isEmpty())
        {
            plan.created.push_back(s);
            continue;
        }
        kept[s.id] = true;
        if (const auto found = before.find(s.id); found == before.end() || ! sameBounds(*found->second, s))
            plan.changed.push_back(s);
    }
    for (const auto& s : existing)
        if (! kept.count(s.id))
            plan.removed.push_back(s.id);
    return plan;
}
} // namespace

TimeMap TimeMap::constantTempo(double bpm)
{
    return {[bpm](double beats) { return beats * 60.0 / bpm; }, [bpm](double seconds) { return seconds * bpm / 60.0; }};
}

Span EditPlanner::withStart(const Span& span, double newStart, const TimeMap& time)
{
    Span result = span;
    result.offset = span.offset + time.secondsBetween(span.start, newStart);
    result.length = span.end() - newStart;
    result.start = newStart;
    return result;
}

Plan EditPlanner::place(const std::vector<Span>& existing, const std::vector<Span>& placed, const TimeMap& time)
{
    std::vector<Span> working;
    for (const auto& s : existing)
    {
        const bool beingPlaced = std::any_of(placed.begin(), placed.end(), [&](const Span& p) { return p.id.isNotEmpty() && p.id == s.id; });
        if (! beingPlaced)
            working.push_back(s);
    }
    for (const auto& p : placed)
    {
        cut(working, p, time);
        working.push_back(p);
    }
    return diff(existing, working);
}

Plan EditPlanner::split(const std::vector<Span>& existing, const std::vector<juce::String>& ids, double beat, const TimeMap& time)
{
    Plan plan;
    for (const auto& s : existing)
    {
        if (std::find(ids.begin(), ids.end(), s.id) == ids.end() || beat <= s.start + eps || beat >= s.end() - eps)
            continue;
        Span left = s;
        left.length = beat - s.start;
        plan.changed.push_back(left);
        Span right = withStart(s, beat, time);
        right.copyOf = s.id;
        right.id = {};
        plan.created.push_back(right);
    }
    return plan;
}

Plan EditPlanner::shortenOverlaps(const std::vector<Span>& existing)
{
    Plan plan;
    std::map<int, std::vector<Span>> byTrack;
    for (const auto& s : existing)
        byTrack[s.track].push_back(s);
    for (auto& [track, spans] : byTrack)
    {
        std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
        for (size_t i = 0; i + 1 < spans.size(); ++i)
        {
            const auto& next = spans[i + 1];
            if (spans[i].end() <= next.start + eps)
                continue;
            if (next.start <= spans[i].start + eps)
            {
                plan.removed.push_back(spans[i].id);
                continue;
            }
            Span shortened = spans[i];
            shortened.length = next.start - shortened.start;
            plan.changed.push_back(shortened);
        }
    }
    return plan;
}

double EditPlanner::clampTrimStart(const Span& span, double newStart, double minLength, const TimeMap& time)
{
    const auto earliest = std::max(0.0, time.beatAfter(span.start, -span.offset)); // where offset 0 would sit
    const auto latest = span.end() - minLength;
    if (latest < earliest)
        return std::min(span.start, std::max(earliest, newStart)); // already shorter than the minimum: it may only grow
    return std::clamp(newStart, earliest, latest);
}

double EditPlanner::clampTrimEnd(const Span& span, double newEnd, double sourceSeconds, double minLength, const TimeMap& time)
{
    const auto latest = time.beatAfter(span.start, sourceSeconds - span.offset); // where the sound ends
    const auto shortest = span.start + minLength;
    if (latest < shortest)
        return std::max(span.end(), std::min(latest, newEnd)); // already shorter than the minimum: it may only grow
    return std::clamp(newEnd, shortest, latest);
}
} // namespace sampler::commands
