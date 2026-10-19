#pragma once

#include "engine/AudioEngine.h"
#include "LookAndFeel.h"

namespace loopdeck
{

/**
    Folder browser (phase 1 of the library). Places on the left, the current
    folder's audio files on the right with BPM / key from the analysis cache.
    Double-click loads to the next free deck; drag onto any deck; right-click
    to choose a deck.
*/
class FileBrowserPanel : public juce::Component,
                         public juce::TableListBoxModel,
                         private juce::Timer
{
public:
    FileBrowserPanel (AudioEngine& engine, juce::PropertiesFile& props);
    ~FileBrowserPanel() override;

    std::function<void (const juce::File&)> loadToNextDeck;
    std::function<void (int deck, const juce::File&)> loadToDeck;

    void setFolder (const juce::File& folder);

    void paint (juce::Graphics&) override;
    void resized() override;

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void sortOrderChanged (int columnId, bool forwards) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void returnKeyPressed (int lastRow) override;

    // places list
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected);
    void listBoxItemClicked (int row, const juce::MouseEvent&);

private:
    struct Entry
    {
        juce::File file;
        bool isFolder = false;
        juce::String name, ext;
        bool infoLoaded = false;
        double bpm = 0.0;
        juce::String camelot;
        double seconds = 0.0;
    };

    void timerCallback() override;
    void rescan();
    void applyFilter();
    void loadInfo (Entry& e);
    void savePlaces();
    void addPlace();

    AudioEngine& engine;
    juce::PropertiesFile& props;

    juce::Array<juce::File> places;
    juce::ListBox placesList;
    juce::TextButton addFolder { "Add folder" }, upButton { "Up" };
    juce::Label pathLabel;
    juce::TextEditor search;
    juce::TableListBox table;

    juce::File currentFolder;
    std::vector<Entry> all;
    std::vector<Entry*> shown;
    int sortColumn = 1;
    bool sortForwards = true;
    std::unique_ptr<juce::FileChooser> chooser;

    class PlacesModel : public juce::ListBoxModel
    {
    public:
        explicit PlacesModel (FileBrowserPanel& o) : owner (o) {}
        int getNumRows() override { return owner.places.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override { owner.paintListBoxItem (row, g, w, h, selected); }
        void listBoxItemClicked (int row, const juce::MouseEvent& e) override { owner.listBoxItemClicked (row, e); }
    private:
        FileBrowserPanel& owner;
    };
    PlacesModel placesModel { *this };
};

} // namespace loopdeck
