#include "ui/MainComponent.h"
#include "ui/SettingsComponent.h"

namespace loopdeck
{

class NEXTLOOPApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "NEXTLOOP";
        o.filenameSuffix = ".settings";
        o.folderName = "NEXTLOOP";
        o.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (o);
        auto& props = *properties.getUserSettings();
        {
            // older settings stored only tech on/off
            int skin = props.getIntValue ("skin", props.getBoolValue ("techSkin", true) ? (int) Theme::Skin::Gunmetal : (int) Theme::Skin::Flat);
            skin = juce::jlimit (0, Theme::numSkins - 1, skin);
            Theme::set (props.getBoolValue ("dayMode", false), props.getBoolValue ("monoMode", false), (Theme::Skin) skin);
        }

        engine = std::make_unique<AudioEngine>();

        // preferences that live in the engine
        if (props.containsKey ("bpmMin"))
        {
            const double mn = props.getDoubleValue ("bpmMin", 88.0);
            engine->getLoader().analysisSettings.minBpm = mn;
            engine->getLoader().analysisSettings.maxBpm = mn * 2.0 - 1.0;
        }
        engine->getLoader().setCacheLimitBytes ((juce::int64) props.getIntValue ("cacheLimitGb", 20) * 1024 * 1024 * 1024);
        if (props.containsKey ("masterOut")) engine->masterOutChannel = props.getIntValue ("masterOut", 0);
        if (props.containsKey ("cueOut"))    engine->cueOutChannel = props.getIntValue ("cueOut", -1);

        auto savedDevice = props.getXmlValue ("audioDevice");
        const auto err = engine->initialiseAudio (savedDevice.get());

        // headless smoke test: --screenshot <file.png> [decks] [audio files...]
        if (commandLine.contains ("--screenshot"))
        {
            runScreenshot (commandLine, props);
            return;
        }

        mainWindow = std::make_unique<MainWindow> (*engine, props);

        if (err.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Audio output problem",
                                                    err + "\n\nOpen Audio and settings to choose your interface.");
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        if (engine != nullptr)
        {
            if (auto xml = engine->getDeviceManager().createStateXml())
                properties.getUserSettings()->setValue ("audioDevice", xml.get());
            engine->housekeeping();
        }
        properties.saveIfNeeded();
        engine = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (AudioEngine& engine, juce::PropertiesFile& props)
            : DocumentWindow ("NEXTLOOP", Theme::base, DocumentWindow::allButtons), engineRef (engine)
        {
            setUsingNativeTitleBar (true);
            properties = &props;
            buildContent();
            setResizable (true, true);
            setResizeLimits (1100, 700, 10000, 10000);
            if (! restoreWindowStateFromString (props.getValue ("windowState")))
                centreWithSize (juce::jmin (1680, getParentMonitorArea().getWidth() - 40),
                                juce::jmin (1020, getParentMonitorArea().getHeight() - 60));
            setVisible (true);
        }

        void buildContent()
        {
            setBackgroundColour (Theme::base);
            auto* mc = new MainComponent (engineRef, *properties);
            mc->onThemeChanged = [this]
            {
                const auto bounds = getContentComponent() != nullptr ? getContentComponent()->getBounds() : juce::Rectangle<int>();
                clearContentComponent();
                buildContent();
                if (! bounds.isEmpty()) getContentComponent()->setBounds (bounds);
            };
            setContentOwned (mc, false);
            if (getWidth() > 0) mc->setBounds (getLocalBounds());
        }

        ~MainWindow() override
        {
            if (properties != nullptr) properties->setValue ("windowState", getWindowStateAsString());
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        AudioEngine& engineRef;
        juce::PropertiesFile* properties = nullptr;
    };

