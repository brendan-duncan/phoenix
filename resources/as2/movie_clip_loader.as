// Loading: MovieClip's loadMovie, unloadMovie, loadVariables and byte
// counts, and MovieClipLoader, whose listeners (itself first) hear
// onLoadStart, onLoadProgress, onLoadComplete, onLoadInit and onLoadError.
// The player does the loading ($phoenix.load).

(function() {
    var natives = _global.$phoenix.load;
    var p = MovieClip.prototype;
    p.loadMovie = natives.loadMovie;
    p.unloadMovie = natives.unloadMovie;
    p.loadVariables = natives.loadVariables;
    p.getBytesLoaded = natives.getBytesLoaded;
    p.getBytesTotal = natives.getBytesTotal;
    ASSetPropFlags(p, ["loadMovie", "unloadMovie", "loadVariables", "getBytesLoaded", "getBytesTotal"], 3);

    var MovieClipLoader = function() {
        this._listeners = [this];
        ASSetPropFlags(this, "_listeners", 1);
    };
    _global.MovieClipLoader = MovieClipLoader;
    var m = MovieClipLoader.prototype;
    AsBroadcaster.initialize(m);
    m.loadClip = natives.loadClip;
    m.unloadClip = natives.unloadClip;
    m.getProgress = natives.getProgress;
    ASSetPropFlags(m, ["loadClip", "unloadClip", "getProgress"], 3);
})();
