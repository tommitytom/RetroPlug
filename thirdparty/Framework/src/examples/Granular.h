#pragma once

#include <cmath>

#include "foundation/Event.h"
#include "foundation/Math.h"
#include "audio/AudioManager.h"
#include "audio/Granular.h"
#include "audio/Oscillators.h"
#include "audio/SampleLoaderUtil.h"
#include "ui/KnobView.h"
#include "ui/PlotView.h"
#include "ui/SliderView.h"
#include "ui/TextureView.h"
#include "ui/View.h"
#include "ui/WaveformUtil.h"
#include "ui/WaveView.h"

namespace fw {
	using NoteIndex = uint32;

	enum class ParameterType {
		Unknown,
		Amp,
		Pitch,
		Stretch,
		Overlap,
		GrainSize
	};

	struct ParameterChangeEvent {
		ParameterType type = ParameterType::Unknown;
		f32 value = 0.0f;
	};

	struct NoteParameterChangeEvent {
		NoteIndex note = 0;
		ParameterType type = ParameterType::Unknown;
		f32 value = 0.0f;
	};

	struct PlayNoteEvent {
		NoteIndex note = 0;
	};

	struct StopNoteEvent {
		NoteIndex note = 0;
	};

	struct StopAllNotesEvent {};

	struct TogglePauseEvent {};

	struct AudioRenderEvent {
		StereoAudioBuffer buffer;
	};

	struct NoteParameters {
		union {
			struct {
				f32 amp;
				f32 pitch;
				f32 stretch;
				f32 overlap;
				f32 grainSize;
			};
			f32 values[5] = { 0.0f };
		};
	};

	struct Parameters {
		NoteParameters noteParameters;
	};

	struct Note {
		StereoAudioBuffer buffer;
		uint64 offset = 0;
		NoteParameters parameters;

		bool contains(uint64 pos) {
			if (!buffer.isEmpty()) {
				return pos >= offset && pos < offset + buffer.getFrameCount();
			}

			return false;
		}

		uint64 getEnd() const {
			return offset + buffer.getFrameCount();
		}
	};

	using NoteArray = std::array<Note, 128>;

	struct SetNoteEvent {
		NoteIndex index = 0;
		Note note;
	};

	struct SetNoteArrayEvent {
		NoteArray notes;
	};

	namespace AudioBufferUtil {
		template <const uint32 ChannelCount, typename T = AudioSampleT>
		void mul(InterleavedAudioBuffer<ChannelCount, T>& buffer, const InterleavedAudioBuffer<ChannelCount, T>& other) {

		}

		template <const uint32 ChannelCount, typename T = AudioSampleT>
		void mul(InterleavedAudioBuffer<ChannelCount, T>& buffer, T value) {
			for (uint32 i = 0; i < buffer.getFrameCount(); ++i) {
				buffer[i] *= value;
			}
		}
	}

	enum class VoiceState {
		Inactive,
		Active,
		Releasing,
		Finished
	};

	class Voice {
	private:
		VoiceState _state = VoiceState::Inactive;

	public:
		virtual void process(StereoAudioBuffer& buffer) = 0;

		VoiceState getState() const {
			return _state;
		}

		void setState(VoiceState state) {
			_state = state;
		}
	};

	using VoicePtr = std::unique_ptr<Voice>;

	class VoiceManager {
	private:
		std::unordered_map<NoteIndex, VoicePtr> _voices;

	public:
		void addVoice(NoteIndex note, VoicePtr&& voice) {
			auto found = _voices.find(note);

			if (found != _voices.end()) {
				// Is this actually needed?
				switch (found->second->getState()) {
				case VoiceState::Active: break; // stop immediately
				case VoiceState::Releasing: break; //stop immediately
				}
			}

			_voices[note] = std::move(voice);
		}

		void stopVoice(NoteIndex idx) {
			_voices.erase(idx);
		}

		void stopAll() {
			_voices.clear();
		}

		Voice* getVoiceForNote(NoteIndex note) {
			auto found = _voices.find(note);

			if (found != _voices.end()) {
				return found->second.get();
			}

			return nullptr;
		}

		void process(StereoAudioBuffer& buffer) {
			for (auto it = _voices.begin(); it != _voices.end();) {
				Voice* voice = it->second.get();

				voice->process(buffer);

				if (voice->getState() == VoiceState::Finished) {
					it = _voices.erase(it);
				} else {
					++it;
				}
			}
		}
	};

