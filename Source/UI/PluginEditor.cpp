#include "PluginEditor.h"

namespace plxui
{
using namespace juce;
using namespace plx;

// ------------------------------------------------------------------ look and feel
LookAndFeel::LookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, col::bg);
    setColour (TextButton::buttonColourId, Colours::transparentBlack);
    setColour (TextButton::buttonOnColourId, col::text);
    setColour (TextButton::textColourOffId, col::text);
    setColour (TextButton::textColourOnId, col::bg);
    setColour (Label::textColourId, col::text);
    setColour (Slider::textBoxTextColourId, col::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (TextEditor::backgroundColourId, col::slot);
    setColour (TextEditor::textColourId, col::text);
    setColour (TextEditor::outlineColourId, col::line);
    setColour (TextEditor::focusedOutlineColourId, col::dim);
    setColour (ListBox::backgroundColourId, col::panel);
    setColour (PopupMenu::backgroundColourId, col::panel);
    setColour (PopupMenu::textColourId, col::text);
    setColour (PopupMenu::highlightedBackgroundColourId, col::line);
    setColour (ScrollBar::thumbColourId, col::line);
}

void LookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    Slider::SliderStyle, Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const Colour accent = s.findColour (Slider::trackColourId);
    g.setColour (col::slot);
    g.fillRoundedRectangle ((float) x, cy - 3.0f, (float) w, 6.0f, 3.0f);
    g.setColour (accent);
    g.fillRoundedRectangle ((float) x, cy - 3.0f, jmax (0.0f, pos - (float) x), 6.0f, 3.0f);
    g.setColour (col::text);
    g.fillEllipse (pos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
}

void LookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const Colour fill = b.getProperties().contains ("fill") ? Colour ((uint32) (int64) b.getProperties()["fill"]) : Colours::transparentBlack;
    g.setColour (on ? b.findColour (TextButton::buttonOnColourId) : fill);
    g.fillRoundedRectangle (r, 6.0f);
    if (over || down) { g.setColour (Colours::white.withAlpha (down ? 0.12f : 0.06f)); g.fillRoundedRectangle (r, 6.0f); }
    if (! b.getProperties().contains ("noborder"))
    {
        g.setColour (on ? b.findColour (TextButton::buttonOnColourId) : col::line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }
}

Font LookAndFeel::getTextButtonFont (TextButton&, int h) { return Font (FontOptions (jmin (14.0f, (float) h * 0.55f))); }

// ------------------------------------------------------------------ plug-in browser
class Browser : public Component, private ListBoxModel, private ChangeListener, private Timer
{
public:
    Browser (ParallaxProcessor& p) : proc (p)
    {
        addAndMakeVisible (title);
        title.setFont (FontOptions (16.0f, Font::bold));
        addAndMakeVisible (search);
        search.setTextToShowWhenEmpty ("Search plug-ins (name, maker, type)", col::dim);
        search.setJustification (Justification::centredLeft);
        search.setIndents (10, 0);
        search.setFont (FontOptions (15.0f));
        search.onTextChange = [this] { update(); };
        search.onReturnKey = [this] { load (list.getSelectedRow() >= 0 ? list.getSelectedRow() : 0); };
        addAndMakeVisible (list);
        list.setRowHeight (30);
        list.setModel (this);
        for (auto* b : { &addBtn, &rescan, &close }) addAndMakeVisible (b);
        addBtn.onClick = [this] { load (list.getSelectedRow()); };
        rescan.onClick = [this] { proc.catalog.startScan (true); };
        close.onClick = [this] { setVisible (false); };
        addAndMakeVisible (status);
        status.setColour (Label::textColourId, col::dim);
        proc.catalog.changes.addChangeListener (this);
        startTimerHz (4);
    }
    ~Browser() override { proc.catalog.changes.removeChangeListener (this); }

