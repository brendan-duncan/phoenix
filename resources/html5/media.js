// flash.media: Sound, SoundChannel, SoundTransform and SoundMixer over Web
// Audio. Library sounds are decoded as the movie loads; one asked to play
// before it is ready starts once it is, as far in as it would have been.
// Where Web Audio cannot load a file (a page opened from the disk), an
// audio element plays it instead.

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;
var EventDispatcher = flash.events.EventDispatcher;
var Event = flash.events.Event;

var audioContext = null;
var master = null;
/// The page's audio, made the first time it is needed. Browsers start it
/// suspended until the person playing clicks or types (see the player).
function audio() {
    if (audioContext === null) {
        var Ctor = typeof AudioContext !== "undefined" ? AudioContext :
            typeof webkitAudioContext !== "undefined" ? webkitAudioContext : null;
        try {
            audioContext = Ctor ? new Ctor() : false;
        } catch (error) {
            audioContext = false;
        }
        if (audioContext) {
            master = audioContext.createGain();
            master.connect(audioContext.destination);
        }
    }
    return audioContext || null;
}
phoenix._.audio = audio;
phoenix._.resumeAudio = function () {
    var ctx = audio();
    if (ctx && ctx.state === "suspended" && ctx.resume)
        ctx.resume();
};

/// Library sounds: their files, and the buffers they decode to.
var library = [];
phoenix._.soundForClass = phoenix._.soundForClass || new Map();
/// Starts loading the published sounds (README.md, "Sounds").
phoenix._.loadSounds = function (sounds, base) {
    library = (sounds || []).map(function (desc) {
        var entry = {desc: desc, buffer: null, failed: false, waiting: [], url: (base || "") + desc.src};
        var ctx = audio();
        if (!ctx || typeof fetch === "undefined") {
            entry.failed = true;
            return entry;
        }
        fetch(entry.url).then(function (response) {
            if (!response.ok)
                throw new Error(response.status);
            return response.arrayBuffer();
        }).then(function (bytes) {
            return new Promise(function (resolve, reject) { ctx.decodeAudioData(bytes, resolve, reject); });
        }).then(function (buffer) {
            entry.buffer = buffer;
            entry.waiting.splice(0).forEach(function (start) { start(); });
        }).catch(function () {
            entry.failed = true;
            entry.waiting.splice(0).forEach(function (start) { start(); });
        });
        return entry;
    });
};
phoenix._.librarySounds = function () { return library; };

class SoundTransform {
    constructor(vol, panning) {
        this.volume = vol === undefined ? 1 : +vol;
        this.pan = panning === undefined ? 0 : +panning;
        this.leftToLeft = 1;
        this.leftToRight = 0;
        this.rightToLeft = 0;
        this.rightToRight = 1;
    }
}

var playing = new Set();

class SoundChannel extends EventDispatcher {
    constructor() {
        super();
        this._transform = new SoundTransform();
        this._started = 0;
        this._offset = 0;
        this._stopped = false;
        this._node = null;
        this._gain = null;
        this._pan = null;
        this._element = null;
        this._sound = null;
    }
    get position() {
        if (this._element)
            return this._element.currentTime * 1000;
        var ctx = audio();
        if (!ctx || !this._node)
            return this._offset;
        var duration = this._sound && this._sound._buffer() ? this._sound._buffer().duration * 1000 : 0;
        var at = this._offset + (ctx.currentTime - this._started) * 1000;
        return duration > 0 ? at % duration : at;
    }
    get leftPeak() { return this._stopped ? 0 : this._transform.volume * 0.5; }
    get rightPeak() { return this._stopped ? 0 : this._transform.volume * 0.5; }
    get soundTransform() { return new SoundTransform(this._transform.volume, this._transform.pan); }
    set soundTransform(value) {
        this._transform = new SoundTransform(value ? value.volume : 1, value ? value.pan : 0);
        this._applyTransform();
    }
    _applyTransform() {
        if (this._gain)
            this._gain.gain.value = Math.max(0, this._transform.volume);
        if (this._pan)
            this._pan.pan.value = Math.max(-1, Math.min(1, this._transform.pan));
        if (this._element)
            this._element.volume = Math.max(0, Math.min(1, this._transform.volume * SoundMixer._volume));
    }
    stop() {
        this._stopped = true;
        playing.delete(this);
        if (this._node) {
            try { this._node.stop(); } catch (error) {}
            this._node.onended = null;
        }
        if (this._element)
            this._element.pause();
    }
    _ended() {
        if (this._stopped)
            return;
        this._stopped = true;
        playing.delete(this);
        this.dispatchEvent(new Event(Event.SOUND_COMPLETE));
    }
}