	class GrainSamplerVoice final : public Voice {
	private:
		GranularTimeStretch _timeStretch;
		f32 _amp = 1.0f;
		f32 _sampleRate;

	public:
		GrainSamplerVoice(StereoAudioBuffer&& buffer, const NoteParameters& params, f32 sampleRate): _sampleRate(sampleRate), _timeStretch(sampleRate) {
			_timeStretch.setInput(std::move(buffer));

			setAmp(params.amp);
			getTimeStretch().setPitch(params.pitch);
			getTimeStretch().setStretch(params.stretch);
			getTimeStretch().setOverlap(params.overlap);
			getTimeStretch().setGrainSize(params.grainSize);
		}

		void setAmp(f32 amp) {
			_amp = amp;
		}

		GranularTimeStretch& getTimeStretch() {
			return _timeStretch;
		}

		void process(StereoAudioBuffer& buffer) override {
			_timeStretch.process(buffer);

			if (_timeStretch.hasFinished()) {
				setState(VoiceState::Finished);
			}
		}

		void beginRelease() {
			// TODO: Apply short envelope to avoid clicks
			setState(VoiceState::Finished);
		}
	};

	class MyAudioProceessor : public AudioProcessor {
	private:
		EventNode _eventNode;

		VoiceManager _voiceManager;
		std::array<Note, 128> _notes;
		f32 _amp = 0.0f;

		Parameters _parameters;

	public:
		MyAudioProceessor(EventNode&& eventNode, const NoteArray& notes): _eventNode(std::move(eventNode)), _notes(notes) {
			_eventNode.subscribe<ParameterChangeEvent>([&](const ParameterChangeEvent& ev) {
				switch (ev.type) {
				case ParameterType::Amp: _amp = ev.value; break;
				}
			});

			_eventNode.subscribe<NoteParameterChangeEvent>([&](const NoteParameterChangeEvent& ev) {
				_notes[ev.note].parameters.values[(uint32)ev.type - 1] = ev.value;

				GrainSamplerVoice* voice = (GrainSamplerVoice*)_voiceManager.getVoiceForNote(ev.note);

				if (voice) {
					switch (ev.type) {
						case ParameterType::Amp: voice->setAmp(ev.value); break;
						case ParameterType::Pitch: voice->getTimeStretch().setPitch(ev.value); break;
						case ParameterType::Stretch: voice->getTimeStretch().setStretch(ev.value); break;
						case ParameterType::Overlap: voice->getTimeStretch().setOverlap(ev.value); break;
						case ParameterType::GrainSize: voice->getTimeStretch().setGrainSize(ev.value); break;
					}
				}
			});

			_eventNode.subscribe<SetNoteEvent>([&](const SetNoteEvent& ev) {
				_notes[ev.index] = ev.note;
			});

			_eventNode.subscribe<SetNoteArrayEvent>([&](const SetNoteArrayEvent& ev) {
				_notes = ev.notes;
			});

			_eventNode.subscribe<PlayNoteEvent>([&](const PlayNoteEvent& ev) { playNote(ev.note); });

			_eventNode.subscribe<StopNoteEvent>([&](const StopNoteEvent& ev) { stopNote(ev.note); });

			_eventNode.subscribe<StopAllNotesEvent>([&]() { stopAllNotes(); });
		}

		~MyAudioProceessor() = default;

		void playNote(NoteIndex note) {
			Note& noteData = _notes[note];

			if (noteData.buffer.getFrameCount()) {
				_voiceManager.addVoice(note, std::make_unique<GrainSamplerVoice>(noteData.buffer.ref(), noteData.parameters, getSampleRate()));
				_eventNode.broadcast(PlayNoteEvent{ note });
			}
		}

		void stopNote(NoteIndex note) {
			_voiceManager.stopVoice(note);
			_eventNode.broadcast(StopNoteEvent{ note });
		}

		void stopAllNotes() {
			_voiceManager.stopAll();
			_eventNode.broadcast<StopAllNotesEvent>();
		}

		void onRender(f32* output, const f32* input, uint32 frameCount) override {
			_eventNode.update();

			StereoAudioBuffer out((StereoAudioBuffer::Frame*)output, frameCount, getSampleRate());
			out.clear();

			_voiceManager.process(out);

			AudioBufferUtil::mul(out, _amp);

			if (_eventNode.hasSubscribers<AudioRenderEvent>()) {
				_eventNode.broadcast(AudioRenderEvent{ out.clone() });
			}
		}