    void show (int chain)
    {
        target = chain;
        title.setText ("Add a plug-in to Chain " + ParallaxProcessor::chainName (chain), dontSendNotification);
        title.setColour (Label::textColourId, col::chain (chain));
        if (proc.catalog.getEffects().isEmpty() && ! proc.catalog.isScanning()) proc.catalog.startScan (false);
        update();
        setVisible (true);
        toFront (true);
        search.grabKeyboardFocus();
    }

    std::function<void (const String&)> onMessage;

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black.withAlpha (0.55f));
        g.setColour (col::panel);
        g.fillRoundedRectangle (box.toFloat(), 12.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (box.toFloat(), 12.0f, 1.0f);
    }

    void resized() override
    {
        box = getLocalBounds().reduced (60, 30);
        auto r = box.reduced (16);
        title.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);
        search.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (36);
        close.setBounds (bottom.removeFromRight (90));
        bottom.removeFromRight (8);
        addBtn.setBounds (bottom.removeFromRight (90));
        bottom.removeFromRight (8);
        rescan.setBounds (bottom.removeFromRight (90));
        status.setBounds (bottom);
        r.removeFromBottom (8);
        list.setBounds (r);
    }

    void mouseDown (const MouseEvent& e) override { if (! box.contains (e.getPosition())) setVisible (false); }

private:
    int getNumRows() override { return results.size(); }
    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (! isPositiveAndBelow (row, results.size())) return;
        auto& d = results.getReference (row);
        if (selected) { g.setColour (col::line); g.fillRoundedRectangle (0.0f, 1.0f, (float) w, (float) h - 2.0f, 5.0f); }
        g.setColour (col::text);
        g.setFont (FontOptions (14.0f));
        g.drawText (d.name, 10, 0, w / 2, h, Justification::centredLeft, true);
        g.setColour (col::dim);
        g.setFont (FontOptions (12.0f));
        g.drawText (d.manufacturerName + "  ·  " + d.pluginFormatName, w / 2, 0, w / 2 - 10, h, Justification::centredRight, true);
    }
    void listBoxItemDoubleClicked (int row, const MouseEvent&) override { load (row); }
    void returnKeyPressed (int row) override { load (row); }
    void changeListenerCallback (ChangeBroadcaster*) override { update(); }
    void timerCallback() override
    {
        if (! isVisible()) return;
        String s = proc.catalog.isScanning()
                     ? proc.catalog.getScanStatus() + "  " + String (roundToInt (proc.catalog.getScanProgress() * 100.0f)) + "%"
                     : String (proc.catalog.getEffects().size()) + " effects.  Double-click one to add it.";
        status.setText (s, dontSendNotification);
    }

    void update()
    {
        results = proc.catalog.search (search.getText());
        list.updateContent();
        if (results.size() > 0 && list.getSelectedRow() < 0) list.selectRow (0);
        list.repaint();
        timerCallback();
    }

    void load (int row)
    {
        if (! isPositiveAndBelow (row, results.size())) return;
        String err;
        const auto d = results[row];
        if (proc.addPlugin (target, d, err)) { setVisible (false); if (onMessage) onMessage (d.name + " added to Chain " + ParallaxProcessor::chainName (target)); }
        else if (onMessage) onMessage (err);
    }

    ParallaxProcessor& proc;
    int target = 0;
    Label title;
    TextEditor search;
    ListBox list { "plugins" };
    TextButton addBtn { "Add" }, rescan { "Rescan" }, close { "Close" };
    Label status;
    Array<PluginDescription> results;
    Rectangle<int> box;
};

