#include "PluginEditor.h"

#include "AudioImport.h"

#include "cc/Overview.h"
#include "cc/SuiteDetector.h"
#include "cc/TakesFolder.h"
#include "cc/TextFile.h"

namespace
{
    const juce::Identifier eventId { "cc" };
    const juce::Colour backgroundNavy { 0xff040b1c };

    juce::String hostDescription (juce::AudioProcessor::WrapperType wrapper)
    {
        const juce::PluginHostType host;
        juce::String format = juce::AudioProcessor::getWrapperTypeDescription (wrapper);
        if (wrapper == juce::AudioProcessor::wrapperType_Standalone)
            return "Standalone app";
        return juce::String (host.getHostDescription()) + " (" + format + ")";
    }

    juce::String str (std::string_view s) { return juce::String::fromUTF8 (s.data(), (int) s.size()); }

    juce::var takeVar (const cc::Take& t, const juce::String& id, bool missing, bool hasPeaks)
    {
        auto* o = new juce::DynamicObject();
        const auto bb = t.startBarBeat();
        o->setProperty ("id", id);
        o->setProperty ("name", str (t.name));
        o->setProperty ("role", str (cc::toName (t.role)));
        o->setProperty ("roleChosen", t.roleChosen);
        o->setProperty ("fav", t.favourite);
        o->setProperty ("source", str (cc::toName (t.source)));
        o->setProperty ("bar", bb.bar);
        o->setProperty ("beat", bb.beat);
        o->setProperty ("barText", str (bb.toText()));
        o->setProperty ("ppq", t.start.ppq);
        o->setProperty ("lenQ", t.lengthQuarters());
        o->setProperty ("startSec", t.start.seconds);
        o->setProperty ("lenSec", t.lengthSeconds());
        o->setProperty ("bpm", t.start.bpm);
        o->setProperty ("meter", str (t.start.meter.toText()));
        o->setProperty ("rate", t.sampleRate);
        o->setProperty ("channels", t.channels);
        o->setProperty ("track", str (t.track));
        o->setProperty ("file", str (t.file));
        o->setProperty ("original", str (t.originalFile));
        o->setProperty ("missing", missing);
        o->setProperty ("hasPeaks", hasPeaks);
        return juce::var (o);
    }

    std::optional<std::uint32_t> ownId (const juce::String& id)
    {
        if (! id.startsWithChar ('t'))
            return std::nullopt;
        const auto n = id.substring (1).getLargeIntValue();
        if (n <= 0)
            return std::nullopt;
        return (std::uint32_t) n;
    }
}

CompCaptainEditor::CompCaptainEditor (CompCaptainProcessor& p)
    : AudioProcessorEditor (p),
#if JucePlugin_Enable_ARA
      AudioProcessorEditorARAExtension (&p),
#endif
      processor (p),
      browser (makeOptions())
{
    addAndMakeVisible (browser);

    const auto state = processor.getUiState();
    setResizable (true, true);
    applySizeRules();
    setSize (state.windowWidth(), state.windowHeight());

    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (30);
}

CompCaptainEditor::~CompCaptainEditor()
{
    stopTimer();
    // hearing takes and the comp only while the window is open: back to the track as it is
    processor.stopHearing();
    if (viewing != 0 && viewing != processor.instanceId())
        CompCaptainProcessor::withTrack (viewing, [] (CompCaptainProcessor& p) { p.stopHearing(); });
    for (auto& [id, u] : uploads)
    {
        u.out.reset();
        u.file.deleteFile();
    }
}

juce::WebBrowserComponent::Options CompCaptainEditor::makeOptions()
{
    using Options = juce::WebBrowserComponent::Options;
    return Options {}
        .withBackend (Options::Backend::webview2)
        .withWinWebView2Options (Options::WinWebView2 {}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CompCaptainWebView2"))
                                     .withBackgroundColour (backgroundNavy)
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withEventListener (eventId, [this] (const juce::var& message) { handleUiEvent (message); })
        .withResourceProvider ([this] (const juce::String& url) { return resources.get (url); });
}

void CompCaptainEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundNavy); // shows for a moment while the interface loads
}

void CompCaptainEditor::applySizeRules()
{
#if JucePlugin_Enable_ARA
    embedded = isARAEditorView();
#endif
    if (embedded)
    {
        // any shape the DAW gives us (the page letterboxes, or scrolls when it would be too small to read)
        if (auto* c = getConstrainer())
            c->setFixedAspectRatio (0.0);
        setResizeLimits (320, 180, 4000, 3000);
        return;
    }
    setResizeLimits (cc::UiState::artWidth / 2, (int) std::lround (cc::UiState::artHeight * cc::UiState::minScale),
                     (int) std::lround (cc::UiState::artWidth * cc::UiState::maxScale), (int) std::lround (cc::UiState::artHeight * cc::UiState::maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) cc::UiState::artWidth / (double) cc::UiState::artHeight);
}

void CompCaptainEditor::resized()
{
    browser.setBounds (getLocalBounds());
    if (embedded)
        return; // the DAW decides an Extension's size: nothing to remember
    const auto scale = cc::clampScale ((double) getWidth() / (double) cc::UiState::artWidth);
    updateState ([scale] (cc::UiState& s) { s.windowScale = scale; });
}

void CompCaptainEditor::updateState (const std::function<void (cc::UiState&)>& change)
{
    auto s = processor.getUiState();
    change (s);
    processor.setUiState (s);
}

void CompCaptainEditor::send (const juce::String& type, juce::DynamicObject* data)
{
    data->setProperty ("type", type);
    browser.emitEventIfBrowserIsVisible (eventId, juce::var (data));
}

template <typename Fn>
bool CompCaptainEditor::withViewed (Fn&& fn)
{
    if (viewing == 0 || viewing == processor.instanceId())
    {
        fn (processor);
        return true;
    }
    if (CompCaptainProcessor::withTrack (viewing, fn))
        return true;
    viewing = 0; // that track's Comp Captain was removed: back to this one
    fn (processor);
    return true;
}

// ============================================================================ from the interface