		f32 getSampleRate() const {
			return 48000.0f;
		}
	};

//	using namespace std::placeholders;

	struct SampleClickEvent {
		uint32 index = 0;
		bool pressed = false;
	};

	class SamplePlayerOverlay final : public Overlay<WaveView> {
	private:
		NoteArray& _notes;

		std::unordered_map<uint32, Rect> _highlights;
		uint32 _lastClicked = 0;

	public:
		SamplePlayerOverlay(NoteArray& notes): _notes(notes) {
			setType<SamplePlayerOverlay>();
		}

		void onInitialize() override {
			subscribe<ZoomChangedEvent>(getSuper()->shared_from_this(), [this](const ZoomChangedEvent& ev) {
				updateHighlights();
			});
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			if (button == MouseButton::Left) {
				if (down) {
					uint64 sampleOffset = getSuper()->pixelToSample((f32)position.x);

					for (size_t i = 0; i < _notes.size(); ++i) {
						if (sampleOffset > _notes[i].offset && sampleOffset < _notes[i].offset + _notes[i].buffer.getFrameCount()) {
							_lastClicked = (uint32)i;
							emit(SampleClickEvent{ _lastClicked, true });
							break;
						}
					}
				} else {
					emit(SampleClickEvent{ _lastClicked, false });
				}
			}

			return true;
		}

		void onResize(const ResizeEvent& ev) {
			updateHighlights();
		}

		void setHighlight(uint32 index, bool highlight) {
			if (highlight) {
				_highlights[index] = getDimensions();
				updateHighlights();
			} else {
				_highlights.erase(index);
			}
		}

		void removeHighlights() {
			_highlights.clear();
		}

		void onRender(Canvas& canvas) override {
			for (auto& hl : _highlights) {
				if (hl.second.w > 0) {
					canvas.fillRect(hl.second, Color4F(1, 1, 1, 0.5f));
				}
			}
		}

		void updateHighlights() {
			WaveViewPtr parent = getSuper();
			f32 w = getDimensionsF().w;

			for (auto& hl : _highlights) {
				const Note& note = _notes[hl.first];

				f32 startPixel;
				f32 endPixel;

				bool startVisible = parent->sampleToPixel((size_t)note.offset, startPixel);
				bool endVisible = parent->sampleToPixel((size_t)note.getEnd(), endPixel);

				if (startPixel > w || endPixel < 0) {
					hl.second = Rect();
				} else {
					if (!startVisible) {
						startPixel = 0.0f;
					}

					if (!endPixel) {
						endPixel = w;
					}

					hl.second = (Rect)RectF(startPixel, 0, endPixel - startPixel, getDimensionsF().h);
				}
			}
		}
	};

	using SamplePlayerOverlayPtr = std::shared_ptr<SamplePlayerOverlay>;

	class Granular final : public View {
	private:
		StereoAudioBuffer _waveformBuffer;
		WaveViewPtr _waveView;
		WaveMarkerOverlayPtr _markerOverlay;
		SamplePlayerOverlayPtr _playerOverlay;

		StereoAudioBuffer _sampleData;
		NoteArray _notes;

		NoteParameters _defaultParameters;

		std::unordered_set<VirtualKey::Enum> _keysDown;

	public:
		Granular() : View({ 1024, 768 }), _waveformBuffer(48000 * 5) {
			setType<Granular>();
			setFocusPolicy(FocusPolicy::Click);
			setSizingPolicy(SizingPolicy::FitToParent);
		}
		~Granular() = default;

