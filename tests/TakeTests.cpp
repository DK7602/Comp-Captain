// Takes: roles guessed from names, the take list, and saving it with the project.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cc/Take.h"

using Catch::Approx;
using cc::Role;

TEST_CASE ("Roles are guessed from track and file names")
{
    CHECK (cc::guessRole ("Lead Vox") == Role::lead);
    CHECK (cc::guessRole ("LeadVox_03") == Role::lead);
    CHECK (cc::guessRole ("Vocal") == Role::lead);
    CHECK (cc::guessRole ("Verse 2 Rap") == Role::lead);
    CHECK (cc::guessRole ("Vox Dbl L") == Role::doubled);
    CHECK (cc::guessRole ("Dbl2") == Role::doubled);
    CHECK (cc::guessRole ("Double Right") == Role::doubled);
    CHECK (cc::guessRole ("Harmony 3rd") == Role::harmony);
    CHECK (cc::guessRole ("Harm High") == Role::harmony);
    CHECK (cc::guessRole ("vox 3rd above") == Role::harmony);
    CHECK (cc::guessRole ("Adlibs") == Role::adlib);
    CHECK (cc::guessRole ("Ad-Libs") == Role::adlib);
    CHECK (cc::guessRole ("AdLib Hype") == Role::adlib);
    CHECK (cc::guessRole ("ad libs chorus") == Role::adlib);
    CHECK (cc::guessRole ("BV Stack") == Role::backing);
    CHECK (cc::guessRole ("BGVs") == Role::backing);
    CHECK (cc::guessRole ("Backing Vocals") == Role::backing);
    CHECK (cc::guessRole ("Choir") == Role::backing);
    CHECK (cc::guessRole ("Gtr Rhythm L") == Role::guitar);
    CHECK (cc::guessRole ("Lead Guitar") == Role::guitar);  // a guitar, not the lead vocal
    CHECK (cc::guessRole ("Acoustic") == Role::guitar);
    CHECK (cc::guessRole ("Piano") == Role::other);
    CHECK (cc::guessRole ("808") == Role::other);
    CHECK_FALSE (cc::guessRole ("Audio 01").has_value());
    CHECK_FALSE (cc::guessRole ("").has_value());
    CHECK_FALSE (cc::guessRole ("Badlibs").has_value());    // whole words only
    CHECK_FALSE (cc::guessRole ("Mixdown").has_value());
}

TEST_CASE ("Role names match the interface")
{
    for (auto r : cc::allRoles)
    {
        Role back = Role::other;
        CHECK (cc::fromName (cc::toName (r), back));
        CHECK (back == r);
    }
    CHECK (cc::toName (Role::doubled) == "double");
    CHECK (cc::toLabel (Role::adlib) == "Ad-lib");
    Role r = Role::lead;
    CHECK_FALSE (cc::fromName ("lead singer", r));
}

TEST_CASE ("The take list gives every take its own id")
{
    cc::TakeList list;
    cc::Take a;
    a.name = "Take 1";
    const auto idA = list.add (a);
    cc::Take b;
    b.name = "Take 2";
    const auto idB = list.add (b);
    CHECK (idA != idB);
    REQUIRE (list.find (idB) != nullptr);
    CHECK (list.find (idB)->name == "Take 2");
    CHECK (list.remove (idA));
    CHECK_FALSE (list.remove (idA));
    const auto idC = list.add (a);
    CHECK (idC != idA); // ids are never reused, even after a take is removed
}

TEST_CASE ("Takes are listed in song order")
{
    cc::TakeList list;
    cc::Take late, early, alsoEarly;
    late.start.ppq = 32;
    early.start.ppq = 16;
    alsoEarly.start.ppq = 16;
    late.name = "late";
    early.name = "early";
    alsoEarly.name = "also early";
    list.add (late);
    list.add (early);
    list.add (alsoEarly);
    const auto order = list.inSongOrder();
    REQUIRE (order.size() == 3);
    CHECK (order[0]->name == "early");
    CHECK (order[1]->name == "also early");
    CHECK (order[2]->name == "late");
}

TEST_CASE ("Lengths in seconds and in beats")
{
    cc::Take t;
    t.sampleRate = 48000;
    t.lengthSamples = 96000;
    t.start.bpm = 90;
    CHECK (t.lengthSeconds() == Approx (2.0));
    CHECK (t.lengthQuarters() == Approx (3.0));
}

TEST_CASE ("The take list saves and restores with the project")
{
    cc::TakeList list;
    list.folder = "C:\\Users\\Me\\Documents\\Comp Captain\\2026-11-23 Lead Vox (4f1c)";
    list.nextTakeNumber = 4;

    cc::Take t;
    t.name = "Take 3 \"keeper\"";
    t.role = Role::doubled;
    t.roleChosen = true;
    t.favourite = true;
    t.source = cc::TakeSource::file;
    t.file = "Take 3.wav";
    t.originalFile = "D:\\Old Session\\Vox Dbl 03.wav";
    t.sampleRate = 44100;
    t.channels = 2;
    t.lengthSamples = 1234567;
    t.start = cc::positionAtBar (9, 1.0, 92.0, { 6, 8 });
    t.track = "Vox Dbl\nL";
    list.add (t);
    cc::Take u;
    u.name = "Take 4";
    list.add (u);

    const auto restored = cc::TakeList::fromText (list.toText());
    REQUIRE (restored.size() == 2);
    const auto& r = restored.all()[0];
    CHECK (r.name == t.name);
    CHECK (r.role == Role::doubled);
    CHECK (r.roleChosen);
    CHECK (r.favourite);
    CHECK (r.source == cc::TakeSource::file);
    CHECK (r.file == t.file);
    CHECK (r.originalFile == t.originalFile);
    CHECK (r.sampleRate == Approx (44100));
    CHECK (r.channels == 2);
    CHECK (r.lengthSamples == 1234567);
    CHECK (r.start.ppq == Approx (t.start.ppq));
    CHECK (r.start.seconds == Approx (t.start.seconds));
    CHECK (r.start.bpm == Approx (92.0));
    CHECK (r.start.meter == cc::Meter { 6, 8 });
    CHECK (r.start.hasBarStart);
    CHECK (r.startBarBeat().bar == 9);
    CHECK (r.track == t.track);
    CHECK (restored.folder == list.folder);
    CHECK (restored.nextTakeNumber == 4);

    // ids carry on from where they were
    auto again = restored;
    const auto id = again.add ({});
    CHECK (id > restored.all()[1].id);
}

TEST_CASE ("Damaged or foreign take text doesn't break anything")
{
    CHECK (cc::TakeList::fromText ("").empty());
    CHECK (cc::TakeList::fromText ("theme=dark\npage=comp\n").empty());
    const auto list = cc::TakeList::fromText ("take=5\nname=A\ntake=5\nname=B\ntake=oops\nname=C\nrole=nonsense\n");
    REQUIRE (list.size() == 1);
    CHECK (list.all()[0].name == "A");
    CHECK (list.all()[0].role == Role::lead);
}
