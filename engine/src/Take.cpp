#include "cc/Take.h"

#include "cc/TextFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace cc
{
    namespace
    {
        struct RoleNames { Role role; std::string_view name, label; };
        constexpr std::array<RoleNames, 7> roleNames { {
            { Role::lead, "lead", "Lead" },
            { Role::doubled, "double", "Double" },
            { Role::harmony, "harmony", "Harmony" },
            { Role::adlib, "adlib", "Ad-lib" },
            { Role::backing, "backing", "Backing" },
            { Role::guitar, "guitar", "Guitar" },
            { Role::other, "other", "Other" },
        } };

        constexpr std::array<std::pair<TakeSource, std::string_view>, 3> sourceNames { {
            { TakeSource::capture, "capture" }, { TakeSource::file, "file" }, { TakeSource::ara, "ara" } } };

        // "LeadVox_Dbl2 (L)" -> lead, vox, dbl, 2, l   ("3rd" -> 3, rd)
        std::vector<std::string> words (std::string_view name)
        {
            std::vector<std::string> out;
            std::string cur;
            auto flush = [&] { if (! cur.empty()) { out.push_back (cur); cur.clear(); } };
            unsigned char prev = 0;
            for (char c : name)
            {
                const auto u = (unsigned char) c;
                const bool alpha = std::isalpha (u) != 0, digit = std::isdigit (u) != 0;
                if (! alpha && ! digit)
                {
                    flush();
                    prev = 0;
                    continue;
                }
                const bool camel = alpha && std::isupper (u) && prev != 0 && std::islower (prev);
                const bool letterDigit = prev != 0 && ((digit && std::isalpha (prev)) || (alpha && std::isdigit (prev)));
                if (camel || letterDigit)
                    flush();
                cur += (char) std::tolower (u);
                prev = u;
            }
            flush();
            return out;
        }

        bool startsWith (const std::string& s, std::string_view prefix) { return s.rfind (prefix, 0) == 0; }

        template <size_t N>
        bool isOneOf (const std::string& w, const std::array<std::string_view, N>& list)
        {
            return std::find (list.begin(), list.end(), w) != list.end();
        }

        constexpr std::array<std::string_view, 6> guitarWords { "gtr", "gtrs", "guitar", "guitars", "guit", "acoustic" };
        constexpr std::array<std::string_view, 5> adlibWords { "adlib", "adlibs", "adl", "hype", "hypes" };
        constexpr std::array<std::string_view, 3> harmonyWords { "hrm", "third", "fifth" };
        constexpr std::array<std::string_view, 6> doubleWords { "dbl", "dbls", "double", "doubles", "doubled", "dub" };
        constexpr std::array<std::string_view, 17> backingWords { "bv", "bvs", "bgv", "bgvs", "bgvox", "backing", "backings",
                                                                  "backs", "backup", "backups", "background", "choir",
                                                                  "stack", "stacks", "gang", "oohs", "aahs" };
        constexpr std::array<std::string_view, 18> otherWords { "keys", "piano", "bass", "drums", "drum", "kick", "snare",
                                                                "hats", "synth", "pad", "pads", "strings", "organ", "beat",
                                                                "instrumental", "inst", "perc", "808" };
        constexpr std::array<std::string_view, 15> leadWords { "lead", "ld", "main", "vox", "vocal", "vocals", "voc", "vo",
                                                               "voice", "rap", "verse", "hook", "lv", "singer", "comp" };
    }

    std::string_view toName (Role r)
    {
        for (auto& n : roleNames)
            if (n.role == r)
                return n.name;
        return "lead";
    }

    std::string_view toLabel (Role r)
    {
        for (auto& n : roleNames)
            if (n.role == r)
                return n.label;
        return "Lead";
    }

    bool fromName (std::string_view s, Role& out)
    {
        for (auto& n : roleNames)
        {
            if (n.name == s)
            {
                out = n.role;
                return true;
            }
        }
        return false;
    }

    std::string_view toName (TakeSource s)
    {
        for (auto& [src, name] : sourceNames)
            if (src == s)
                return name;
        return "capture";
    }

    bool fromName (std::string_view s, TakeSource& out)
    {
        for (auto& [src, name] : sourceNames)
        {
            if (name == s)
            {
                out = src;
                return true;
            }
        }
        return false;
    }

    std::optional<Role> guessRole (std::string_view name)
    {
        const auto w = words (name);
        auto any = [&] (auto&& test) { return std::any_of (w.begin(), w.end(), test); };

        // Most specific first: "Lead Guitar" is a guitar, "Vox Dbl" is a double, "BV Harmony" a harmony.
        if (any ([] (const std::string& x) { return isOneOf (x, guitarWords); }))
            return Role::guitar;

        bool adlib = any ([] (const std::string& x) { return isOneOf (x, adlibWords); });
        for (size_t i = 0; i + 1 < w.size() && ! adlib; ++i)
            adlib = w[i] == "ad" && (w[i + 1] == "lib" || w[i + 1] == "libs");
        if (adlib)
            return Role::adlib;

        bool harmony = any ([] (const std::string& x) { return startsWith (x, "harm") || isOneOf (x, harmonyWords); });
        for (size_t i = 0; i + 1 < w.size() && ! harmony; ++i)
            harmony = (w[i] == "3" && w[i + 1] == "rd") || (w[i] == "5" && w[i + 1] == "th");
        if (harmony)
            return Role::harmony;
        if (any ([] (const std::string& x) { return isOneOf (x, doubleWords); }))
            return Role::doubled;
        if (any ([] (const std::string& x) { return isOneOf (x, backingWords); }))
            return Role::backing;
        if (any ([] (const std::string& x) { return isOneOf (x, otherWords); }))
            return Role::other;
        if (any ([] (const std::string& x) { return isOneOf (x, leadWords); }))
            return Role::lead;
        return std::nullopt;
    }

    double Take::lengthSeconds() const noexcept
    {
        return sampleRate > 0.0 ? (double) lengthSamples / sampleRate : 0.0;
    }

    double Take::lengthQuarters() const noexcept
    {
        return lengthSeconds() * clampBpm (start.bpm) / 60.0;
    }

    std::uint32_t TakeList::add (Take t)
    {
        t.id = nextId++;
        takes.push_back (std::move (t));
        return takes.back().id;
    }

    bool TakeList::remove (std::uint32_t id)
    {
        const auto it = std::find_if (takes.begin(), takes.end(), [id] (const Take& t) { return t.id == id; });
        if (it == takes.end())
            return false;
        takes.erase (it);
        return true;
    }

    Take* TakeList::find (std::uint32_t id)
    {
        for (auto& t : takes)
            if (t.id == id)
                return &t;
        return nullptr;
    }

    const Take* TakeList::find (std::uint32_t id) const
    {
        return const_cast<TakeList*> (this)->find (id);
    }

    std::vector<const Take*> TakeList::inSongOrder() const
    {
        std::vector<const Take*> out;
        for (auto& t : takes)
            out.push_back (&t);
        std::stable_sort (out.begin(), out.end(), [] (const Take* a, const Take* b) {
            if (a->start.ppq != b->start.ppq)
                return a->start.ppq < b->start.ppq;
            return a->id < b->id;
        });
        return out;
    }

    std::string TakeList::toText() const
    {
        using namespace text;
        std::string out = "compcaptain-takes=1\n";
        out += "folder=" + escape (folder) + "\n";
        out += "nextId=" + std::to_string (nextId) + "\n";
        out += "nextTakeNumber=" + std::to_string (nextTakeNumber) + "\n";
        for (auto& t : takes)
        {
            out += "take=" + std::to_string (t.id) + "\n";
            out += "name=" + escape (t.name) + "\n";
            out += "role=" + std::string (toName (t.role)) + "\n";
            out += std::string ("roleChosen=") + (t.roleChosen ? "1" : "0") + "\n";
            out += std::string ("favourite=") + (t.favourite ? "1" : "0") + "\n";
            out += "source=" + std::string (toName (t.source)) + "\n";
            out += "file=" + escape (t.file) + "\n";
            out += "originalFile=" + escape (t.originalFile) + "\n";
            out += "sampleRate=" + formatFixed (t.sampleRate, 3) + "\n";
            out += "channels=" + std::to_string (t.channels) + "\n";
            out += "lengthSamples=" + std::to_string (t.lengthSamples) + "\n";
            out += "ppq=" + formatFixed (t.start.ppq, 6) + "\n";
            out += "seconds=" + formatFixed (t.start.seconds, 6) + "\n";
            out += "bpm=" + formatFixed (t.start.bpm, 4) + "\n";
            out += "meter=" + t.start.meter.toText() + "\n";
            if (t.start.hasBarStart)
                out += "barStartPpq=" + formatFixed (t.start.barStartPpq, 6) + "\n";
            if (t.start.barNumber > 0)
                out += "barNumber=" + std::to_string (t.start.barNumber) + "\n";
            out += "track=" + escape (t.track) + "\n";
        }
        return out;
    }

    TakeList TakeList::fromText (std::string_view textIn)
    {
        using namespace text;
        TakeList list;
        Take* cur = nullptr;
        std::uint32_t highestId = 0;

        forEachKeyValue (textIn, [&] (std::string_view key, std::string_view value) {
            std::int64_t i = 0;
            double d = 0.0;
            if (key == "take")
            {
                if (! parseInt (value, i) || i <= 0 || i > 0x7fffffff)
                {
                    cur = nullptr;
                    return;
                }
                Take t;
                t.id = (std::uint32_t) i;
                highestId = std::max (highestId, t.id);
                list.takes.push_back (std::move (t));
                cur = &list.takes.back();
                return;
            }
            if (cur == nullptr)
            {
                if (key == "folder")
                    list.folder = unescape (value);
                else if (key == "nextId" && parseInt (value, i) && i > 0 && i < 0x7fffffff)
                    list.nextId = (std::uint32_t) i;
                else if (key == "nextTakeNumber" && parseInt (value, i) && i > 0 && i < 1000000)
                    list.nextTakeNumber = (int) i;
                return;
            }
            Take& t = *cur;
            if (key == "name") t.name = unescape (value);
            else if (key == "role") fromName (value, t.role);
            else if (key == "roleChosen") t.roleChosen = value == "1";
            else if (key == "favourite") t.favourite = value == "1";
            else if (key == "source") fromName (value, t.source);
            else if (key == "file") t.file = unescape (value);
            else if (key == "originalFile") t.originalFile = unescape (value);
            else if (key == "sampleRate" && parseDouble (value, d) && d > 0) t.sampleRate = d;
            else if (key == "channels" && parseInt (value, i) && i > 0 && i <= 64) t.channels = (int) i;
            else if (key == "lengthSamples" && parseInt (value, i) && i >= 0) t.lengthSamples = i;
            else if (key == "ppq" && parseDouble (value, d)) t.start.ppq = d;
            else if (key == "seconds" && parseDouble (value, d)) t.start.seconds = d;
            else if (key == "bpm" && parseDouble (value, d)) t.start.bpm = clampBpm (d);
            else if (key == "meter") t.start.meter = Meter::fromText (value);
            else if (key == "barStartPpq" && parseDouble (value, d)) { t.start.barStartPpq = d; t.start.hasBarStart = true; }
            else if (key == "barNumber" && parseInt (value, i) && i > 0 && i < 100000) t.start.barNumber = (int) i;
            else if (key == "track") t.track = unescape (value);
        });

        // Two takes with the same id (a damaged file): keep the first.
        std::vector<std::uint32_t> seen;
        list.takes.erase (std::remove_if (list.takes.begin(), list.takes.end(), [&] (const Take& t) {
            if (std::find (seen.begin(), seen.end(), t.id) != seen.end())
                return true;
            seen.push_back (t.id);
            return false;
        }), list.takes.end());

        list.nextId = std::max (list.nextId, highestId + 1);
        return list;
    }
}