void CompCaptainEditor::handleUiEvent (const juce::var& message)
{
    const auto type = message.getProperty ("type", {}).toString();
    const auto value = message.getProperty ("value", {}).toString().toStdString();

    if (type == "ready")
    {
        uiReady = true;
        sentTakesVersion = sentAraVersion = sentHearingVersion = 0;
        sentSongVersion = 0;
        sentTracks = sentTransport = sentCapture = sentListening = sentScores = {};
        liveSerial = 0;
        liveSent = 0;
        sendInit();
    }
    else if (type == "theme")
        updateState ([&] (cc::UiState& s) { cc::fromName (value, s.theme); });
    else if (type == "page")
        updateState ([&] (cc::UiState& s) { cc::fromName (value, s.page); });
    else if (type == "compView")
        updateState ([&] (cc::UiState& s) { cc::fromName (value, s.compView); });
    else if (type == "size")
    {
        const auto scale = cc::clampScale ((double) message.getProperty ("scale", 0.8));
        setSize ((int) std::lround (cc::UiState::artWidth * scale), (int) std::lround (cc::UiState::artHeight * scale));
    }
    else if (type == "arm")
    {
        processor.setArmed ((bool) message.getProperty ("on", false));
        sendCapture (true);
    }
    else if (type == "loopTakes")
    {
        processor.setLoopTakes ((bool) message.getProperty ("on", true));
        sendCapture (true);
    }
    else if (type == "take")
        takeAction (message);
    else if (type == "getPeaks")
        sendPeaks (message.getProperty ("ids", {}));
    else if (type == "viewTrack")
    {
        const auto id = (juce::uint64) message.getProperty ("id", 0).toString().getLargeIntValue();
        // stop hearing the track we're leaving
        withViewed ([] (CompCaptainProcessor& p) { p.stopHearing(); });
        viewing = id == processor.instanceId() ? 0 : id;
        sentTakesVersion = 0;
        sendTakes();
        sendTracks (true);
        sendScores (true);
        sendAudition (true);
        sendBuilt (true);
    }
    else if (type == "trackRole")
    {
        cc::Role r = cc::Role::lead;
        if (cc::fromName (value, r))
            withViewed ([r] (CompCaptainProcessor& p) { p.setTrackRole (r); });
        sendTracks (true);
    }
    else if (type == "addFiles")
        chooseFiles();
    else if (type == "importChosen")
        importChosen (message);
    else if (type == "dropBegin")
        uploadBegin (message);
    else if (type == "dropChunk")
        uploadChunk (message);
    else if (type == "dropEnd")
        uploadEnd (message);
    else if (type == "dropCancel")
        uploadCancel (message);
    else if (type == "openFolder")
    {
        juce::File folder;
        withViewed ([&] (CompCaptainProcessor& p) { folder = p.takesFolder(); });
        folder.createDirectory();
        folder.startAsProcess();
    }
    else if (type == "lyricsSet")
    {
        CompCaptainProcessor::setSongLyrics (message.getProperty ("text", {}).toString().substring (0, 200000));
        sendLyrics (false);
    }
    else if (type == "lyricsOpen")
        openLyricsFile (message.getProperty ("from", {}).toString() == "songrider");
    else if (type == "lyricsFile")
    {
        const auto data = cc::fromBase64 (message.getProperty ("data", {}).toString().toStdString());
        lyricsFromFile (message.getProperty ("name", {}).toString(), juce::MemoryBlock (data.data(), data.size()));
    }
    else if (type == "hearIt")
        hearIt (message);
    else if (type == "pick")
    {
        // your own pick for a line ("" = none: Comp Captain's best)
        const int line = (int) message.getProperty ("line", -1);
        const auto take = ownId (message.getProperty ("id", {}).toString());
        const auto text = message.getProperty ("text", {}).toString().toStdString();
        if (line >= 0)
            withViewed ([&] (CompCaptainProcessor& p) { p.setPick (line, take.value_or (0), text); });
        sendScores (true);
    }
    else if (type == "picksClear")
    {
        withViewed ([] (CompCaptainProcessor& p) { p.clearPicks(); });
        sendScores (true);
    }
    else if (type == "picksSet")
    {
        std::map<int, CompCaptainProcessor::Pick> all;
        if (auto* o = message.getProperty ("picks", {}).getDynamicObject())
            for (auto& prop : o->getProperties())
            {
                const int line = prop.name.toString().getIntValue();
                const auto take = ownId (prop.value.getProperty ("id", {}).toString());
                if (line >= 0 && take)
                    all[line] = { *take, prop.value.getProperty ("text", {}).toString().toStdString() };
            }
        withViewed ([&] (CompCaptainProcessor& p) { p.setPicks (all); });
        sendScores (true);
    }
    else if (type == "comp")
    {
        // which take sings each line (worked out by the window), and how picky
        std::vector<CompCaptainProcessor::CompLine> lines;
        if (const auto* list = message.getProperty ("lines", {}).getArray())
            for (auto& l : *list)
                if (const auto take = ownId (l.getProperty ("id", {}).toString()))
                    lines.push_back ({ (int) l.getProperty ("line", -1), *take });
        const double howPicky = (double) message.getProperty ("picky", 0.7);
        withViewed ([&] (CompCaptainProcessor& p) { p.setComp (lines, howPicky); });
    }
    else if (type == "hear")
    {
        const auto what = message.getProperty ("what", {}).toString();
        const auto take = ownId (message.getProperty ("id", {}).toString()).value_or (0);
        const int line = (int) message.getProperty ("line", -1);
        const bool inTime = (bool) message.getProperty ("inTime", false);
        if (viewing != 0 && viewing != processor.instanceId())
            processor.stopHearing(); // only one track heard at a time
        withViewed ([&] (CompCaptainProcessor& p) {
            if (inTime)
                p.hearInTime (what == "take" ? take : 0);
            else if (what == "take" && take != 0)
                p.hearTakeLine (take, line);
            else if (what == "comp")
                p.hearComp (line);
        });
        sendAudition (true);
    }
    else if (type == "hearStop")
    {
        processor.stopHearing();
        withViewed ([] (CompCaptainProcessor& p) { p.stopHearing(); });
        sendAudition (true);
    }
    else if (type == "build")
    {
        withViewed ([] (CompCaptainProcessor& p) { p.buildComp(); });
        sendBuilt (true);
    }
    else if (type == "dragOut" || type == "reveal")
    {
        // only a file Comp Captain made, in its own folder
        const juce::File file (message.getProperty ("file", {}).toString());
        if (file.existsAsFile() && file.isAChildOf (CompCaptainProcessor::takesRoot()))
        {
            if (type == "reveal")
                file.revealToUser();
            else
                juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
        }
    }
    else if (type == "style")
    {
        const auto w = message.getProperty ("weights", {});
        cc::ScoreWeights weights;
        if (w.isArray() && w.size() == 4)
            weights = { (double) w[0], (double) w[1], (double) w[2], (double) w[3] };
        else if (const auto preset = cc::styleWeights (message.getProperty ("name", {}).toString().toStdString()))
            weights = *preset;
        const auto name = message.getProperty ("name", "My Own").toString().toStdString();
        withViewed ([&] (CompCaptainProcessor& p) { p.setStyle (name, weights); });
        sendScores (true);
    }
    else if (type == "listenAgain")
    {
        withViewed ([] (CompCaptainProcessor& p) { p.listenAgain(); });
        sendListening (true);
    }
    else if (type == "getListenReport" || type == "copyListenReport")
    {
        const auto text = listeningReport();
        if (type == "copyListenReport")
            juce::SystemClipboard::copyTextToClipboard (text);
        auto* r = new juce::DynamicObject();
        r->setProperty ("text", text);
        r->setProperty ("copied", type == "copyListenReport");
        send ("report", r);
    }
    else if (type == "getReport" || type == "copyReport")
    {
        juce::String text;
#if JucePlugin_Enable_ARA
        if (auto* doc = processor.araDocument())
            text = doc->report (processor.araRegionsPlayedHere(), processor.araRoles());
#endif
        if (text.isEmpty())
            text = "This copy of Comp Captain isn't linked to ARA: it's an insert effect on its track.\n"
                   "To try ARA in Cubase: select the takes on a track's lanes, then Audio > Extensions > Comp Captain.\n"
                   "Host: " + hostDescription (processor.wrapperType) + "\n";
        if (type == "copyReport")
            juce::SystemClipboard::copyTextToClipboard (text);
        auto* r = new juce::DynamicObject();
        r->setProperty ("text", text);
        r->setProperty ("copied", type == "copyReport");
        send ("report", r);
    }
}

