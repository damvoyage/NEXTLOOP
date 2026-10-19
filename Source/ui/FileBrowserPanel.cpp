#include "FileBrowserPanel.h"

namespace loopdeck
{

enum Columns { colTitle = 1, colBpm, colKey, colLength, colFormat };

FileBrowserPanel::FileBrowserPanel (AudioEngine& e, juce::PropertiesFile& p) : engine (e), props (p)
{
    // places: saved list, or sensible defaults
    const auto saved = juce::StringArray::fromLines (props.getValue ("browserPlaces"));
    for (auto& s : saved)
        if (s.isNotEmpty() && juce::File (s).isDirectory()) places.add (juce::File (s));
    if (places.isEmpty())
    {
        for (auto loc : { juce::File::userMusicDirectory, juce::File::userDesktopDirectory, juce::File::userHomeDirectory })
        {
            const auto f = juce::File::getSpecialLocation (loc);
            if (f.isDirectory()) places.addIfNotAlreadyThere (f);
        }
        const auto downloads = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
        if (downloads.isDirectory()) places.addIfNotAlreadyThere (downloads);
    }

    addAndMakeVisible (placesList);
    placesList.setModel (&placesModel);
    placesList.setRowHeight (24);
    placesList.setColour (juce::ListBox::backgroundColourId, Theme::tech ? Theme::lcd : Theme::panel);

    addAndMakeVisible (addFolder);
    addFolder.setTooltip ("Add a music folder to the list");
    addFolder.onClick = [this] { addPlace(); };

    addAndMakeVisible (upButton);
    upButton.setTooltip ("Go to the parent folder");
    upButton.onClick = [this] { if (currentFolder.exists()) setFolder (currentFolder.getParentDirectory()); };

    addAndMakeVisible (pathLabel);
    pathLabel.setColour (juce::Label::textColourId, Theme::textDim);
    pathLabel.setFont (Theme::label (12.0f));
    pathLabel.setMinimumHorizontalScale (0.5f);

    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Search this folder", Theme::textDim);
    search.onTextChange = [this] { applyFilter(); };
    search.onEscapeKey = [this] { search.clear(); applyFilter(); };

    addAndMakeVisible (table);
    table.setModel (this);
    table.setRowHeight (22);
    table.setMultipleSelectionEnabled (false);
    table.setColour (juce::ListBox::backgroundColourId, Theme::tech ? Theme::lcd : Theme::panel);
    auto& h = table.getHeader();
    h.addColumn ("Title", colTitle, 360, 120, 2000, juce::TableHeaderComponent::defaultFlags);
    h.addColumn ("BPM", colBpm, 70, 50, 100, juce::TableHeaderComponent::defaultFlags);
    h.addColumn ("Key", colKey, 56, 40, 90, juce::TableHeaderComponent::defaultFlags);
    h.addColumn ("Length", colLength, 70, 50, 100, juce::TableHeaderComponent::defaultFlags);
    h.addColumn ("Type", colFormat, 60, 40, 100, juce::TableHeaderComponent::defaultFlags);
    h.setStretchToFitActive (true);
    h.setSortColumnId (colTitle, true);

    auto start = juce::File (props.getValue ("browserFolder"));
    if (! start.isDirectory()) start = places.isEmpty() ? juce::File::getSpecialLocation (juce::File::userHomeDirectory) : places[0];
    setFolder (start);

    startTimer (1500);
}

FileBrowserPanel::~FileBrowserPanel()
{
    table.setModel (nullptr);
    placesList.setModel (nullptr);
}

void FileBrowserPanel::savePlaces()
{
    juce::StringArray s;
    for (auto& f : places) s.add (f.getFullPathName());
    props.setValue ("browserPlaces", s.joinIntoString ("\n"));
}

void FileBrowserPanel::addPlace()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a music folder", currentFolder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.isDirectory())
                              {
                                  places.addIfNotAlreadyThere (f);
                                  savePlaces();
                                  placesList.updateContent();
                                  setFolder (f);
                              }
                          });
}

