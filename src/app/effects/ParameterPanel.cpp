// SPDX-License-Identifier: AGPL-3.0-only
#include "app/effects/ParameterPanel.h"
#include "app/theme/Fonts.h"
#include "app/theme/Palette.h"

#include <cmath>

namespace mxj
{
    namespace
    {
        int rowHeight()   { return fonts::scaled (30); }
        int labelWidth (int panelWidth) { return juce::jmin (fonts::scaled (170), panelWidth * 2 / 5); }
        int curveHeight() { return fonts::scaled (150); }
        int meterHeight() { return fonts::scaled (34); }
    }

    //==============================================================================
    /** Gain of an equaliser or filter over frequency, 20 Hz to 20 kHz. */
    class ParameterPanel::ResponseCurve : public juce::Component
    {
    public:
        ResponseCurve (const dsp::EffectType& t, dsp::ParameterValuesPtr v, double sampleRate)
            : type (t), values (std::move (v)), rate (sampleRate > 0.0 ? sampleRate : 48000.0) {}

        bool update()
        {
            const auto now = values->getAll();
            if (now == shown)
                return false;

            shown = now;
            repaint();
            return true;
        }

        void paint (juce::Graphics& g) override
        {
            auto area = getLocalBounds().toFloat();
            g.setColour (palette::background);
            g.fillRoundedRectangle (area, palette::cornerRadius);
            area = area.reduced (1.0f);

            const auto xFor = [&] (double hz) { return area.getX() + area.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
            const auto yFor = [&] (double db) { return area.getCentreY() - area.getHeight() * 0.5f * (float) (db / range); };

            g.setFont (fonts::mono (10.0f));
            for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            {
                const auto x = xFor (hz);
                g.setColour (palette::line);
                g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
                g.setColour (palette::textDim);
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (x + 3.0f, area.getBottom() - 14.0f, 40.0f, 12.0f), juce::Justification::left);
            }

            for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
            {
                const auto y = yFor (db);
                g.setColour (db == 0.0 ? palette::line.brighter (0.3f) : palette::line);
                g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
                g.setColour (palette::textDim);
                g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db) + " dB",
                            juce::Rectangle<float> (area.getX() + 4.0f, y - 12.0f, 60.0f, 12.0f), juce::Justification::left);
            }

            juce::Path p;
            const int points = juce::jmax (2, (int) area.getWidth() / 2);
            for (int i = 0; i < points; ++i)
            {
                const auto hz = 20.0 * std::pow (1000.0, (double) i / (points - 1));
                const auto db = juce::jlimit (-range * 1.2, range * 1.2, (double) type.response (shown, rate, hz));
                const auto pt = juce::Point<float> (xFor (hz), yFor (db));
                if (i == 0)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
            }