void CompCaptainEditor::takeAction (const juce::var& message)
{
    const auto op = message.getProperty ("op", {}).toString();
    const auto id = message.getProperty ("id", {}).toString();
    const auto value = message.getProperty ("value", {});

#if JucePlugin_Enable_ARA
    if (id.startsWithChar ('a') && viewing == 0)
    {
        if (auto* doc = processor.araDocument())
        {
            cc::Role r = cc::Role::lead;
            if (op == "role" && cc::fromName (value.toString().toStdString(), r))
                doc->setRole (id, r);
            else if (op == "fav")
                doc->setFavourite (id, (bool) value);
        }
        sentAraVersion = 0;
        sendTakes();
        return;
    }
#endif

    const auto own = ownId (id);
    if (! own)
        return;
    withViewed ([&] (CompCaptainProcessor& p) {
        cc::Role r = cc::Role::lead;
        if (op == "rename")
            p.renameTake (*own, value.toString());
        else if (op == "role" && cc::fromName (value.toString().toStdString(), r))
            p.setTakeRole (*own, r);
        else if (op == "fav")
            p.setTakeFavourite (*own, (bool) value);
        else if (op == "remove")
            p.removeTake (*own);
    });
    sendTakes();
}

// ============================================================================ to the interface

void CompCaptainEditor::sendInit()
{
    const auto state = processor.getUiState();
    const auto suite = cc::detectSuiteOnThisComputer();

    auto* sonicology = new juce::DynamicObject();
    sonicology->setProperty ("voxology", str (cc::toName (suite.voxology)));
    sonicology->setProperty ("mixology", str (cc::toName (suite.mixology)));

    auto* init = new juce::DynamicObject();
    init->setProperty ("theme", str (cc::toName (state.theme)));
    init->setProperty ("page", str (cc::toName (state.page)));
    init->setProperty ("compView", str (cc::toName (state.compView)));
    init->setProperty ("version", JucePlugin_VersionString);
    init->setProperty ("host", hostDescription (processor.wrapperType));
    init->setProperty ("sonicology", juce::var (sonicology));
    init->setProperty ("ara", processor.isAra());
    init->setProperty ("standalone", processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone);
    init->setProperty ("takesRoot", CompCaptainProcessor::takesRoot().getFullPathName());
    send ("init", init);

    sendTracks (true);
    sendTakes();
    sendTransport (true);
    sendCapture (true);
    sendLyrics (true);
    sendListening (true);
    sendScores (true);
    sendAudition (true);
    sendBuilt (true);
}

