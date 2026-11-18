// SPDX-License-Identifier: AGPL-3.0-only
#include "plugins/clap/ClapFormat.h"
#include "core/Version.h"

#include <clap/clap.h>

#include <map>
#include <mutex>

namespace mxj::plugins
{
    namespace
    {
        const char* platformGuiApi()
        {
           #if JUCE_WINDOWS
            return CLAP_WINDOW_API_WIN32;
           #elif JUCE_MAC
            return CLAP_WINDOW_API_COCOA;
           #else
            return CLAP_WINDOW_API_X11;
           #endif
        }

        /** True while this thread is inside the plugin's process() or flush(). */
        thread_local bool inAudioCall = false;

        //==============================================================================
        /** A loaded .clap file. Shared by its plugins; unloaded with the last one. */
        class ClapModule
        {
        public:
            static std::shared_ptr<ClapModule> load (const juce::File& file, juce::String& error)
            {
                static std::mutex lock;
                static std::map<juce::String, std::weak_ptr<ClapModule>> loaded;

                const std::lock_guard<std::mutex> l (lock);
                const auto key = file.getFullPathName();
                if (auto existing = loaded[key].lock())
                    return existing;

                auto module = std::shared_ptr<ClapModule> (new ClapModule());
                if (! module->library.open (key))
                {
                    error = "The CLAP file could not be loaded: " + key;
                    return nullptr;
                }

                module->entry = reinterpret_cast<const clap_plugin_entry_t*> (module->library.getFunction ("clap_entry"));
                if (module->entry == nullptr || ! clap_version_is_compatible (module->entry->clap_version))
                {
                    module->entry = nullptr;
                    error = "Not a CLAP plugin, or a CLAP version this program does not know: " + key;
                    return nullptr;
                }

                if (! module->entry->init (key.toRawUTF8()))
                {
                    module->entry = nullptr;
                    error = "The CLAP plugin did not start: " + key;
                    return nullptr;
                }

                loaded[key] = module;
                return module;
            }

            ~ClapModule()
            {
                if (entry != nullptr)
                    entry->deinit();

                library.close();
            }

            const clap_plugin_factory_t* getFactory() const
            {
                return entry != nullptr ? static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID))
                                        : nullptr;
            }

        private:
            ClapModule() = default;