		void onInitialize() override {
			EventNode* eventNode = createState<EventNode>({ "Main" });

			_defaultParameters.amp = 1.0f;
			_defaultParameters.grainSize = 25.0f;
			_defaultParameters.overlap = 0.0f;
			_defaultParameters.pitch = 0.0f;
			_defaultParameters.stretch = 1.0f;

			for (Note& note : _notes) {
				note.parameters = _defaultParameters;
			}

			SampleLoaderUtil::loadSample("C:\\temp\\amen.wav", _sampleData);

			_notes[0].buffer = _sampleData.ref();

			_waveView = addChild<WaveView>("Waveform");
			_waveView->setSizingPolicy(SizingPolicy::FitToParent);
			//_waveView->setAudioData(_waveformBuffer.getSampleBuffer(), 2);
			_waveView->setAudioData(_sampleData.getSampleBuffer(), 2);

			_playerOverlay = _waveView->addChild(std::make_shared<SamplePlayerOverlay>(_notes));

			subscribe<SampleClickEvent>(_playerOverlay, [this, eventNode](const SampleClickEvent& ev) {
				if (ev.pressed && !_notes[ev.index].buffer.isEmpty()) {
					eventNode->broadcast(PlayNoteEvent{ .note = ev.index });
				} else {
					eventNode->broadcast<StopAllNotesEvent>();
				}
			});

			_markerOverlay = _waveView->addChild<WaveMarkerOverlay>("Marker Overlay");

			subscribe<MarkerAddedEvent>(_markerOverlay, [eventNode, this](const MarkerAddedEvent& ev) {
				Note& note = _notes[ev.idx];
				assert(note.contains(ev.marker));

				// Split this note
				uint64 size = ev.marker - note.offset;
				if (size == 0) {
					return;
				}

				uint64 newSize = note.getEnd() - ev.marker;
				if (newSize == 0) {
					return;
				}

				// Shift existing notes up
				for (size_t i = _notes.size() - 2; i > ev.idx; --i) {
					_notes[i + 1] = _notes[i];
				}

				note.buffer = _sampleData.slice(note.offset, (uint32)size);

				_notes[ev.idx + 1] = Note{
					.buffer = _sampleData.slice((uint32)ev.marker, (uint32)newSize),
					.offset = ev.marker,
					.parameters = _defaultParameters
				};

				eventNode->broadcast(SetNoteArrayEvent{ _notes });

				_playerOverlay->updateHighlights();
			});

			subscribe<MarkerRemovedEvent>(_markerOverlay, [eventNode, this](const MarkerRemovedEvent& ev) {
				Note& note = _notes[ev.idx];
				Note& next = _notes[ev.idx + 1];

				assert(!note.buffer.isEmpty());
				assert(next.offset > 0 && !next.buffer.isEmpty());

				uint64 newSize = next.getEnd() - note.offset;

				_notes[ev.idx].buffer = _sampleData.slice((uint32)note.offset, (uint32)newSize);

				// Shift existing notes down
				for (size_t i = ev.idx + 1; i < _notes.size() - 1; ++i) {
					_notes[i] = _notes[i + 1];
				}

				_notes.back() = Note();

				eventNode->broadcast(SetNoteArrayEvent{ _notes });

				_playerOverlay->updateHighlights();
			});

			subscribe<MarkerChangedEvent>(_markerOverlay, [eventNode, this](const MarkerChangedEvent& ev) {
				Note& note = _notes[ev.idx];
				Note& next = _notes[ev.idx + 1];

				note.buffer = _sampleData.slice(note.offset, ev.marker - note.offset);

				next.buffer = _sampleData.slice(ev.marker, next.getEnd() - ev.marker);
				next.offset = ev.marker;

				eventNode->broadcast(SetNoteArrayEvent{ _notes });

				_playerOverlay->updateHighlights();
			});

			auto audioManager = getState<std::shared_ptr<audio::AudioManager>>();
			audioManager->get()->setProcessor(std::make_shared<MyAudioProceessor>(eventNode->spawn("Audio"), _notes));

			auto slider = addChild<SliderView>("Pitch slider");
			slider->setArea({ 10, 10, 500, 30 });
			slider->setRange(-3.0f, 3.0f);
			slider->setValue(0.0f);
			slider->ValueChangeEvent = [eventNode](f32 value) { eventNode->broadcast(NoteParameterChangeEvent{ 0, ParameterType::Pitch, value }); };

			auto slider2 = addChild<SliderView>("Amp slider");
			slider2->setArea({ 10, 50, 500, 30 });
			slider2->setRange(0.0f, 0.2f);
			slider2->ValueChangeEvent = [eventNode](f32 value) { eventNode->broadcast(ParameterChangeEvent{ ParameterType::Amp, value }); };

			auto slider3 = addChild<SliderView>("Stretch slider");
			slider3->setArea({ 10, 100, 500, 30 });
			slider3->setRange(1.0f, 20.0f);
			slider3->ValueChangeEvent = [eventNode](f32 value) { eventNode->broadcast(NoteParameterChangeEvent{ 0, ParameterType::Stretch, value }); };

			auto slider4 = addChild<SliderView>("Overlap slider");
			slider4->setArea({ 10, 150, 500, 30 });
			slider4->setRange(0.0f, 0.5f);
			slider4->ValueChangeEvent = [eventNode](f32 value) { eventNode->broadcast(NoteParameterChangeEvent{ 0, ParameterType::Overlap, value }); };

			auto slider5 = addChild<SliderView>("Grain Size slider");
			slider5->setArea({ 10, 200, 500, 30 });
			slider5->setRange(2.0f, 50.0f);
			slider5->ValueChangeEvent = [eventNode](f32 value) { eventNode->broadcast(NoteParameterChangeEvent{ 0, ParameterType::GrainSize, value }); };

			TextureHandle knobTexture1 = getResourceManager().load<Texture>("C:\\code\\RetroPlugNext\\thirdparty\\Framework\\resources\\textures\\knob-M.png");
			TextureHandle knobTexture2 = getResourceManager().load<Texture>("C:\\code\\RetroPlugNext\\thirdparty\\Framework\\resources\\textures\\knob-M2.png");
			TextureHandle knobTexture3 = getResourceManager().load<Texture>("C:\\code\\RetroPlugNext\\thirdparty\\Framework\\resources\\textures\\knob-M3.png");
			TextureHandle upTexture = getResourceManager().load<Texture>("C:\\code\\RetroPlugNext\\thirdparty\\Framework\\resources\\textures\\up.png");

			auto knob = addChild<KnobView>("Amp");
			//auto knob = addChild<TextureView>("Amp");
			knob->setTexture(knobTexture1, 128);
			//knob->setTexture(knobTexture2, 16);
			//knob->setTexture(upTexture);
			knob->setArea({ 10, 250, knob->getTileSize().w, knob->getTileSize().h });
			//knob->setArea({ 10, 250, 200, 200 });


			eventNode->subscribe<PlayNoteEvent>([&](const PlayNoteEvent& ev) {
				_playerOverlay->setHighlight(ev.note, true);
			});

			eventNode->subscribe<StopNoteEvent>([&](const StopNoteEvent& ev) {
				_playerOverlay->setHighlight(ev.note, false);
			});

			eventNode->subscribe<StopAllNotesEvent>([&]() {
				_playerOverlay->removeHighlights();
			});

			/*auto plot = addChild<PlotView>("Plot");
			plot->setArea({ 10, 140, 400, 400 });
			plot->setFunc(Envelopes::hann);*/

			/*eventNode->subscribe<AudioRenderEvent>([&](const AudioRenderEvent& ev) {
				size_t copyAmount = std::min(ev.buffer.getFrameCount(), _waveformBuffer.getFrameCount());
				size_t moveAmount = _waveformBuffer.getFrameCount() - copyAmount;

				for (size_t i = 0; i < moveAmount; ++i) {
					_waveformBuffer[i] = _waveformBuffer[copyAmount + i];
				}

				for (size_t i = 0; i < copyAmount; ++i) {
					_waveformBuffer[moveAmount + i] = ev.buffer[i];
				}

				if (_playing) {
					_waveView->updateSlice();
				}
			});*/
		}