void CompCaptainEditor::sendTakes()
{
    juce::Array<juce::var> list;
    juce::String trackName, folder;
    cc::Role role = cc::Role::lead;
    juce::uint32 version = 0;

    juce::uint32 hearingVersion = 0;
    withViewed ([&] (CompCaptainProcessor& p) {
        const auto takes = p.getTakes();
        for (const auto* t : takes.inSongOrder())
        {
            auto v = takeVar (*t, "t" + juce::String (t->id), p.takeFileMissing (t->id), p.takeOverview (t->id).has_value());
            if (auto* o = v.getDynamicObject())
            {
                // what listening found: its state, how many words, and the sung phrases (take seconds)
                o->setProperty ("heard", p.takeHearingState (t->id));
                if (const auto h = p.takeHearing (t->id))
                {
                    o->setProperty ("words", (int) h->words.size());
                    juce::Array<juce::var> phrases;
                    for (auto& ph : h->phrases)
                        phrases.add (juce::Array<juce::var> { std::round (ph.start * 100.0) / 100.0, std::round (ph.end * 100.0) / 100.0 });
                    o->setProperty ("phrases", phrases);
                }
            }
            list.add (v);
        }
        trackName = p.trackName();
        role = p.trackRole();
        folder = juce::String::fromUTF8 (takes.folder.c_str());
        version = p.takesVersion();
        hearingVersion = p.hearingVersion();
    });

    juce::uint32 araVersion = 0;
#if JucePlugin_Enable_ARA
    if (viewing == 0)
    {
        if (auto* doc = processor.araDocument())
        {
            araVersion = doc->version();
            const auto mine = processor.araRegionsPlayedHere();
            for (auto& a : doc->takes())
            {
                auto v = takeVar (a.take, a.key, false, doc->peaks (a.key) != nullptr);
                if (auto* o = v.getDynamicObject())
                {
                    o->setProperty ("here", std::find (mine.begin(), mine.end(), a.region) != mine.end());
                    o->setProperty ("laneTrack", a.trackName);
                    o->setProperty ("readable", a.samplesReady);
                }
                list.add (v);
            }
        }
    }
#endif

    sentTakesVersion = version;
    sentAraVersion = araVersion;
    sentHearingVersion = hearingVersion;

    auto* msg = new juce::DynamicObject();
    msg->setProperty ("takes", list);
    msg->setProperty ("track", viewing == 0 ? juce::var ("self") : juce::var (juce::String (viewing)));
    msg->setProperty ("trackName", trackName);
    msg->setProperty ("trackRole", str (cc::toName (role)));
    msg->setProperty ("folder", folder);
    msg->setProperty ("importing", processor.importsWaiting());
    send ("takes", msg);
}

void CompCaptainEditor::sendPeaks (const juce::var& ids)
{
    if (! ids.isArray())
        return;
    for (auto& idVar : *ids.getArray())
    {
        const auto id = idVar.toString();
        std::optional<std::vector<std::uint8_t>> peaks;
#if JucePlugin_Enable_ARA
        if (id.startsWithChar ('a') && viewing == 0)
            if (auto* doc = processor.araDocument())
                if (auto* p = doc->peaks (id))
                    peaks = *p;
#endif
        if (auto own = ownId (id))
            withViewed ([&] (CompCaptainProcessor& p) { peaks = p.takeOverview (*own); });
        if (! peaks)
            continue;
        auto* msg = new juce::DynamicObject();
        msg->setProperty ("id", id);
        msg->setProperty ("perSecond", cc::OverviewBuilder::defaultSlicesPerSecond);
        msg->setProperty ("data", juce::String (cc::toBase64 (peaks->data(), peaks->size())));
        send ("peaks", msg);
    }
}

void CompCaptainEditor::sendTracks (bool force)
{
    const auto tracks = CompCaptainProcessor::allTracks();
    juce::String signature = juce::String (viewing) + ";";
    for (auto& t : tracks)
        signature << (juce::int64) t.id << "|" << t.name << "|" << (int) t.role << "|" << t.takes << "|" << (int) t.ara << ";";
    if (! force && signature == sentTracks)
        return;
    sentTracks = signature;

    juce::Array<juce::var> list;
    for (auto& t : tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", juce::String (t.id));
        o->setProperty ("name", t.name);
        o->setProperty ("role", str (cc::toName (t.role)));
        o->setProperty ("takes", t.takes);
        o->setProperty ("ara", t.ara);
        o->setProperty ("self", t.id == processor.instanceId());
        o->setProperty ("viewing", viewing == 0 ? t.id == processor.instanceId() : t.id == viewing);
        list.add (juce::var (o));
    }
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("tracks", list);
    send ("tracks", msg);
}

void CompCaptainEditor::sendTransport (bool force)
{
    const auto t = processor.transport();
    const auto signature = juce::String ((int) t.known) + juce::String ((int) t.playing) + juce::String ((int) t.recording) + "|" + juce::String (t.ppq, 3)
                           + "|" + juce::String (t.bpm, 2) + "|" + t.meter.toText();
    if (! force && signature == sentTransport)
        return;
    sentTransport = signature;
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("known", t.known);
    msg->setProperty ("playing", t.playing);
    msg->setProperty ("recording", t.recording);
    msg->setProperty ("ppq", t.ppq);
    msg->setProperty ("bpm", t.bpm);
    msg->setProperty ("num", t.meter.numerator);
    msg->setProperty ("den", t.meter.denominator);
    msg->setProperty ("seconds", t.seconds);
    send ("transport", msg);
}

void CompCaptainEditor::sendCapture (bool force)
{
    auto live = processor.liveTake (liveSent);
    if (live.serial != liveSerial)
    {
        // a new take: its waveform starts from the beginning
        liveSerial = live.serial;
        liveSent = 0;
        live = processor.liveTake (0);
    }
    const bool armed = processor.isArmed();
    const bool capturing = processor.isCapturing() || live.recording;
    const auto signature = juce::String ((int) armed) + juce::String ((int) capturing) + juce::String ((int) processor.loopTakes()) + "|" + juce::String (live.name)
                           + "|" + juce::String ((juce::int64) live.frames / 4800);
    const bool newPeaks = live.recording && live.peaksFrom + live.peaks.size() > liveSent;
    if (! force && signature == sentCapture && ! newPeaks)
        return;
    sentCapture = signature;

    auto* msg = new juce::DynamicObject();
    msg->setProperty ("armed", armed);
    msg->setProperty ("recording", capturing);
    msg->setProperty ("loopTakes", processor.loopTakes());
    msg->setProperty ("ara", processor.isAra());
    if (live.recording)
    {
        const auto bb = cc::barBeatAt (live.start);
        auto* l = new juce::DynamicObject();
        l->setProperty ("serial", (int) live.serial);
        l->setProperty ("name", str (live.name));
        l->setProperty ("ppq", live.start.ppq);
        l->setProperty ("bpm", live.start.bpm);
        l->setProperty ("bar", bb.bar);
        l->setProperty ("barText", str (bb.toText()));
        l->setProperty ("seconds", live.sampleRate > 0 ? (double) live.frames / live.sampleRate : 0.0);
        // only the new part of the waveform; the window keeps the rest
        const size_t from = std::max (liveSent, live.peaksFrom);
        const size_t skip = from - live.peaksFrom;
        l->setProperty ("from", (int) from);
        l->setProperty ("perSecond", cc::OverviewBuilder::defaultSlicesPerSecond);
        if (skip < live.peaks.size())
            l->setProperty ("data", juce::String (cc::toBase64 (live.peaks.data() + skip, live.peaks.size() - skip)));
        liveSent = live.peaksFrom + live.peaks.size();
        msg->setProperty ("live", juce::var (l));
    }
    send ("capture", msg);
}