void FileBrowserPanel::setFolder (const juce::File& folder)
{
    if (! folder.isDirectory()) return;
    currentFolder = folder;
    props.setValue ("browserFolder", folder.getFullPathName());
    pathLabel.setText (folder.getFullPathName(), juce::dontSendNotification);
    rescan();
}

void FileBrowserPanel::rescan()
{
    all.clear();
    for (const auto& entry : juce::RangedDirectoryIterator (currentFolder, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto& f = entry.getFile();
        if (f.isHidden() || f.getFileName().startsWithChar ('.')) continue;
        Entry e;
        e.file = f;
        e.isFolder = entry.isDirectory();
        if (! e.isFolder && ! engine.getLoader().isSupportedFile (f)) continue;
        e.name = e.isFolder ? f.getFileName() : f.getFileNameWithoutExtension();
        e.ext = e.isFolder ? juce::String() : f.getFileExtension().substring (1).toUpperCase();
        all.push_back (std::move (e));
    }
    applyFilter();
}

void FileBrowserPanel::applyFilter()
{
    shown.clear();
    const auto q = search.getText().trim().toLowerCase();
    for (auto& e : all)
        if (q.isEmpty() || e.name.toLowerCase().contains (q))
            shown.push_back (&e);

    for (auto* e : shown) if (! e->isFolder) loadInfo (*e);

    std::stable_sort (shown.begin(), shown.end(), [this] (const Entry* a, const Entry* b)
    {
        if (a->isFolder != b->isFolder) return a->isFolder;   // folders first
        int c = 0;
        switch (sortColumn)
        {
            case colBpm:    c = a->bpm < b->bpm ? -1 : (a->bpm > b->bpm ? 1 : 0); break;
            case colKey:    c = a->camelot.compareNatural (b->camelot); break;
            case colLength: c = a->seconds < b->seconds ? -1 : (a->seconds > b->seconds ? 1 : 0); break;
            case colFormat: c = a->ext.compare (b->ext); break;
            default:        c = a->name.compareNatural (b->name); break;
        }
        if (c == 0) c = a->name.compareNatural (b->name);
        return sortForwards ? c < 0 : c > 0;
    });

    table.updateContent();
    table.repaint();
}

void FileBrowserPanel::loadInfo (Entry& e)
{
    if (e.infoLoaded) return;
    const auto meta = engine.getLoader().getCacheDirectory().getChildFile (TrackLoader::makeCacheKey (e.file) + ".json");
    if (! meta.existsAsFile()) return;
    const auto json = juce::JSON::parse (meta);
    if (auto* a = json["analysis"].getDynamicObject())
    {
        e.bpm = a->getProperty ("bpm");
        e.camelot = a->getProperty ("camelot").toString();
        e.infoLoaded = true;
    }
    const double sr = json["sampleRate"];
    if (sr > 0.0) e.seconds = (double) (juce::int64) json["frames"] / sr;
}

void FileBrowserPanel::timerCallback()
{
    // pick up analysis results for tracks that were loaded since the last scan
    bool changed = false;
    for (auto* e : shown)
        if (! e->isFolder && ! e->infoLoaded)
        {
            loadInfo (*e);
            changed |= e->infoLoaded;
        }
    if (changed) table.repaint();
}

// ---------------------------------------------------------------------------
int FileBrowserPanel::getNumRows() { return (int) shown.size(); }

void FileBrowserPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected) g.fillAll (Theme::tech ? Theme::active.withAlpha (0.45f) : Theme::raised);
    else if (row % 2) g.fillAll (Theme::tech ? Theme::lcd.brighter (Theme::day ? 0.03f : 0.05f) : Theme::panel.brighter (0.025f));
}