		void onUpdate(f32 delta) override {
			getState<EventNode>()->update();
		}

		bool onKey(VirtualKey::Enum key, bool down) override {
			if (key == VirtualKey::Space && down) {
				getState<EventNode>()->broadcast<TogglePauseEvent>();
			}

			if (key == VirtualKey::E && down) {
				_markerOverlay->setEditMode(!_markerOverlay->getEditMode());
			}

			int32 noteIdx = -1;

			switch (key) {
			case VirtualKey::Z: noteIdx = 0; break;
			case VirtualKey::S: noteIdx = 1; break;
			case VirtualKey::X: noteIdx = 2; break;
			case VirtualKey::D: noteIdx = 3; break;
			case VirtualKey::C: noteIdx = 4; break;
			case VirtualKey::V: noteIdx = 5; break;
			case VirtualKey::G: noteIdx = 6; break;
			case VirtualKey::B: noteIdx = 7; break;
			case VirtualKey::H: noteIdx = 8; break;
			case VirtualKey::N: noteIdx = 9; break;
			}

			if (noteIdx != -1) {
				if (down) {
					if (!_keysDown.contains(key)) {
						_keysDown.insert(key);
						getState<EventNode>()->broadcast(PlayNoteEvent{ .note = (NoteIndex)noteIdx });
					}
				} else {
					_keysDown.erase(key);
					getState<EventNode>()->broadcast(StopNoteEvent{ .note = (NoteIndex)noteIdx });
				}
			}

			return true;
		}

		bool onMouseScroll(PointF delta, Point position) override {
			return true;
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override {
			return true;
		}
	};
}
