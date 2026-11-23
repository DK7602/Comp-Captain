#include "cc/SongTime.h"

#include "cc/TextFormat.h"

#include <algorithm>
#include <cmath>

namespace cc
{
    bool Meter::isValid() const noexcept
    {
        const bool powerOfTwo = denominator > 0 && (denominator & (denominator - 1)) == 0;
        return numerator >= 1 && numerator <= 64 && powerOfTwo && denominator <= 64;
    }

    double Meter::quartersPerBeat() const noexcept
    {
        return isValid() ? 4.0 / (double) denominator : 1.0;
    }

    double Meter::quartersPerBar() const noexcept
    {
        return isValid() ? (double) numerator * 4.0 / (double) denominator : 4.0;
    }

    std::string Meter::toText() const
    {
        return std::to_string (numerator) + "/" + std::to_string (denominator);
    }

    Meter Meter::fromText (std::string_view s)
    {
        return fromText (s, Meter {});
    }

    Meter Meter::fromText (std::string_view s, Meter fallback)
    {
        const auto slash = s.find ('/');
        if (slash == std::string_view::npos)
            return fallback;
        std::int64_t n = 0, d = 0;
        if (! text::parseInt (s.substr (0, slash), n) || ! text::parseInt (s.substr (slash + 1), d))
            return fallback;
        Meter m { (int) n, (int) d };
        return m.isValid() ? m : fallback;
    }

    std::string BarBeat::toText() const
    {
        const int whole = (int) std::floor (beat + 1e-6);
        if (whole <= 1 && beat < 1.0 + 1e-3)
            return std::to_string (bar);
        return std::to_string (bar) + "." + std::to_string (whole);
    }

    BarBeat barBeatAtPpq (double ppq, Meter meter)
    {
        const double qpb = meter.quartersPerBar();
        const double bars = ppq / qpb;
        double barIndex = std::floor (bars + 1e-9);
        double within = ppq - barIndex * qpb;
        if (within < 0.0)
            within = 0.0;
        return { (int) barIndex + 1, 1.0 + within / meter.quartersPerBeat() };
    }

    BarBeat barBeatAt (const SongPosition& pos)
    {
        if (! pos.hasBarStart)
            return barBeatAtPpq (pos.ppq, pos.meter);

        const double qpb = pos.meter.quartersPerBar();
        const int barIndex = pos.barNumber > 0 ? pos.barNumber - 1 : (int) std::lround (pos.barStartPpq / qpb);
        double within = pos.ppq - pos.barStartPpq;
        if (within < 0.0 || within >= qpb + 1e-6)
            return barBeatAtPpq (pos.ppq, pos.meter); // the DAW's bar start doesn't fit: count it ourselves
        return { barIndex + 1, 1.0 + within / pos.meter.quartersPerBeat() };
    }

    double ppqForBarBeat (int bar, double beat, Meter meter)
    {
        return (double) (bar - 1) * meter.quartersPerBar() + (beat - 1.0) * meter.quartersPerBeat();
    }

    double clampBpm (double bpm)
    {
        if (! std::isfinite (bpm) || bpm <= 0.0)
            return 120.0;
        return std::clamp (bpm, 20.0, 400.0);
    }

    SongPosition positionAtBar (int bar, double beat, double bpm, Meter meter)
    {
        if (! meter.isValid())
            meter = {};
        bpm = clampBpm (bpm);
        SongPosition p;
        p.ppq = std::max (0.0, ppqForBarBeat (std::max (1, bar), std::max (1.0, beat), meter));
        p.seconds = p.ppq * 60.0 / bpm;
        p.bpm = bpm;
        p.meter = meter;
        p.barStartPpq = ppqForBarBeat (std::max (1, bar), 1.0, meter);
        p.hasBarStart = true;
        return p;
    }
}