void CompCaptainEditor::sendNews()
{
    const auto news = processor.takeNews();
    if (news.empty())
        return;
    juce::Array<juce::var> items;
    for (auto& n : news)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", n.kind);
        o->setProperty ("text", n.text);
        items.add (juce::var (o));
    }
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("items", items);
    send ("news", msg);
}

void CompCaptainEditor::timerCallback()
{
    const float peak = processor.getMeter().readAndReset();
    if (! uiReady)
        return;

    // Only talk to the interface when there's sound (or to settle back to silence),
    // so an idle window costs nothing in a big session.
    constexpr float quiet = 0.0005f;
    if (peak >= quiet || lastSentPeak >= quiet)
    {
        auto* msg = new juce::DynamicObject();
        msg->setProperty ("peak", peak);
        send ("level", msg);
        lastSentPeak = peak;
    }

    if (++tick % 3 != 0)
        return; // the rest ten times a second

#if JucePlugin_Enable_ARA
    // the DAW may make this copy an ARA editor after the window opened
    if (isARAEditorView() != embedded)
        applySizeRules();
#endif

    sendCapture (false);
    sendTransport (false);
    sendNews();

    juce::uint32 version = 0, hearingVersion = 0;
    withViewed ([&] (CompCaptainProcessor& p) {
        version = p.takesVersion();
        hearingVersion = p.hearingVersion();
    });
    juce::uint32 araVersion = 0;
#if JucePlugin_Enable_ARA
    if (auto* doc = processor.araDocument())
        araVersion = doc->version();
#endif
    if (version != sentTakesVersion || araVersion != sentAraVersion || hearingVersion != sentHearingVersion)
        sendTakes();
    sendLyrics (false);
    sendScores (false);
    sendAudition (false);
    sendBuilt (false);
    if (tick % 3 == 0)
        sendListening (false);
    if (tick % 15 == 0)
        sendTracks (false);
}

// ============================================================================ lyrics and listening

void CompCaptainEditor::sendLyrics (bool force)
{
    const auto song = CompCaptainProcessor::song();
    if (! force && song->version == sentSongVersion)
        return;
    sentSongVersion = song->version;

    const auto round = [] (double v) { return std::round (v * 1000.0) / 1000.0; };
    juce::Array<juce::var> sections, lines;
    for (auto& sec : song->lyrics.sections)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", str (sec.name));
        o->setProperty ("first", sec.firstLine);
        o->setProperty ("count", sec.lineCount);
        sections.add (juce::var (o));
    }
    for (size_t i = 0; i < song->lyrics.lines.size(); ++i)
    {
        const auto& line = song->lyrics.lines[i];
        auto* o = new juce::DynamicObject();
        o->setProperty ("text", str (line.text));
        o->setProperty ("section", line.section);
        if (i < song->lineUp.lines.size())
        {
            const auto& l = song->lineUp.lines[i];
            o->setProperty ("start", round (l.start));
            o->setProperty ("end", round (l.end));
            o->setProperty ("startQ", round (l.startPpq));
            o->setProperty ("endQ", round (l.endPpq));
            o->setProperty ("sure", std::round (l.sure * 100.0f) / 100.0);
            o->setProperty ("takes", l.takes);
            if (const auto name = song->takeNames.find (l.from); name != song->takeNames.end())
                o->setProperty ("from", name->second);
            juce::Array<juce::var> words;
            for (auto& w : l.words)
                words.add (juce::Array<juce::var> { round (w.start), round (w.end), w.heard ? 1 : 0 });
            o->setProperty ("words", words);
        }
        lines.add (juce::var (o));
    }
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("text", song->text);
    msg->setProperty ("sections", sections);
    msg->setProperty ("lines", lines);
    msg->setProperty ("linedUp", song->lineUp.takesLinedUp);
    msg->setProperty ("sungTakes", song->sungTakes);
    msg->setProperty ("heardTakes", song->heardTakes);
    juce::Array<juce::var> unmatched;
    for (auto& u : song->unmatched)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("take", u.take);
        o->setProperty ("words", u.words);
        o->setProperty ("heard", u.heard);
        unmatched.add (juce::var (o));
    }
    msg->setProperty ("unmatched", unmatched);
    send ("lyrics", msg);
}

void CompCaptainEditor::sendListening (bool force)
{
    const auto st = CompCaptainProcessor::listeningStatus();
    const auto signature = st.now + "|" + juce::String (st.percent) + "|" + juce::String (st.waiting) + "|"
                           + juce::String ((int) st.pausedForRecording) + "|" + st.problem;
    if (! force && signature == sentListening)
        return;
    sentListening = signature;
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("now", st.now);
    msg->setProperty ("percent", st.percent);
    msg->setProperty ("waiting", st.waiting);
    msg->setProperty ("paused", st.pausedForRecording);
    msg->setProperty ("problem", st.problem);
    send ("listening", msg);
}

void CompCaptainEditor::openLyricsFile (bool songRider)
{
    // Song Rider Pro's Export button saves "<Song> - Full Preview (Print).html", usually in Downloads.
    const auto downloads = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    chooser = songRider ? std::make_unique<juce::FileChooser> ("Open a song exported from Song Rider Pro (\"... Full Preview (Print).html\")",
                                                               downloads.isDirectory() ? downloads : juce::File(), "*.html;*.htm")
                        : std::make_unique<juce::FileChooser> ("Open lyrics (.txt, .lrc, Word .docx, or a Song Rider Pro page)", juce::File(),
                                                               "*.txt;*.lrc;*.docx;*.text;*.html;*.htm");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        juce::MemoryBlock bytes;
        if (file.getSize() > 4 * 1024 * 1024 || ! file.loadFileAsData (bytes))
        {
            auto* msg = new juce::DynamicObject();
            msg->setProperty ("error", "That file couldn't be opened as lyrics.");
            send ("lyricsLoaded", msg);
            return;
        }
        lyricsFromFile (file.getFileName(), bytes);
    });
}