// ------------------------------------------------------------------ chain panel
ChainPanel::ChainPanel (ParallaxProcessor& p, int index) : proc (p), c (index)
{
    addAndMakeVisible (title);
    title.setText ("Chain " + ParallaxProcessor::chainName (c), dontSendNotification);
    title.setFont (FontOptions (15.0f, Font::bold));
    title.setColour (Label::textColourId, col::chain (c));

    addAndMakeVisible (level);
    level.setSliderStyle (Slider::LinearHorizontal);
    level.setTextBoxStyle (Slider::TextBoxRight, false, 52, 20);
    level.setTextValueSuffix (" %");
    level.setColour (Slider::trackColourId, col::chain (c));
    level.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    levelAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "level" + ParallaxProcessor::chainName (c), level);
    level.textFromValueFunction = [] (double v) { return String (roundToInt (v)) + " %"; };
    level.updateText();

    addAndMakeVisible (solo);
    solo.setTooltip ("Solo this chain, at the same loudness as the dry signal");
    solo.setColour (TextButton::buttonOnColourId, col::yellow);
    solo.onClick = [this]
    {
        auto* prm = proc.apvts.getParameter ("solo");
        const int cur = roundToInt (proc.apvts.getRawParameterValue ("solo")->load());
        const int next = cur == c + 1 ? 0 : c + 1;
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) next));
        prm->endChangeGesture();
        refreshSolo();
    };

    addAndMakeVisible (add);
    add.getProperties().set ("fill", (int64) 0);
    add.setColour (TextButton::textColourOffId, col::dim);
    add.onClick = [this] { if (onAdd) onAdd (c); };
    rebuild();
}

void ChainPanel::refreshSolo()
{
    solo.setToggleState (roundToInt (proc.apvts.getRawParameterValue ("solo")->load()) == c + 1, dontSendNotification);
}

void ChainPanel::rebuild()
{
    rows.clear();
    const auto& ch = proc.chain (c);
    for (int i = 0; i < (int) ch.size(); ++i)
    {
        auto* r = rows.add (new Row());
        addAndMakeVisible (r);
        r->addAndMakeVisible (r->name);
        r->addAndMakeVisible (r->remove);
        r->name.setButtonText (ch[(size_t) i]->name());
        r->name.getProperties().set ("fill", (int64) col::slot.getARGB());
        r->name.getProperties().set ("noborder", true);
        r->name.setTooltip ("Click: open or close its window.  Right-click: move or remove.");
        r->name.setColour (TextButton::textColourOffId, ch[(size_t) i]->missing() ? col::dim : col::text);
        r->name.onClick = [this, i]
        {
            if (ModifierKeys::currentModifiers.isPopupMenu()) { showRowMenu (i); return; }
            proc.openPluginWindow (c, i);
        };
        r->remove.getProperties().set ("noborder", true);
        r->remove.setColour (TextButton::textColourOffId, col::dim);
        r->remove.setTooltip ("Remove");
        r->remove.onClick = [this, i] { MessageManager::callAsync ([sp = SafePointer<ChainPanel> (this), i] { if (sp) sp->proc.removePlugin (sp->c, i); }); };
    }
    add.setVisible ((int) ch.size() < kMaxSlots);
    resized();
    repaint();
}

void ChainPanel::showRowMenu (int i)
{
    PopupMenu m;
    const int n = (int) proc.chain (c).size();
    m.addItem (1, "Open window", ! proc.chain (c)[(size_t) i]->missing());
    m.addItem (2, "Move up", i > 0);
    m.addItem (3, "Move down", i < n - 1);
    m.addSeparator();
    m.addItem (4, "Remove");
    m.showMenuAsync (PopupMenu::Options(), [sp = SafePointer<ChainPanel> (this), i] (int r)
    {
        if (sp == nullptr) return;
        if (r == 1) sp->proc.openPluginWindow (sp->c, i);
        if (r == 2) sp->proc.movePlugin (sp->c, i, i - 1);
        if (r == 3) sp->proc.movePlugin (sp->c, i, i + 1);
        if (r == 4) sp->proc.removePlugin (sp->c, i);
    });
}

void ChainPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (col::chain (c));
    g.fillRoundedRectangle (r.removeFromTop (3.0f).reduced (8.0f, 0.0f), 1.5f);
    if (dragOver) { g.setColour (col::chain (c).withAlpha (0.8f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 10.0f, 2.0f); }

    const int lat = proc.engine.chainLatency (c);
    g.setColour (col::dim);
    g.setFont (FontOptions (11.0f));
    g.drawText (lat > 0 ? String (lat) + " smp" : String(), getLocalBounds().reduced (12, 10).removeFromTop (22), Justification::centredRight);
    if (proc.chain (c).empty())
    {
        g.drawFittedText ("Empty chains are silent.\nAdd a plug-in, or drop a\n.vst3 / .component file here.",
                          getLocalBounds().reduced (14).withTrimmedTop (150).withHeight (60), Justification::centred, 3);
    }
}

void ChainPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    title.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);
    auto lr = r.removeFromTop (30);
    solo.setBounds (lr.removeFromRight (36));
    lr.removeFromRight (6);
    level.setBounds (lr);
    r.removeFromTop (10);
    for (auto* row : rows)
    {
        auto rr = r.removeFromTop (32);
        row->setBounds (rr);
        auto in = row->getLocalBounds();
        row->remove.setBounds (in.removeFromRight (26));
        row->name.setBounds (in);
        r.removeFromTop (4);
    }
    add.setBounds (r.removeFromTop (32));
}

bool ChainPanel::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".vst3") || f.endsWithIgnoreCase (".component")) return true;
    return false;
}

void ChainPanel::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (auto& f : files)
    {
        String err;
        auto types = proc.catalog.findTypesForFile (File (f), err);
        if (types.isEmpty()) { if (onMessage) onMessage (err.isNotEmpty() ? err : "No effect found in " + File (f).getFileName()); continue; }
        if (! proc.addPlugin (c, types.getFirst(), err) && onMessage) onMessage (err);
    }
}

// ------------------------------------------------------------------ editor
ParallaxEditor::ParallaxEditor (ParallaxProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (amount);
    amount.setSliderStyle (Slider::LinearHorizontal);
    amount.setTextBoxStyle (Slider::TextBoxRight, false, 60, 22);
    amount.setTextValueSuffix (" %");
    amount.setColour (Slider::trackColourId, col::text);
    amount.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    amountAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "amount", amount);
    amount.textFromValueFunction = [] (double v) { return String (roundToInt (v)) + " %"; };
    amount.updateText();

    addAndMakeVisible (lockButton);
    lockButton.setClickingTogglesState (true);
    lockButton.setColour (TextButton::buttonOnColourId, col::slot);
    lockButton.setColour (TextButton::textColourOnId, col::text);
    lockButton.setTooltip ("Loudness Lock: after every change Parallax measures for a second, then matches the output to the dry loudness in one move");
    lockAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "lock", lockButton);

    addAndMakeVisible (relearn);
    relearn.setTooltip ("Measure again now (e.g. in a louder part of the song)");
    relearn.onClick = [this] { proc.engine.requestRelearn(); };

    for (int c = 0; c < kNumChains; ++c)
    {
        auto* cp = panels.add (new ChainPanel (proc, c));
        cp->onAdd = [this] (int ch) { openBrowser (ch); };
        cp->onMessage = [this] (const String& s) { message (s); };
        addAndMakeVisible (cp);
    }

    browser = std::make_unique<Browser> (proc);
    browser->onMessage = [this] (const String& s) { message (s); };
    addChildComponent (*browser);

    proc.addChangeListener (this);
    setSize (820, 500);
    startTimerHz (15);
}

ParallaxEditor::~ParallaxEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void ParallaxEditor::openBrowser (int chain) { browser->show (chain); }

void ParallaxEditor::changeListenerCallback (ChangeBroadcaster*)
{
    for (auto* p : panels) p->rebuild();
}

void ParallaxEditor::timerCallback()
{
    const auto st = (LockState) proc.engine.state.load();
    const bool on = proc.apvts.getRawParameterValue ("lock")->load() > 0.5f;
    String t = ! on ? "Lock: off" : st == LockState::matching ? "Matching..." : "Locked";
    lockButton.setButtonText (t);
    for (auto* p : panels) p->refreshSolo();
    repaint (lockLight.expanded (4));
    repaint (footer);
    if (toast.isNotEmpty() && Time::getMillisecondCounter() > toastUntil) { toast.clear(); repaint (footer); }
}