    /** Renders the UI to a PNG without a window, for automated visual checks. */
    void runScreenshot (const juce::String& commandLine, juce::PropertiesFile& props)
    {
        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.removeEmptyStrings();
        if (args.contains ("--day")) { Theme::setDay (true); args.removeString ("--day"); }
        if (args.contains ("--night")) { Theme::setDay (false); args.removeString ("--night"); }
        if (args.contains ("--mono")) { Theme::set (Theme::userDay, true); args.removeString ("--mono"); }
        if (args.contains ("--flat")) { Theme::set (Theme::userDay, Theme::userMono, Theme::Skin::Flat); args.removeString ("--flat"); }
        for (auto& a : args)
            if (a.startsWith ("--skin="))
                Theme::set (Theme::userDay, Theme::userMono, (Theme::Skin) juce::jlimit (0, Theme::numSkins - 1, a.fromFirstOccurrenceOf ("=", false, false).getIntValue()));
        for (int i = args.size(); --i >= 0;) if (args[i].startsWith ("--skin=")) args.remove (i);
        // --view=a,b,c sets which sections are shown (for screenshots)
        for (auto& a : args)
            if (a.startsWith ("--view="))
            {
                const auto shown = juce::StringArray::fromTokens (a.fromFirstOccurrenceOf ("=", false, false), ",", "");
                props.setValue ("viewOverride", shown.joinIntoString (","));
            }
        for (int i = args.size(); --i >= 0;) if (args[i].startsWith ("--view=")) args.remove (i);
        const bool withSettings = args.contains ("--with-settings");
        const bool bench = args.contains ("--bench");
        args.removeString ("--bench");
        args.removeString ("--with-settings");
        if (withSettings) args.add ("--with-settings");
        if (bench) args.add ("--bench");
        const int idx = args.indexOf ("--screenshot");
        const juce::File out (args[idx + 1].unquoted());
        const int decks = args[idx + 2].getIntValue();
        const int w = args[idx + 3].getIntValue() > 0 ? args[idx + 3].getIntValue() : 1680;
        const int h = args[idx + 4].getIntValue() > 0 ? args[idx + 4].getIntValue() : 1000;

        props.setValue ("numDecks", decks > 0 ? decks : 4);
        auto comp = std::make_unique<MainComponent> (*engine, props);
        comp->setSize (w, h);

        // load any audio files given and let them decode/analyse
        int deck = 0;
        for (int i = idx + 5; i < args.size(); ++i)
        {
            const juce::File f (args[i].unquoted());
            if (f.existsAsFile()) engine->loadTrack (deck++, f);
        }
        engine->getLoader().waitUntilIdle (60000);

        // advance the engine a little so decks have state, without a device
        engine->shutdownAudio();
        engine->prepareOffline (48000.0, 512);
        for (int d = 0; d < deck; ++d)
        {
            engine->getDeck (d).syncEnabled = d > 0;
            engine->post (DeckCommand::Play, d);
            engine->post (DeckCommand::HotCueDown, d, 0.0, 0);
            engine->post (DeckCommand::HotCueUp, d, 0.0, 0);
        }
        std::vector<float> l (512), r (512);
        float* outs[2] = { l.data(), r.data() };
        for (int i = 0; i < 48000 * 6 / 512; ++i)
        {
            if (i == 48000 * 3 / 512)
                for (int d = 0; d < deck; ++d) engine->post (DeckCommand::AutoLoop, d, d % 2 == 0 ? 20.0 : 4.0);
            engine->renderOffline (outs, 2, 512);
            engine->getMixer().channels[0].meterL = 0.6f; // keep meters lively for the picture
        }
        engine->getMixer().channels[1].cue = true;
        engine->getDeck (1).keyLock = true;
        engine->getDeck (2).keyShift = 2.0f;
        engine->getMixer().channels[0].eqLow = 0.2f;
        engine->getMixer().channels[0].filter = 0.3f;

        for (int i = 0; i < 6; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        }
        if (args.contains ("--with-settings"))
        {
            DeckSettings ds;
            SettingsComponent sc (*engine, props, ds);
            sc.setSize (660, 840);
            auto simg = sc.createComponentSnapshot (sc.getLocalBounds(), true, 1.0f);
            juce::FileOutputStream sos (out.getSiblingFile (out.getFileNameWithoutExtension() + "-settings.png"));
            sos.setPosition (0); sos.truncate();
            juce::PNGImageFormat().writeImageToStream (simg, sos);
        }
        if (args.contains ("--bench"))
        {
            // full-frame paint cost: worst case, every view redrawn
            const int frames = 60;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < frames; ++i)
            {
                engine->renderOffline (outs, 2, 512);
                auto img = comp->createComponentSnapshot (comp->getLocalBounds(), true, 1.0f);
            }
            const auto ms = (juce::Time::getMillisecondCounterHiRes() - t0) / frames;
            std::printf ("full-frame paint: %.2f ms per frame (%.0f%% of one core at 60 fps, %.0f%% at 30 fps)\n",
                         ms, ms * 60.0 / 10.0, ms * 30.0 / 10.0);

            // steady state: only the waveform views redraw each frame while decks play
            juce::Array<juce::Component*> waves;
            std::function<void (juce::Component*)> collect = [&] (juce::Component* c)
            {
                if (c->isShowing() || c->isVisible())
                    if (dynamic_cast<ScrollingWaveform*> (c) != nullptr || dynamic_cast<OverviewWaveform*> (c) != nullptr || dynamic_cast<Platter*> (c) != nullptr)
                        if (c->isVisible() && c->getWidth() > 0) waves.add (c);
                for (auto* ch : c->getChildren()) collect (ch);
            };
            collect (comp.get());
            juce::Image target (juce::Image::ARGB, comp->getWidth(), comp->getHeight(), true);
            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < frames; ++i)
            {
                engine->renderOffline (outs, 2, 512);   // decks advance, so views really scroll
                juce::Graphics g (target);
                for (auto* view : waves)
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    const auto pos = comp->getLocalPoint (view, juce::Point<int>());
                    g.setOrigin (pos);
                    g.reduceClipRegion (view->getLocalBounds());
                    view->paintEntireComponent (g, false);
                }
            }
            const auto wms = (juce::Time::getMillisecondCounterHiRes() - t1) / frames;
            std::printf ("waveforms + platters only (%d views): %.2f ms per frame (%.1f%% of one core at 60 fps, %.1f%% at 30 fps)\n",
                         waves.size(), wms, wms * 60.0 / 10.0, wms * 30.0 / 10.0);
        }
        auto image = comp->createComponentSnapshot (comp->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (out);
        os.setPosition (0);
        os.truncate();
        juce::PNGImageFormat png;
        png.writeImageToStream (image, os);
        os.flush();
        comp = nullptr;
        quit();
    }

    juce::ApplicationProperties properties;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace loopdeck

START_JUCE_APPLICATION (loopdeck::NEXTLOOPApplication)