void CompCaptainEditor::lyricsFromFile (const juce::String& name, const juce::MemoryBlock& bytes)
{
    juce::String text, error;
    const auto ext = name.fromLastOccurrenceOf (".", false, false).toLowerCase();
    if (bytes.getSize() > 4 * 1024 * 1024)
        error = "That file is too big to be lyrics.";
    else if (ext == "docx")
    {
        // a Word file is a zip; the words are in word/document.xml
        juce::MemoryInputStream in (bytes, false);
        juce::ZipFile zip (in);
        if (const auto* entry = zip.getEntry ("word/document.xml"))
        {
            std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (*entry));
            juce::MemoryBlock xml;
            if (stream != nullptr)
                stream->readIntoMemoryBlock (xml);
            text = str (cc::textFromWordXml (std::string_view (static_cast<const char*> (xml.getData()), xml.getSize())));
        }
        else
            error = "That Word file couldn't be read. Open it in Word, copy the lyrics, and use Paste.";
    }
    else if (ext == "html" || ext == "htm")
    {
        const auto page = cc::textFromBytes (std::string_view (static_cast<const char*> (bytes.getData()), bytes.getSize()));
        text = str (cc::textFromSongRiderPage (page));
    }
    else if (ext == "doc")
        error = "Older Word files (.doc) can't be read. In Word, use Save As > .docx, or copy the lyrics and use Paste.";
    else if (ext == "pdf" || ext == "rtf" || ext == "pages")
        error = "That kind of file can't be read. Open it, copy the lyrics, and use Paste.";
    else
        text = str (cc::textFromBytes (std::string_view (static_cast<const char*> (bytes.getData()), bytes.getSize())));

    if (error.isEmpty() && text.trim().isEmpty())
        error = "There were no words in " + name + ".";
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("name", name);
    if (error.isNotEmpty())
        msg->setProperty ("error", error);
    else
    {
        CompCaptainProcessor::setSongLyrics (text.trim() + "\n");
        msg->setProperty ("text", text.trim() + "\n");
    }
    send ("lyricsLoaded", msg);
    sendLyrics (false);
}

// The scores for the viewed track: every line, every take that sang it (four scores and the reasons), your picks,
// and the style. The window works out the totals and the best take (so moving a style slider is instant).
void CompCaptainEditor::sendScores (bool force)
{
    std::shared_ptr<const CompCaptainProcessor::Scores> scores;
    std::map<int, CompCaptainProcessor::Pick> picks;
    std::string style;
    cc::ScoreWeights weights;
    juce::uint32 version = 0;
    bool ready = false;
    juce::uint64 track = 0;
    withViewed ([&] (CompCaptainProcessor& p) {
        version = p.scoresVersion();
        scores = p.scores();
        picks = p.picks();
        style = p.styleName();
        weights = p.styleWeights();
        ready = p.scoresReady();
        track = p.instanceId();
    });
    const auto signature = juce::String ((juce::int64) track) + "|" + juce::String ((int) version) + "|" + juce::String ((int) ready);
    if (! force && signature == sentScores)
        return;
    sentScores = signature;

    const auto list = [] (const std::vector<std::string>& v) {
        juce::Array<juce::var> a;
        for (auto& s : v)
            a.add (str (s));
        return a;
    };
    juce::Array<juce::var> lines;
    if (scores != nullptr)
    {
        for (auto& line : scores->lines)
        {
            if (line.takes.empty())
                continue;
            juce::Array<juce::var> takes;
            for (auto& s : line.takes)
            {
                const auto take = scores->takeOf.find (s.take);
                if (take == scores->takeOf.end())
                    continue;
                auto* o = new juce::DynamicObject();
                o->setProperty ("id", "t" + juce::String (take->second));
                o->setProperty ("p", std::round (s.pitch));
                o->setProperty ("t", std::round (s.timing));
                o->setProperty ("c", std::round (s.clarity));
                o->setProperty ("e", std::round (s.energy));
                o->setProperty ("good", list (s.good));
                o->setProperty ("bad", list (s.bad));
                o->setProperty ("problems", list (s.problems));
                takes.add (juce::var (o));
            }
            auto* o = new juce::DynamicObject();
            o->setProperty ("line", line.line);
            o->setProperty ("takes", takes);
            lines.add (juce::var (o));
        }
    }
    auto* pickObj = new juce::DynamicObject();
    for (auto& [line, pick] : picks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", "t" + juce::String (pick.take));
        o->setProperty ("text", str (pick.lineText));
        pickObj->setProperty (juce::Identifier (juce::String (line)), juce::var (o));
    }
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("lines", lines);
    msg->setProperty ("picks", juce::var (pickObj));
    msg->setProperty ("style", str (style));
    msg->setProperty ("weights", juce::Array<juce::var> { weights.pitch, weights.timing, weights.clarity, weights.energy });
    msg->setProperty ("ready", ready);
    msg->setProperty ("any", scores != nullptr);
    double howPicky = 0.7;
    withViewed ([&] (CompCaptainProcessor& p) { howPicky = p.compPicky(); });
    msg->setProperty ("picky", howPicky);
    send ("scores", msg);
}