class Sound extends EventDispatcher {
    constructor(stream, context) {
        super();
        this._index = -1;
        this._loadedBuffer = null;
        this._url = null;
        this._bytesLoaded = 0;
        for (var at = new.target; at && at !== Sound; at = Object.getPrototypeOf(at))
            if (phoenix._.soundForClass.has(at)) {
                this._index = phoenix._.soundForClass.get(at);
                break;
            }
        this.isBuffering = false;
        this.id3 = {};
        if (stream)
            this.load(stream, context);
    }
    $construct(stream, context) {
        super.$construct();
        if (stream)
            this.load(stream, context);
    }
    _entry() { return this._index >= 0 ? library[this._index] : this._loading || null; }
    _buffer() {
        var entry = this._entry();
        return entry ? entry.buffer : null;
    }
    get length() {
        var buffer = this._buffer();
        return buffer ? buffer.duration * 1000 : 0;
    }
    get bytesLoaded() { return this._buffer() ? 1 : 0; }
    get bytesTotal() { return 1; }
    get url() { return this._url; }
    load(stream, context) {
        var self = this;
        this._url = stream.url;
        var entry = {desc: {src: stream.url}, buffer: null, failed: false, waiting: [], url: stream.url};
        this._loading = entry;
        var ctx = audio();
        if (!ctx || typeof fetch === "undefined") {
            entry.failed = true;
            return;
        }
        fetch(stream.url).then(function (response) {
            if (!response.ok)
                throw new Error(response.status);
            return response.arrayBuffer();
        }).then(function (bytes) {
            return new Promise(function (resolve, reject) { ctx.decodeAudioData(bytes, resolve, reject); });
        }).then(function (buffer) {
            entry.buffer = buffer;
            entry.waiting.splice(0).forEach(function (start) { start(); });
            self.dispatchEvent(new Event(Event.COMPLETE));
        }).catch(function (error) {
            entry.failed = true;
            entry.waiting.splice(0).forEach(function (start) { start(); });
            self.dispatchEvent(new flash.events.IOErrorEvent("ioError", false, false, "Error #2032: Stream Error. URL: " + stream.url, 2032));
        });
    }
    close() {}
    extract(target, length, startPosition) { return 0; }
    /// Plays from \a startTime milliseconds in, \a loops times.
    play(startTime, loops, sndTransform) {
        var channel = new SoundChannel();
        channel._sound = this;
        if (sndTransform)
            channel._transform = new SoundTransform(sndTransform.volume, sndTransform.pan);
        var entry = this._entry();
        if (!entry)
            return channel;
        start(entry, channel, Math.max(0, +startTime || 0), Math.max(1, loops | 0));
        return channel;
    }
}