            juce::DynamicLibrary library;
            const clap_plugin_entry_t* entry = nullptr;
        };

        bool hasFeature (const clap_plugin_descriptor_t* d, const char* feature)
        {
            if (d->features != nullptr)
                for (auto* f = d->features; *f != nullptr; ++f)
                    if (std::strcmp (*f, feature) == 0)
                        return true;

            return false;
        }

        void describe (juce::PluginDescription& d, const clap_plugin_descriptor_t* c, const juce::File& file, int count)
        {
            d.name = juce::String::fromUTF8 (c->name != nullptr ? c->name : c->id);
            d.descriptiveName = juce::String::fromUTF8 (c->description != nullptr ? c->description : "");
            d.pluginFormatName = ClapPluginFormat::getFormatName();
            d.manufacturerName = juce::String::fromUTF8 (c->vendor != nullptr ? c->vendor : "");
            d.version = juce::String::fromUTF8 (c->version != nullptr ? c->version : "");
            d.fileOrIdentifier = file.getFullPathName();
            d.lastFileModTime = file.getLastModificationTime();
            d.lastInfoUpdateTime = juce::Time::getCurrentTime();
            d.uniqueId = d.deprecatedUid = juce::String (c->id).hashCode();
            d.isInstrument = hasFeature (c, CLAP_PLUGIN_FEATURE_INSTRUMENT);
            d.category = hasFeature (c, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT) ? "Effect"
                       : d.isInstrument ? "Instrument" : "Other";
            d.numInputChannels = d.isInstrument ? 0 : 2;
            d.numOutputChannels = 2;
            d.hasSharedContainer = count > 1;
        }

        //==============================================================================
        class ClapInstance;

        /** The host structure the plugin talks to. Made before the instance (the
            plugin must exist to know its ports, which the instance's buses need). */
        struct HostData
        {
            clap_host_t host {};
            ClapInstance* owner = nullptr; ///< nullptr until the instance exists
        };

        int mainPortChannels (const clap_plugin_t* plugin, const clap_plugin_audio_ports_t* ports, bool input)
        {
            if (ports == nullptr)
                return 2;

            const auto count = ports->count (plugin, input);
            for (uint32_t i = 0; i < count; ++i)
            {
                clap_audio_port_info_t info {};
                if (ports->get (plugin, i, input, &info) && (info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0)
                    return juce::jlimit (0, 2, (int) info.channel_count);
            }

            if (count > 0)
            {
                clap_audio_port_info_t info {};
                if (ports->get (plugin, 0, input, &info))
                    return juce::jlimit (0, 2, (int) info.channel_count);
            }

            return 0;
        }

        class ClapInstance final : public ClapPluginInstance,
                                   private ClapPluginInstance::Gui
        {
        public:
            /** Creates and initialises the plugin. nullptr if it refuses. */
            static std::unique_ptr<ClapInstance> create (std::shared_ptr<ClapModule> module, const clap_plugin_descriptor_t* descriptor,
                                                         const juce::PluginDescription& d)
            {
                auto data = std::make_unique<HostData>();
                auto& host = data->host;
                host.clap_version = CLAP_VERSION;
                host.host_data = data.get();
                host.name = "MXJ AUDIO";
                host.vendor = "MXJ AUDIO";
                host.url = "https://github.com/supercontra93/mxj-audio";
                host.version = version::string;
                host.get_extension = [] (const clap_host_t*, const char* id) -> const void* { return getHostExtension (id); };
                host.request_restart = [] (const clap_host_t* h) { if (auto* i = self (h)) i->requestRestart(); };
                host.request_process = [] (const clap_host_t*) {};
                host.request_callback = [] (const clap_host_t* h) { if (auto* i = self (h)) i->requestCallback(); };

                const auto* factory = module->getFactory();
                const auto* plugin = factory != nullptr ? factory->create_plugin (factory, &host, descriptor->id) : nullptr;
                if (plugin == nullptr)
                    return nullptr;

                if (! plugin->init (plugin))
                {
                    plugin->destroy (plugin);
                    return nullptr;
                }

                const auto* ports = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
                const auto numIn = mainPortChannels (plugin, ports, true);
                const auto numOut = mainPortChannels (plugin, ports, false);

                return std::unique_ptr<ClapInstance> (new ClapInstance (std::move (module), std::move (data), plugin, d, numIn, numOut));
            }

            ~ClapInstance() override
            {
                *alive = false;

                if (guiCreated)
                    close();

                deactivate();
                plugin->destroy (plugin);
            }

            //==============================================================================
            const juce::String getName() const override { return description.name; }

            void fillInPluginDescription (juce::PluginDescription& d) const override
            {
                d = description;
                d.numInputChannels = numIn;
                d.numOutputChannels = numOut;
            }

            void prepareToPlay (double sampleRate, int maximumBlockSize) override
            {
                deactivate();

                maxFrames = juce::jmax (1, maximumBlockSize);
                rate = sampleRate;
                inputCopy.setSize (juce::jmax (1, numIn), maxFrames);
                inputPointers.assign ((size_t) juce::jmax (1, numIn), nullptr);
                outputPointers.assign ((size_t) juce::jmax (1, numOut), nullptr);

                if (plugin->activate (plugin, sampleRate, 1, (uint32_t) maxFrames))
                {
                    active = true;
                    if (latency != nullptr)
                        setLatencySamples ((int) latency->get (plugin));
                }
            }

            void releaseResources() override
            {
                deactivate();
            }

            void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
            {
                const auto n = buffer.getNumSamples();
                if (! active || n <= 0 || n > maxFrames || busy.exchange (true, std::memory_order_acquire))
                    return; // leaves the audio unchanged

                const juce::ScopedValueSetter<bool> audio (inAudioCall, true);

                if (! processing)
                    processing = plugin->start_processing == nullptr || plugin->start_processing (plugin);

                if (processing)
                {
                    // The plugin gets its input in a copy, so it may write its output in place.
                    for (int c = 0; c < numIn; ++c)
                    {
                        inputCopy.copyFrom (c, 0, buffer, juce::jmin (c, buffer.getNumChannels() - 1), 0, n);
                        inputPointers[(size_t) c] = inputCopy.getWritePointer (c);
                    }

                    for (int c = 0; c < numOut; ++c)
                        outputPointers[(size_t) c] = buffer.getWritePointer (juce::jmin (c, buffer.getNumChannels() - 1));

                    clap_audio_buffer_t in {};
                    in.data32 = inputPointers.data();
                    in.channel_count = (uint32_t) numIn;

                    clap_audio_buffer_t out {};
                    out.data32 = outputPointers.data();
                    out.channel_count = (uint32_t) numOut;

                    clap_process_t p {};
                    p.steady_time = steadyTime;
                    p.frames_count = (uint32_t) n;
                    p.audio_inputs = numIn > 0 ? &in : nullptr;
                    p.audio_inputs_count = numIn > 0 ? 1u : 0u;
                    p.audio_outputs = numOut > 0 ? &out : nullptr;
                    p.audio_outputs_count = numOut > 0 ? 1u : 0u;
                    p.in_events = &noEvents;
                    p.out_events = &ignoredEvents;

                    if (plugin->process (plugin, &p) == CLAP_PROCESS_ERROR)
                        buffer.clear();

                    steadyTime += n;
                }

                busy.store (false, std::memory_order_release);
            }

            double getTailLengthSeconds() const override { return 0.0; }
            bool acceptsMidi() const override  { return false; }
            bool producesMidi() const override { return false; }

            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }

            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}

            bool isBusesLayoutSupported (const BusesLayout& layout) const override
            {
                return layout.getMainInputChannels() == numIn && layout.getMainOutputChannels() == numOut;
            }

            void getStateInformation (juce::MemoryBlock& data) override
            {
                data.reset();
                if (state == nullptr)
                    return;

                juce::MemoryOutputStream stream (data, false);
                clap_ostream_t s {};
                s.ctx = &stream;
                s.write = [] (const clap_ostream_t* o, const void* buffer, uint64_t count) -> int64_t
                {
                    return static_cast<juce::MemoryOutputStream*> (o->ctx)->write (buffer, (size_t) count) ? (int64_t) count : -1;
                };

                if (! state->save (plugin, &s))
                    data.reset();
            }

            void setStateInformation (const void* data, int size) override
            {
                if (state == nullptr || size <= 0)
                    return;

                juce::MemoryInputStream stream (data, (size_t) size, false);
                clap_istream_t s {};
                s.ctx = &stream;
                s.read = [] (const clap_istream_t* i, void* buffer, uint64_t count) -> int64_t
                {
                    return static_cast<juce::MemoryInputStream*> (i->ctx)->read (buffer, (int) juce::jmin (count, (uint64_t) 1 << 30));
                };

                state->load (plugin, &s);
                updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
            }

            ClapPluginInstance::Gui* getGui() override { return gui != nullptr ? this : nullptr; }

        private:
            static BusesProperties busesFor (int numIn, int numOut)
            {
                BusesProperties buses;
                if (numIn > 0)
                    buses = buses.withInput ("Input", juce::AudioChannelSet::canonicalChannelSet (numIn), true);
                if (numOut > 0)
                    buses = buses.withOutput ("Output", juce::AudioChannelSet::canonicalChannelSet (numOut), true);
                return buses;
            }

            ClapInstance (std::shared_ptr<ClapModule> m, std::unique_ptr<HostData> data, const clap_plugin_t* p,
                          const juce::PluginDescription& d, int ins, int outs)
                : ClapPluginInstance (busesFor (ins, outs)), module (std::move (m)), hostData (std::move (data)),
                  plugin (p), description (d), numIn (ins), numOut (outs)
            {
                hostData->owner = this;
                params  = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
                state   = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
                latency = static_cast<const clap_plugin_latency_t*> (plugin->get_extension (plugin, CLAP_EXT_LATENCY));
                gui     = static_cast<const clap_plugin_gui_t*> (plugin->get_extension (plugin, CLAP_EXT_GUI));
            }

            //==============================================================================
            // Gui
            bool isSupported() const override
            {
                return gui != nullptr && gui->is_api_supported (plugin, platformGuiApi(), false);
            }

            bool open (void* nativeParent, double scale, int& width, int& height) override
            {
                if (! isSupported() || guiCreated)
                    return false;

                if (! gui->create (plugin, platformGuiApi(), false))
                    return false;

                guiCreated = true;
                gui->set_scale (plugin, scale);

                clap_window_t window {};
                window.api = platformGuiApi();
               #if JUCE_WINDOWS || JUCE_MAC
                window.ptr = nativeParent;
               #else
                window.x11 = (clap_xwnd) (juce::pointer_sized_uint) nativeParent;
               #endif

                if (! gui->set_parent (plugin, &window))
                {
                    close();
                    return false;
                }

                uint32_t w = 400, h = 300;
                gui->get_size (plugin, &w, &h);
                width = (int) w;
                height = (int) h;
                gui->show (plugin);
                return true;
            }

            void close() override
            {
                if (! guiCreated)
                    return;

                gui->hide (plugin);
                gui->destroy (plugin);
                guiCreated = false;
            }

            bool canResize() const override
            {
                return guiCreated && gui->can_resize (plugin);
            }

            std::pair<int, int> resize (int width, int height) override
            {
                auto w = (uint32_t) juce::jmax (1, width), h = (uint32_t) juce::jmax (1, height);
                if (! guiCreated)
                    return {};

                if (gui->can_resize (plugin))
                {
                    gui->adjust_size (plugin, &w, &h);
                    gui->set_size (plugin, w, h);
                }
                else
                {
                    gui->get_size (plugin, &w, &h);
                }

                return { (int) w, (int) h };
            }

            //==============================================================================
            static ClapInstance* self (const clap_host_t* h) { return static_cast<HostData*> (h->host_data)->owner; }

            /** Runs `f` later on the message thread, if this plugin still exists then. */
            void later (std::function<void (ClapInstance&)> f)
            {
                juce::MessageManager::callAsync ([this, token = alive, f]
                {
                    if (*token)
                        f (*this);
                });
            }

            void requestCallback()
            {
                later ([] (ClapInstance& i)
                {
                    if (i.plugin->on_main_thread != nullptr)
                        i.plugin->on_main_thread (i.plugin);
                });
            }

            void requestRestart()
            {
                later ([] (ClapInstance& i)
                {
                    if (i.active)
                        i.withAudioStopped ([&i] { i.prepareToPlay (i.rate, i.maxFrames); });
                });
            }

            void requestFlush()
            {
                later ([] (ClapInstance& i)
                {
                    if (i.params == nullptr)
                        return;

                    // While playing, process() takes the changes. When stopped, flush here.
                    i.withAudioStopped ([&i]
                    {
                        const juce::ScopedValueSetter<bool> audio (inAudioCall, i.active);
                        i.params->flush (i.plugin, &noEvents, &ignoredEvents);
                    });
                });
            }

            /** Runs `f` while process() is not running (it passes the audio through meanwhile). */
            void withAudioStopped (const std::function<void()>& f)
            {
                while (busy.exchange (true, std::memory_order_acquire))
                    juce::Thread::yield();

                f();
                busy.store (false, std::memory_order_release);
            }

            void deactivate()
            {
                if (processing && plugin->stop_processing != nullptr)
                {
                    const juce::ScopedValueSetter<bool> audio (inAudioCall, true);
                    plugin->stop_processing (plugin);
                }

                processing = false;

                if (active)
                    plugin->deactivate (plugin);

                active = false;
            }

            //==============================================================================
            static const void* getHostExtension (const char* id)
            {
                static const clap_host_log_t log { [] (const clap_host_t*, clap_log_severity, const char* message)
                {
                    juce::ignoreUnused (message);
                    DBG ("CLAP: " << message);
                } };

                static const clap_host_thread_check_t threadCheck {
                    [] (const clap_host_t*) { return juce::MessageManager::getInstance()->isThisTheMessageThread(); },
                    [] (const clap_host_t*) { return inAudioCall; } };

                static const clap_host_params_t hostParams {
                    [] (const clap_host_t*, clap_param_rescan_flags) {},
                    [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
                    [] (const clap_host_t* h) { if (auto* i = self (h)) i->requestFlush(); } };

                static const clap_host_latency_t hostLatency { [] (const clap_host_t* h)
                {
                    if (self (h) == nullptr) return;
                    self (h)->later ([] (ClapInstance& i)
                    {
                        if (i.latency != nullptr && i.active)
                            i.setLatencySamples ((int) i.latency->get (i.plugin));
                    });
                } };

                static const clap_host_state_t hostState { [] (const clap_host_t* h)
                {
                    if (self (h) == nullptr) return;
                    self (h)->later ([] (ClapInstance& i)
                    {
                        i.updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
                    });
                } };

                static const clap_host_gui_t hostGui {
                    [] (const clap_host_t*) {},
                    [] (const clap_host_t* h, uint32_t width, uint32_t height)
                    {
                        if (self (h) == nullptr) return false;
                        self (h)->later ([width, height] (ClapInstance& i)
                        {
                            if (i.onResizeRequest != nullptr)
                                i.onResizeRequest ((int) width, (int) height);
                        });
                        return true;
                    },
                    [] (const clap_host_t*) { return false; },
                    [] (const clap_host_t*) { return false; },
                    [] (const clap_host_t* h, bool)
                    {
                        if (self (h) == nullptr) return;
                        self (h)->later ([] (ClapInstance& i)
                        {
                            i.guiCreated = false;
                            if (i.onClosedByPlugin != nullptr)
                                i.onClosedByPlugin();
                        });
                    } };

                static const clap_host_audio_ports_t hostPorts {
                    [] (const clap_host_t*, uint32_t) { return false; },
                    [] (const clap_host_t*, uint32_t) {} };

                const juce::String name (id);
                if (name == CLAP_EXT_LOG)          return &log;
                if (name == CLAP_EXT_THREAD_CHECK) return &threadCheck;
                if (name == CLAP_EXT_PARAMS)       return &hostParams;
                if (name == CLAP_EXT_LATENCY)      return &hostLatency;
                if (name == CLAP_EXT_STATE)        return &hostState;
                if (name == CLAP_EXT_GUI)          return &hostGui;
                if (name == CLAP_EXT_AUDIO_PORTS)  return &hostPorts;
                return nullptr;
            }

            static inline const clap_input_events_t noEvents {
                nullptr,
                [] (const clap_input_events_t*) -> uint32_t { return 0; },
                [] (const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; } };

            static inline const clap_output_events_t ignoredEvents {
                nullptr,
                [] (const clap_output_events_t*, const clap_event_header_t*) { return true; } };

            //==============================================================================
            std::shared_ptr<ClapModule> module;
            std::unique_ptr<HostData> hostData;
            const clap_plugin_t* plugin = nullptr;
            juce::PluginDescription description;
            const clap_plugin_params_t* params = nullptr;
            const clap_plugin_state_t* state = nullptr;
            const clap_plugin_latency_t* latency = nullptr;
            const clap_plugin_gui_t* gui = nullptr;

            int numIn = 2, numOut = 2, maxFrames = 0;
            double rate = 48000.0;
            bool active = false, processing = false, guiCreated = false;
            std::atomic<bool> busy { false };
            juce::int64 steadyTime = 0;
            juce::AudioBuffer<float> inputCopy;
            std::vector<float*> inputPointers, outputPointers;
            std::shared_ptr<bool> alive = std::make_shared<bool> (true);

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapInstance)
        };
    }

    //==============================================================================
    void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& path)
    {
        const juce::File file (path);
        juce::String error;
        const auto module = ClapModule::load (file, error);
        const auto* factory = module != nullptr ? module->getFactory() : nullptr;
        if (factory == nullptr)
            return;

        const auto count = (int) factory->get_plugin_count (factory);
        for (int i = 0; i < count; ++i)
        {
            const auto* d = factory->get_plugin_descriptor (factory, (uint32_t) i);
            if (d == nullptr || d->id == nullptr || ! clap_version_is_compatible (d->clap_version))
                continue;

            auto description = std::make_unique<juce::PluginDescription>();
            describe (*description, d, file, count);
            results.add (description.release());
        }
    }

    bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& path)
    {
        const juce::File f (path);
        return f.hasFileExtension ("clap") && f.exists();
    }

    juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& path)
    {
        return juce::File (path).getFileNameWithoutExtension();
    }

    bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& d)
    {
        return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
    }

    bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& d)
    {
        return juce::File (d.fileOrIdentifier).exists();
    }

    juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& paths, bool recursive, bool)
    {
        juce::StringArray result;
        for (int i = 0; i < paths.getNumPaths(); ++i)
        {
            const auto folder = paths.getRawString (i);
            for (const auto& entry : juce::RangedDirectoryIterator (juce::File (folder), recursive, "*.clap",
                                                                    juce::File::findFilesAndDirectories))
            {
                // On macOS a .clap is a bundle (folder); elsewhere it is a file.
                const auto f = entry.getFile();
               #if ! JUCE_MAC
                if (f.isDirectory())
                    continue;
               #endif
                result.addIfNotAlreadyThere (f.getFullPathName());
            }
        }

        return result;
    }

    juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
    {
        juce::FileSearchPath paths;
       #if JUCE_WINDOWS
        paths.add (juce::File (juce::SystemStats::getEnvironmentVariable ("COMMONPROGRAMFILES", "C:\\Program Files\\Common Files"))
                       .getChildFile ("CLAP"));
        paths.add (juce::File (juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {}))
                       .getChildFile ("Programs").getChildFile ("Common").getChildFile ("CLAP"));
       #elif JUCE_MAC
        paths.add (juce::File ("/Library/Audio/Plug-Ins/CLAP"));
        paths.add (juce::File ("~/Library/Audio/Plug-Ins/CLAP"));
       #else
        paths.add (juce::File ("~/.clap"));
        paths.add (juce::File ("/usr/lib/clap"));
       #endif
        return paths;
    }

    void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& d, double rate, int blockSize,
                                                 PluginCreationCallback callback)
    {
        juce::String error;
        const auto module = ClapModule::load (juce::File (d.fileOrIdentifier), error);
        const auto* factory = module != nullptr ? module->getFactory() : nullptr;

        if (factory != nullptr)
        {
            const auto count = factory->get_plugin_count (factory);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto* descriptor = factory->get_plugin_descriptor (factory, i);
                if (descriptor == nullptr || descriptor->id == nullptr || juce::String (descriptor->id).hashCode() != d.uniqueId)
                    continue;

                auto instance = ClapInstance::create (module, descriptor, d);
                if (instance == nullptr)
                {
                    callback (nullptr, "The CLAP plugin could not be created: " + d.name);
                    return;
                }

                instance->setRateAndBufferSizeDetails (rate, blockSize);
                callback (std::move (instance), {});
                return;
            }

            error = "The plugin is no longer in its file: " + d.name;
        }

        callback (nullptr, error);
    }
}