// A plain report of the lyrics, the listening and where each line landed, to paste into the chat when the
// words don't follow the song.
juce::String CompCaptainEditor::listeningReport()
{
    juce::String r;
    const auto nl = juce::String ("\n");
    r << "Comp Captain " << JucePlugin_VersionString << ": lyrics and listening report" << nl
      << "Host: " << hostDescription (processor.wrapperType) << nl;

    const auto model = Listening::findModel();
    r << "Speech model: " << (model == juce::File() ? juce::String ("NOT FOUND") : model.getFullPathName()) << nl;
#if JUCE_INTEL
    r << "Processor: AVX2 " << (juce::SystemStats::hasAVX2() ? "yes" : "NO") << ", FMA " << (juce::SystemStats::hasFMA3() ? "yes" : "NO")
      << ", " << juce::SystemStats::getNumCpus() << " cores" << nl;
#endif
    const auto st = CompCaptainProcessor::listeningStatus();
    r << "Listening: " << (st.now.isNotEmpty() ? "now " + st.now + " (" + juce::String (st.percent) + "%)" : juce::String ("idle"))
      << ", waiting " << st.waiting << (st.pausedForRecording ? ", paused while a take records" : "")
      << (st.problem.isNotEmpty() ? ", PROBLEM: " + st.problem : juce::String()) << nl;

    const auto song = CompCaptainProcessor::song();
    r << "Lyrics: " << (int) song->lyrics.lines.size() << " lines, " << (int) song->lyrics.sections.size() << " sections";
    if (! song->lyrics.lines.empty())
        r << " (first line: \"" << str (song->lyrics.lines.front().text) << "\")";
    r << nl << "Vocal takes in the song: " << song->sungTakes << ", listened to: " << song->heardTakes << ", lined up: " << song->lineUp.takesLinedUp << nl;
    int timed = 0;
    for (auto& l : song->lineUp.lines)
        timed += l.start >= 0 ? 1 : 0;
    r << "Lines with a time: " << timed << " of " << (int) song->lineUp.lines.size() << nl;

    const auto t = processor.transport();
    r << "DAW: " << (t.known ? (t.playing ? "playing" : "stopped") : "position unknown") << " at " << juce::String (t.seconds, 2) << " s (quarter "
      << juce::String (t.ppq, 2) << "), " << juce::String (t.bpm, 2) << " BPM " << str (t.meter.toText()) << nl;

    withViewed ([&] (CompCaptainProcessor& p) {
        const auto takes = p.getTakes();
        r << nl << "Takes on " << (p.trackName().isNotEmpty() ? p.trackName() : juce::String ("this track")) << ": " << (int) takes.size() << nl;
        for (const auto* tk : takes.inSongOrder())
        {
            r << "- " << str (tk->name) << " (" << str (cc::toLabel (tk->role)) << ", " << str (cc::toName (tk->source)) << "): bar "
              << str (tk->startBarBeat().toText()) << ", starts at " << juce::String (tk->start.seconds, 2) << " s, "
              << juce::String (tk->lengthSeconds(), 1) << " s long" << nl;
            const auto state = p.takeHearingState (tk->id);
            r << "  listened: " << (state.isEmpty() ? juce::String ("not asked") : state);
            if (const auto h = p.takeHearing (tk->id))
            {
                r << ", " << (int) h->words.size() << " words, " << (int) h->phrases.size() << " phrases" << nl << "  heard:";
                int n = 0;
                for (auto& w : h->words)
                {
                    if (++n > 40)
                    {
                        r << " ...";
                        break;
                    }
                    r << str (w.text);
                }
            }
            r << nl;
            const auto key = (p.instanceId() << 32) | tk->id;
            if (const auto placed = song->lineUp.takes.find (key); placed != song->lineUp.takes.end())
            {
                r << "  lines found: " << (int) placed->second.size();
                if (! placed->second.empty())
                    r << " (lines " << placed->second.front().line + 1 << " to " << placed->second.back().line + 1 << ", first at "
                      << juce::String (tk->start.seconds + placed->second.front().start, 2) << " s in the song)";
                r << nl;
            }
            else if (! song->lyrics.empty() && state == "done")
                r << "  lines found: none (what was heard didn't match the lyrics)" << nl;
        }
    });
    return r;
}

void CompCaptainEditor::hearIt (const juce::var& message)
{
    // Lyrics written from what was heard: the take asked for, or the lead take with the most words.
    const auto wanted = ownId (message.getProperty ("id", {}).toString());
    std::optional<cc::Hearing> best;
    juce::String from;
    int waiting = 0;
    withViewed ([&] (CompCaptainProcessor& p) {
        const auto takes = p.getTakes();
        for (const auto* t : takes.inSongOrder())
        {
            if (wanted && t->id != *wanted)
                continue;
            if (p.takeHearingState (t->id) == "waiting")
                ++waiting;
            auto h = p.takeHearing (t->id);
            if (! h)
                continue;
            const bool better = ! best || (t->role == cc::Role::lead && h->words.size() > best->words.size());
            if (better)
            {
                best = std::move (h);
                from = str (t->name);
            }
        }
    });
    auto* msg = new juce::DynamicObject();
    if (best && ! best->words.empty())
    {
        msg->setProperty ("text", str (cc::lyricsFromWords (best->words)));
        msg->setProperty ("from", from);
    }
    else
        msg->setProperty ("error", waiting > 0 ? "Still listening to your takes. Try Hear It again in a moment."
                                               : "Hear It needs a vocal take to listen to. Record one with CAPTURE, or add a file with +.");
    send ("hearIt", msg);
}

// ============================================================================ adding files

void CompCaptainEditor::chooseFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Add takes (WAV, AIFF, FLAC or MP3)", juce::File(), importableWildcard());
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc) {
        const auto files = fc.getResults();
        if (files.isEmpty())
            return;
        const int token = nextToken++;
        chosen[token] = files;
        juce::Array<juce::var> list;
        for (auto& f : files)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", f.getFileNameWithoutExtension());
            o->setProperty ("file", f.getFileName());
            const auto guess = cc::guessRole (f.getFileNameWithoutExtension().toStdString());
            o->setProperty ("guess", guess ? str (cc::toName (*guess)) : juce::String());
            list.add (juce::var (o));
        }
        auto* msg = new juce::DynamicObject();
        msg->setProperty ("token", token);
        msg->setProperty ("files", list);
        send ("chosen", msg);
    });
}

namespace
{
    CompCaptainProcessor::Placement placementFrom (const juce::var& m)
    {
        CompCaptainProcessor::Placement p;
        p.bar = (int) m.getProperty ("bar", 1);
        p.bpm = (double) m.getProperty ("bpm", 120.0);
        p.meter = { (int) m.getProperty ("num", 4), (int) m.getProperty ("den", 4) };
        return p;
    }