void ParallaxEditor::paint (Graphics& g)
{
    g.fillAll (col::bg);
    auto r = getLocalBounds().reduced (16, 12);
    auto head = r.removeFromTop (40);
    g.setColour (col::text);
    g.setFont (FontOptions (20.0f, Font::bold));
    g.drawText ("PARALLAX", head.removeFromLeft (200), Justification::centredLeft);

    // lock light + delta
    const auto st = (LockState) proc.engine.state.load();
    const bool on = proc.apvts.getRawParameterValue ("lock")->load() > 0.5f;
    g.setColour (! on ? col::dim : st == LockState::matching ? col::orange : col::green);
    g.fillEllipse (lockLight.toFloat());
    g.setColour (col::dim);
    g.setFont (FontOptions (12.0f));
    String d = String (proc.engine.deltaDb.load(), 1);
    g.drawText (String (CharPointer_UTF8 ("\xce\x94 ")) + d + " dB   gain " + String (proc.engine.compDb.load(), 1) + " dB",
                lockLight.getRight() - 300 - lockLight.getWidth() - 8, lockLight.getY() - 6, 300, lockLight.getHeight() + 12,
                Justification::centredRight);

    // amount panel
    auto ar = getLocalBounds().reduced (16, 12).withTrimmedTop (48).removeFromTop (58);
    g.setColour (col::panel);
    g.fillRoundedRectangle (ar.toFloat(), 10.0f);
    g.setColour (col::dim);
    g.setFont (FontOptions (11.0f));
    g.drawText ("AMOUNT", ar.reduced (14, 6).removeFromTop (14), Justification::centredLeft);
    auto labels = ar.reduced (14, 6).withTrimmedTop (16);
    g.drawText ("Dry", labels.removeFromLeft (30), Justification::centredLeft);
    g.drawText ("Wet", labels.removeFromRight (34), Justification::centredRight);

    // footer
    g.setColour (toast.isNotEmpty() ? col::orange : col::dim);
    g.setFont (FontOptions (12.0f));
    String f = toast;
    if (f.isEmpty())
    {
        auto lufs = [] (float v) { return v < -90.0f ? String ("-inf") : String (v, 1); };
        f = "Dry " + lufs (proc.engine.dryLufs.load()) + " LUFS   Out " + lufs (proc.engine.outLufs.load()) + " LUFS";
        if (proc.engine.latency() > 0) f << "   Latency " << proc.engine.latency() << " smp";
        if (st == LockState::matching && on) f << "   measuring " << roundToInt (proc.engine.matchProgress.load() * 100.0f) << "%";
    }
    g.drawText (f, footer, Justification::centredLeft, true);
}

void ParallaxEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto head = r.removeFromTop (40);
    relearn.setBounds (head.removeFromRight (90).reduced (0, 4));
    head.removeFromRight (8);
    lockButton.setBounds (head.removeFromRight (130).reduced (0, 4));
    lockLight = Rectangle<int> (lockButton.getX() - 20, lockButton.getBounds().getCentreY() - 6, 12, 12);
    r.removeFromTop (8);
    auto ar = r.removeFromTop (58);
    {
        auto sr = ar.reduced (14, 6).withTrimmedTop (16).withTrimmedLeft (34);
        amount.setBounds (sr.withTrimmedRight (0));
        amount.setTextBoxStyle (Slider::TextBoxRight, false, 60, 22);
        // leave room for the "Wet" label: the slider track ends before it
        amount.setBounds (sr.withTrimmedRight (38));
    }
    r.removeFromTop (10);
    footer = r.removeFromBottom (22);
    r.removeFromBottom (6);
    const int w = (r.getWidth() - 2 * 10) / 3;
    for (int c = 0; c < kNumChains; ++c)
    {
        panels[c]->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (10);
    }
    browser->setBounds (getLocalBounds());
}
}