            g.saveState();
            g.reduceClipRegion (area.toNearestInt());
            g.setColour (palette::accent);
            g.strokePath (p, juce::PathStrokeType (2.0f));
            g.restoreState();
        }

    private:
        static constexpr double range = 18.0; // dB from the middle to the top

        const dsp::EffectType& type;
        dsp::ParameterValuesPtr values;
        double rate;
        std::vector<float> shown;
    };

    //==============================================================================
    /** How much a dynamics effect turns the audio down right now. */
    class ParameterPanel::ReductionMeter : public juce::Component
    {
    public:
        explicit ReductionMeter (dsp::ParameterValuesPtr v) : values (std::move (v)) {}

        void update()
        {
            const auto now = values->gainReductionDb.load();
            // Fall back slowly, so short reductions stay readable.
            const auto next = now > shownDb ? now : shownDb * 0.85f + now * 0.15f;
            if (std::abs (next - shownDb) > 0.05f)
            {
                shownDb = next;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto area = getLocalBounds();
            g.setFont (fonts::sans (12.0f));
            g.setColour (palette::textDim);
            g.drawText (TRANS ("Gain reduction"), area.removeFromLeft (fonts::scaled (170)), juce::Justification::centredLeft);

            auto text = area.removeFromRight (fonts::scaled (70));
            g.setColour (palette::text);
            g.setFont (fonts::mono (12.0f));
            g.drawText ("-" + juce::String (shownDb, 1) + " dB", text, juce::Justification::centredRight);

            auto bar = area.reduced (0, area.getHeight() / 2 - fonts::scaled (5)).toFloat();
            g.setColour (palette::background);
            g.fillRoundedRectangle (bar, palette::cornerRadius);
            const auto fraction = juce::jlimit (0.0f, 1.0f, shownDb / 24.0f);
            g.setColour (palette::playhead);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * fraction), palette::cornerRadius);
        }

    private:
        dsp::ParameterValuesPtr values;
        float shownDb = 0.0f;
    };

    //==============================================================================
    ParameterPanel::ParameterPanel (const dsp::EffectType& t, dsp::ParameterValuesPtr v, double sampleRate)
        : type (t), values (std::move (v))
    {
        if (type.response != nullptr)
        {
            curve = std::make_unique<ResponseCurve> (type, values, sampleRate);
            addAndMakeVisible (*curve);
        }

        for (size_t i = 0; i < type.parameters.size(); ++i)
        {
            const auto& info = type.parameters[i];
            Row row;
            row.info = &info;
            row.index = (int) i;

            if (info.kind == dsp::ParameterInfo::Kind::number)
            {
                auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
                s->setRange (0.0, 1.0, 0.0);
                s->setTextBoxStyle (juce::Slider::TextBoxRight, false, fonts::scaled (84), fonts::scaled (24));
                s->textFromValueFunction = [p = &info] (double proportion) { return p->format (dsp::fromProportion (*p, (float) proportion)); };
                s->valueFromTextFunction = [p = &info] (const juce::String& text)
                {
                    auto number = text.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue();
                    if (text.containsIgnoreCase ("k") && (p->unit == "Hz"))
                        number *= 1000.0f;
                    if (text.trim().endsWithIgnoreCase (" s") && p->unit == "ms")
                        number *= 1000.0f;
                    return (double) dsp::toProportion (*p, number);
                };
                s->setDoubleClickReturnValue (true, dsp::toProportion (info, info.defaultValue));
                s->onValueChange = [this, idx = (int) i, p = &info, sl = s.get()]
                {
                    valueChanged (idx, dsp::fromProportion (*p, (float) sl->getValue()));
                };
                s->setTooltip (TRANS (info.name) + ": " + TRANS ("double-click to reset"));
                addAndMakeVisible (*s);
                row.slider = std::move (s);
            }
            else if (info.kind == dsp::ParameterInfo::Kind::choice)
            {
                auto c = std::make_unique<juce::ComboBox>();
                for (int k = 0; k < info.choices.size(); ++k)
                    c->addItem (TRANS (info.choices[k]), k + 1);

                c->onChange = [this, idx = (int) i, cb = c.get()] { valueChanged (idx, (float) (cb->getSelectedItemIndex())); };
                addAndMakeVisible (*c);
                row.choice = std::move (c);
            }
            else
            {
                auto b = std::make_unique<juce::ToggleButton> (TRANS (info.name));
                b->onClick = [this, idx = (int) i, tb = b.get()] { valueChanged (idx, tb->getToggleState() ? 1.0f : 0.0f); };
                addAndMakeVisible (*b);
                row.toggle = std::move (b);
            }

            rows.push_back (std::move (row));
        }

        if (type.showsGainReduction)
        {
            reduction = std::make_unique<ReductionMeter> (values);
            addAndMakeVisible (*reduction);
        }

        refreshFromValues();
        startTimerHz (25);
    }

    ParameterPanel::~ParameterPanel() = default;

    void ParameterPanel::refreshFromValues()
    {
        for (auto& row : rows)
        {
            const auto v = values->get (row.index);
            if (row.slider != nullptr)
                row.slider->setValue (dsp::toProportion (*row.info, v), juce::dontSendNotification);
            if (row.choice != nullptr)
                row.choice->setSelectedItemIndex (juce::roundToInt (v), juce::dontSendNotification);
            if (row.toggle != nullptr)
                row.toggle->setToggleState (v > 0.5f, juce::dontSendNotification);
        }

        if (curve != nullptr)
            curve->update();
    }

    void ParameterPanel::valueChanged (int index, float value)
    {
        values->set (index, type.parameters[(size_t) index].limit (value));

        if (curve != nullptr)
            curve->update();

        if (onChange != nullptr)
            onChange();
    }

    int ParameterPanel::getIdealHeight() const
    {
        int h = (int) rows.size() * rowHeight() + 8;
        if (curve != nullptr)
            h += curveHeight() + 12;
        if (reduction != nullptr)
            h += meterHeight() + 6;

        return h;
    }

    void ParameterPanel::timerCallback()
    {
        if (reduction != nullptr)
            reduction->update();
        if (curve != nullptr)
            curve->update(); // values may also change from a preset or the rack
    }

    void ParameterPanel::resized()
    {
        auto area = getLocalBounds();

        if (curve != nullptr)
        {
            curve->setBounds (area.removeFromTop (curveHeight()));
            area.removeFromTop (12);
        }

        for (auto& row : rows)
        {
            auto r = area.removeFromTop (rowHeight()).reduced (0, 3);
            if (row.toggle != nullptr)
            {
                row.toggle->setBounds (r.withTrimmedLeft (labelWidth (getWidth()) - 4));
                continue;
            }

            r.removeFromLeft (labelWidth (getWidth()));
            if (row.slider != nullptr)
                row.slider->setBounds (r);
            if (row.choice != nullptr)
                row.choice->setBounds (r.withWidth (juce::jmin (r.getWidth(), fonts::scaled (240))));
        }

        if (reduction != nullptr)
        {
            area.removeFromTop (6);
            reduction->setBounds (area.removeFromTop (meterHeight()));
        }
    }

    void ParameterPanel::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds();
        if (curve != nullptr)
            area.removeFromTop (curveHeight() + 12);

        g.setFont (fonts::sans (13.0f));
        g.setColour (palette::textDim);

        for (const auto& row : rows)
        {
            auto r = area.removeFromTop (rowHeight());
            if (row.toggle == nullptr)
                g.drawFittedText (TRANS (row.info->name), r.removeFromLeft (labelWidth (getWidth()) - 8), juce::Justification::centredLeft, 1, 0.75f);
        }
    }
}
