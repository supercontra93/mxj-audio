// SPDX-License-Identifier: AGPL-3.0-only
#include "app/plugins/PluginWindow.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"
#include "plugins/clap/ClapFormat.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mxj
{
    namespace
    {
        juce::OwnedArray<PluginWindow>& windows()
        {
            static juce::OwnedArray<PluginWindow> list;
            return list;
        }

        std::map<PluginWindow*, plugins::HostedPlugin*>& owners()
        {
            static std::map<PluginWindow*, plugins::HostedPlugin*> map;
            return map;
        }
    }

    //==============================================================================
    /** Shown when the plugin has no window, or could not be loaded. */
    class PluginWindow::Message : public juce::Component
    {
    public:
        explicit Message (juce::String t) : text (std::move (t)) { setSize (fonts::scaled (420), fonts::scaled (120)); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (palette::panel);
            g.setColour (palette::text);
            g.setFont (fonts::sans (13.0f));
            g.drawFittedText (text, getLocalBounds().reduced (16), juce::Justification::centred, 4);
        }

    private:
        juce::String text;
    };

    //==============================================================================
    /** Holds the native window of a CLAP plugin. The plugin draws into the
        window itself; this component only keeps the place and the size. */
    class PluginWindow::ClapView : public juce::Component
    {
    public:
        ClapView (plugins::ClapPluginInstance::Gui& g, PluginWindow& w) : gui (g), window (w)
        {
            setSize (400, 300);
        }

        ~ClapView() override
        {
            gui.onResizeRequest = nullptr;
            gui.onClosedByPlugin = nullptr;
            if (opened)
                gui.close();
        }

        /** Opens the plugin's window inside the (visible) main window. */
        bool open()
        {
            auto* peer = window.getPeer();
            if (peer == nullptr)
                return false;

           #if JUCE_MAC
            scale = 1.0;
           #else
            scale = peer->getPlatformScaleFactor();
           #endif

            int w = 0, h = 0;
            if (! gui.open (peer->getNativeHandle(), scale, w, h))
                return false;

            opened = true;
            gui.onResizeRequest = [this] (int width, int height) { setPhysicalSize (width, height); };
            gui.onClosedByPlugin = [&w = window] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginWindow> (&w)]
            {
                if (safe != nullptr)
                    safe->closeButtonPressed();
            }); };

            setPhysicalSize (w, h);
            window.setResizable (gui.canResize(), false);
            return true;
        }

        void resized() override
        {
            if (! opened || settingSize || ! gui.canResize())
                return;

            const auto [w, h] = gui.resize (juce::roundToInt (getWidth() * scale), juce::roundToInt (getHeight() * scale));
            if (w > 0 && h > 0)
                setPhysicalSize (w, h);
        }

    private:
        void setPhysicalSize (int width, int height)
        {
            const juce::ScopedValueSetter<bool> busy (settingSize, true);
            window.setContentComponentSize (juce::jmax (1, juce::roundToInt (width / scale)),
                                            juce::jmax (1, juce::roundToInt (height / scale)));
        }

        plugins::ClapPluginInstance::Gui& gui;
        PluginWindow& window;
        double scale = 1.0;
        bool opened = false, settingSize = false;
    };

    //==============================================================================
    bool PluginWindow::canShow (const std::shared_ptr<dsp::EffectState>& state)
    {
        return std::dynamic_pointer_cast<plugins::HostedPlugin> (state) != nullptr;
    }

    void PluginWindow::show (const std::shared_ptr<dsp::EffectState>& state, const juce::String& title)
    {
        auto plugin = std::dynamic_pointer_cast<plugins::HostedPlugin> (state);
        if (plugin == nullptr)
            return;

        for (auto* w : windows())
            if (owners()[w] == plugin.get())
            {
                w->toFront (true);
                return;
            }

        auto* window = windows().add (new PluginWindow (plugin, title));
        owners()[window] = plugin.get();
    }

    void PluginWindow::closeFor (const std::shared_ptr<dsp::EffectState>& state)
    {
        if (state == nullptr)
            return;

        for (auto* w : windows())
            if (owners()[w] == state.get())
                return w->closeButtonPressed();
    }

    void PluginWindow::closeAll()
    {
        owners().clear();
        windows().clear();
    }

    PluginWindow::PluginWindow (std::shared_ptr<plugins::HostedPlugin> p, const juce::String& title)
        : juce::DocumentWindow (title, palette::panel, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          plugin (std::move (p))
    {
        setUsingNativeTitleBar (true);
        auto* instance = plugin->getInstance();

        if (instance == nullptr)
        {
            setContentOwned (new Message (TRANS ("The plugin could not be loaded.") + "\n" + plugin->getError()), true);
        }
        else if (auto* clap = dynamic_cast<plugins::ClapPluginInstance*> (instance))
        {
            auto* gui = clap->getGui();
            if (gui != nullptr && gui->isSupported())
            {
                auto* view = new ClapView (*gui, *this);
                setContentOwned (view, true);
                centreWithSize (getWidth(), getHeight());
                setVisible (true);

                if (view->open())
                    return;

                clearContentComponent();
            }

            setContentOwned (new Message (TRANS ("This plugin has no window of its own.")), true);
        }
        else if (auto* editor = instance->createEditorAndMakeActive())
        {
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
        }
        else
        {
            setContentOwned (new Message (TRANS ("This plugin has no window of its own.")), true);
        }

        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    PluginWindow::~PluginWindow()
    {
        // The editor must go before the plugin.
        clearContentComponent();
    }

    void PluginWindow::closeButtonPressed()
    {
        owners().erase (this);
        windows().removeObject (this); // deletes this window
    }
}