void FileBrowserPanel::paintCell (juce::Graphics& g, int row, int column, int w, int h, bool)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& e = *shown[(size_t) row];
    juce::String text;
    auto colour = Theme::tech ? Theme::lcdText : Theme::text;
    auto just = juce::Justification::centredLeft;
    g.setFont (Theme::label (13.0f));

    switch (column)
    {
        case colTitle:
            text = e.isFolder ? e.name + "/" : e.name;
            if (e.isFolder) colour = Theme::textDim;
            break;
        case colBpm:
            if (e.bpm > 0) text = juce::String (e.bpm, std::abs (e.bpm - std::round (e.bpm)) < 1e-6 ? 0 : 1);
            just = juce::Justification::centredRight;
            g.setFont (Theme::numbers (13.0f));
            break;
        case colKey:    text = e.camelot; just = juce::Justification::centred; break;
        case colLength:
            if (e.seconds > 0)
                text = juce::String ((int) e.seconds / 60) + ":" + juce::String ((int) e.seconds % 60).paddedLeft ('0', 2);
            just = juce::Justification::centredRight;
            break;
        case colFormat: text = e.ext; colour = Theme::textDim; break;
        default: break;
    }
    g.setColour (colour);
    g.drawText (text, 6, 0, w - 12, h, just, true);
}

void FileBrowserPanel::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    returnKeyPressed (row);
}

void FileBrowserPanel::returnKeyPressed (int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto f = shown[(size_t) row]->file;
    if (shown[(size_t) row]->isFolder) { setFolder (f); return; }
    if (loadToNextDeck) loadToNextDeck (f);
}

void FileBrowserPanel::cellClicked (int row, int, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || row < 0 || row >= (int) shown.size() || shown[(size_t) row]->isFolder) return;
    const auto f = shown[(size_t) row]->file;
    juce::PopupMenu menu;
    for (int d = 0; d < engine.getNumDecks(); ++d)
        menu.addItem (d + 1, "Load to deck " + juce::String (d + 1));
    menu.addSeparator();
    menu.addItem (100, "Show in folder");
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, f] (int result)
    {
        if (result == 100) f.revealToUser();
        else if (result > 0 && loadToDeck) loadToDeck (result - 1, f);
    });
}

void FileBrowserPanel::sortOrderChanged (int columnId, bool forwards)
{
    sortColumn = columnId;
    sortForwards = forwards;
    applyFilter();
}

juce::var FileBrowserPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty()) return {};
    const int row = rows[0];
    if (row < 0 || row >= (int) shown.size() || shown[(size_t) row]->isFolder) return {};
    return "file:" + shown[(size_t) row]->file.getFullPathName();
}

// ---------------------------------------------------------------------------
void FileBrowserPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= places.size()) return;
    const bool current = places[row] == currentFolder;
    if (current) { g.setColour (Theme::tech ? Theme::active.withAlpha (0.45f) : Theme::raised); g.fillRect (0, 0, w, h); }
    g.setColour (current ? (Theme::tech ? Theme::lcdText : Theme::text) : Theme::textDim);
    g.setFont (Theme::label (13.0f, current));
    g.drawText (places[row].getFileName().isEmpty() ? places[row].getFullPathName() : places[row].getFileName(),
                10, 0, w - 14, h, juce::Justification::centredLeft, true);
}

void FileBrowserPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= places.size()) return;
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Remove from list");
        menu.showMenuAsync (juce::PopupMenu::Options(), [this, row] (int r)
        {
            if (r == 1) { places.remove (row); savePlaces(); placesList.updateContent(); placesList.repaint(); }
        });
        return;
    }
    setFolder (places[row]);
    placesList.repaint();
}

void FileBrowserPanel::paint (juce::Graphics& g)
{
    Theme::drawPanel (g, getLocalBounds().toFloat(), 5.0f);
}

void FileBrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto left = r.removeFromLeft (juce::jmin (200, r.getWidth() / 5));
    addFolder.setBounds (left.removeFromBottom (24));
    left.removeFromBottom (4);
    placesList.setBounds (left);
    r.removeFromLeft (6);

    auto top = r.removeFromTop (26);
    upButton.setBounds (top.removeFromLeft (44));
    top.removeFromLeft (6);
    search.setBounds (top.removeFromRight (juce::jmin (260, top.getWidth() / 3)));
    top.removeFromRight (6);
    pathLabel.setBounds (top);
    r.removeFromTop (4);
    table.setBounds (r);
}

} // namespace loopdeck