    std::optional<cc::Role> roleFrom (const juce::var& v)
    {
        cc::Role r = cc::Role::lead;
        if (cc::fromName (v.toString().toStdString(), r))
            return r;
        return std::nullopt;
    }
}

void CompCaptainEditor::importChosen (const juce::var& message)
{
    const int token = (int) message.getProperty ("token", 0);
    const auto found = chosen.find (token);
    if (found == chosen.end())
        return;
    const auto roles = message.getProperty ("roles", {});
    std::vector<CompCaptainProcessor::ImportRequest> requests;
    for (int i = 0; i < found->second.size(); ++i)
    {
        CompCaptainProcessor::ImportRequest r;
        r.file = found->second[i];
        if (roles.isArray() && i < roles.size())
            r.role = roleFrom (roles[i]);
        requests.push_back (r);
    }
    chosen.erase (found);
    const auto placement = placementFrom (message);
    withViewed ([&] (CompCaptainProcessor& p) { p.importFiles (requests, placement); });
}

void CompCaptainEditor::uploadBegin (const juce::var& message)
{
    // The window can't hand us a dropped file's location, only its bytes: they arrive in pieces
    // and are saved to a temporary file, then added like any other file.
    const auto name = message.getProperty ("name", "Dropped file").toString();
    const auto ext = name.fromLastOccurrenceOf (".", true, false).substring (0, 8);
    const auto stem = juce::String::fromUTF8 (cc::safeFileName (name.upToLastOccurrenceOf (".", false, false).toStdString()).c_str());
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Comp Captain uploads");
    dir.createDirectory();

    Upload u;
    u.file = dir.getNonexistentChildFile (stem, ext, true);
    u.name = name.upToLastOccurrenceOf (".", false, false);
    u.size = (juce::int64) message.getProperty ("size", 0);
    u.out = std::make_unique<juce::FileOutputStream> (u.file);
    const bool ok = u.out->openedOk();
    const int id = nextUpload++;
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("ref", message.getProperty ("ref", {}));
    msg->setProperty ("upload", ok ? id : 0);
    if (ok)
        uploads[id] = std::move (u);
    send ("dropReady", msg);
}

void CompCaptainEditor::uploadChunk (const juce::var& message)
{
    const int id = (int) message.getProperty ("upload", 0);
    const auto it = uploads.find (id);
    if (it == uploads.end())
        return;
    const auto data = cc::fromBase64 (message.getProperty ("data", {}).toString().toStdString());
    const bool ok = it->second.out->write (data.data(), data.size());
    it->second.received += (juce::int64) data.size();
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("upload", id);
    msg->setProperty ("received", it->second.received);
    msg->setProperty ("ok", ok);
    send ("dropAck", msg);
}

void CompCaptainEditor::uploadEnd (const juce::var& message)
{
    const int id = (int) message.getProperty ("upload", 0);
    const auto it = uploads.find (id);
    if (it == uploads.end())
        return;
    it->second.out.reset();
    CompCaptainProcessor::ImportRequest r;
    r.file = it->second.file;
    r.name = it->second.name;
    r.role = roleFrom (message.getProperty ("role", {}));
    r.deleteWhenDone = true;
    uploads.erase (it);
    const auto placement = placementFrom (message);
    withViewed ([&] (CompCaptainProcessor& p) { p.importFiles ({ r }, placement); });
}

void CompCaptainEditor::uploadCancel (const juce::var& message)
{
    const int id = (int) message.getProperty ("upload", 0);
    if (auto it = uploads.find (id); it != uploads.end())
    {
        it->second.out.reset();
        it->second.file.deleteFile();
        uploads.erase (it);
    }
}

// ============================================================================ hearing and building the comp (M4)

// What's being heard from the plug-in (a take, the comp, or nothing), with the song position while it plays.
void CompCaptainEditor::sendAudition (bool force)
{
    CompCaptainProcessor::Audition a;
    bool building = false;
    withViewed ([&] (CompCaptainProcessor& p) {
        a = p.audition();
        building = p.buildingComp();
    });
    const auto signature = a.what + "|" + juce::String (a.take) + "|" + juce::String (a.line) + "|" + juce::String ((int) a.inTime) + "|"
                           + juce::String ((int) a.preparing) + "|" + juce::String ((int) a.sounding) + "|" + juce::String (a.seconds, 2) + "|"
                           + a.problem + "|" + juce::String ((int) building);
    if (! force && signature == sentAudition)
        return;
    sentAudition = signature;
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("what", a.what);
    msg->setProperty ("id", a.take != 0 ? "t" + juce::String (a.take) : juce::String());
    msg->setProperty ("line", a.line);
    msg->setProperty ("inTime", a.inTime);
    msg->setProperty ("preparing", a.preparing);
    msg->setProperty ("sounding", a.sounding);
    msg->setProperty ("seconds", std::round (a.seconds * 100.0) / 100.0);
    msg->setProperty ("problem", a.problem);
    msg->setProperty ("building", building);
    send ("audition", msg);
}

// The comp file Build Performance made (or why it couldn't).
void CompCaptainEditor::sendBuilt (bool force)
{
    juce::uint32 version = 0;
    std::optional<CompCaptainProcessor::Built> built;
    bool building = false;
    withViewed ([&] (CompCaptainProcessor& p) {
        version = p.builtVersion();
        built = p.lastBuilt();
        building = p.buildingComp();
    });
    if (! force && version == sentBuiltVersion)
        return;
    sentBuiltVersion = version;
    auto* msg = new juce::DynamicObject();
    msg->setProperty ("building", building);
    msg->setProperty ("ok", built.has_value() && built->ok);
    msg->setProperty ("file", built.has_value() && built->ok ? built->file.getFullPathName() : juce::String());
    msg->setProperty ("name", built.has_value() && built->ok ? built->file.getFileName() : juce::String());
    msg->setProperty ("bar", built.has_value() ? built->bar : 0);
    msg->setProperty ("message", built.has_value() ? built->message : juce::String());
    send ("built", msg);
}
