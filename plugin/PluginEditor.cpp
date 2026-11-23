#include "PluginEditor.h"

#include "AudioImport.h"

#include "cc/Overview.h"
#include "cc/SuiteDetector.h"
#include "cc/TakesFolder.h"

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
    setResizeLimits (cc::UiState::artWidth / 2, (int) std::lround (cc::UiState::artHeight * cc::UiState::minScale),
                     (int) std::lround (cc::UiState::artWidth * cc::UiState::maxScale), (int) std::lround (cc::UiState::artHeight * cc::UiState::maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) cc::UiState::artWidth / (double) cc::UiState::artHeight);
    setSize (state.windowWidth(), state.windowHeight());

    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (30);
}

CompCaptainEditor::~CompCaptainEditor()
{
    stopTimer();
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

void CompCaptainEditor::resized()
{
    browser.setBounds (getLocalBounds());
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
        sentTakesVersion = sentAraVersion = 0;
        sentTracks = sentTransport = sentCapture = {};
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
        viewing = id == processor.instanceId() ? 0 : id;
        sentTakesVersion = 0;
        sendTakes();
        sendTracks (true);
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
}

void CompCaptainEditor::sendTakes()
{
    juce::Array<juce::var> list;
    juce::String trackName, folder;
    cc::Role role = cc::Role::lead;
    juce::uint32 version = 0;

    withViewed ([&] (CompCaptainProcessor& p) {
        const auto takes = p.getTakes();
        for (const auto* t : takes.inSongOrder())
            list.add (takeVar (*t, "t" + juce::String (t->id), p.takeFileMissing (t->id), p.takeOverview (t->id).has_value()));
        trackName = p.trackName();
        role = p.trackRole();
        folder = juce::String::fromUTF8 (takes.folder.c_str());
        version = p.takesVersion();
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

    sendCapture (false);
    sendTransport (false);
    sendNews();

    juce::uint32 version = 0;
    withViewed ([&] (CompCaptainProcessor& p) { version = p.takesVersion(); });
    juce::uint32 araVersion = 0;
#if JucePlugin_Enable_ARA
    if (auto* doc = processor.araDocument())
        araVersion = doc->version();
#endif
    if (version != sentTakesVersion || araVersion != sentAraVersion)
        sendTakes();
    if (tick % 15 == 0)
        sendTracks (false);
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