/// Plays \a entry on \a channel: through Web Audio once decoded, or an
/// audio element where it would not decode.
function start(entry, channel, offset, loops) {
    var asked = phoenix.now();
    var go = function () {
        if (channel._stopped)
            return;
        var late = phoenix.now() - asked;
        if (entry.buffer)
            startBuffer(entry.buffer, channel, offset + late, loops);
        else if (entry.failed && typeof Audio !== "undefined")
            startElement(entry.url, channel, offset + late, loops);
    };
    if (entry.buffer || entry.failed)
        go();
    else
        entry.waiting.push(go);
    playing.add(channel);
}
function startBuffer(buffer, channel, offset, loops) {
    var ctx = audio();
    if (!ctx)
        return;
    var duration = buffer.duration;
    var from = offset / 1000;
    var total = (duration - Math.min(from, duration)) + duration * (loops - 1);
    // A late start of a sound that has already ended plays nothing.
    if (from >= duration * loops)
        return;
    var node = ctx.createBufferSource();
    node.buffer = buffer;
    var gain = ctx.createGain();
    var tail = gain;
    if (ctx.createStereoPanner) {
        var pan = ctx.createStereoPanner();
        gain.connect(pan);
        tail = pan;
        channel._pan = pan;
    }
    tail.connect(master);
    node.connect(gain);
    channel._node = node;
    channel._gain = gain;
    channel._applyTransform();
    if (loops > 1) {
        node.loop = true;
        node.loopStart = 0;
        node.loopEnd = duration;
    }
    channel._started = ctx.currentTime;
    channel._offset = (from % duration) * 1000;
    node.onended = function () { channel._ended(); };
    node.start(0, from % duration);
    if (loops > 1 && isFinite(total))
        node.stop(ctx.currentTime + total);
}
function startElement(url, channel, offset, loops) {
    var element = new Audio(url);
    channel._element = element;
    channel._applyTransform();
    var left = loops;
    element.addEventListener("ended", function () {
        if (--left > 0 && !channel._stopped) {
            element.currentTime = 0;
            element.play().catch(function () {});
        } else
            channel._ended();
    });
    element.addEventListener("loadedmetadata", function () {
        element.currentTime = Math.min(offset / 1000, element.duration || 0);
    });
    element.play().catch(function () {});
}

/// Plays library sound \a index for a timeline: \a loops times, from
/// \a offset milliseconds.
phoenix._.playLibrarySound = function (index, loops, offset) {
    var entry = library[index];
    var channel = new SoundChannel();
    if (!entry)
        return channel;
    var sound = new Sound();
    sound._index = index;
    channel._sound = sound;
    start(entry, channel, offset || 0, Math.max(1, loops | 0));
    return channel;
};

var SoundMixer = {
    _volume: 1,
    bufferTime: 0,
    audioPlaybackMode: "media",
    useSpeakerphoneForVoice: false,
    stopAll: function () {
        Array.from(playing).forEach(function (channel) { channel.stop(); });
    },
    areSoundsInaccessible: function () { return false; },
    computeSpectrum: function () {}
};
Object.defineProperty(SoundMixer, "soundTransform", {
    get: function () { return new SoundTransform(SoundMixer._volume, 0); },
    set: function (value) {
        SoundMixer._volume = value ? Math.max(0, value.volume) : 1;
        if (master)
            master.gain.value = SoundMixer._volume;
        playing.forEach(function (channel) { channel._applyTransform(); });
    }
});

class SoundLoaderContext {
    constructor(bufferTime, checkPolicyFile) {
        this.bufferTime = bufferTime === undefined ? 1000 : bufferTime;
        this.checkPolicyFile = !!checkPolicyFile;
    }
}

phoenix.defineClass("flash.media.Sound", Sound);
phoenix.defineClass("flash.media.SoundChannel", SoundChannel);
phoenix.defineClass("flash.media.SoundTransform", SoundTransform);
phoenix.defineClass("flash.media.SoundLoaderContext", SoundLoaderContext);
phoenix.defineClass("flash.media.SoundMixer", function SoundMixerClass() {});
Object.getOwnPropertyNames(SoundMixer).forEach(function (key) {
    Object.defineProperty(flash.media.SoundMixer, key, Object.getOwnPropertyDescriptor(SoundMixer, key));
});
phoenix._.stopAllSounds = function () { SoundMixer.stopAll(); };
})(phoenix);
