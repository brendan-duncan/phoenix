// flash.external.ExternalInterface for ActionScript 2. A movie Phoenix
// plays has no web page around it to call, so the interface is not
// available: call gives null and addCallback false. What the player writes
// in ActionScript to turn values into the XML it sends a page, and that XML
// back into values, is here too, as movies can call it.

(function() {
    // flash.* is there from SWF 8.
    if (_global.$phoenix.version < 8) {
        return;
    }
    if (_global.flash == undefined) {
        _global.flash = {};
    }
    if (_global.flash.external == undefined) {
        _global.flash.external = {};
    }
    var E = function() {
    };
    flash.external.ExternalInterface = E;

    E.addProperty("available", function() {
        return false;
    }, null);
    E.addCallback = function(name, instance, method) {
        return false;
    };
    E.call = function(name) {
        return null;
    };

    // Text with each of \a from replaced by \a to.
    var replaced = function(text, from, to) {
        return text.split(from).join(to);
    };
    // The argument as text (null with none), changed by \a change; null
    // for an empty result.
    var textOf = function(args, change) {
        if (args.length == 0) {
            return null;
        }
        var out = change(String(args[0]));
        return out == "" ? null : out;
    };
    E._escapeXML = function(text) {
        return textOf(arguments, function(s) {
            s = replaced(s, "&", "&amp;");
            s = replaced(s, "\"", "&quot;");
            s = replaced(s, "'", "&apos;");
            s = replaced(s, "<", "&lt;");
            return replaced(s, ">", "&gt;");
        });
    };
    E._unescapeXML = function(text) {
        return textOf(arguments, function(s) {
            s = replaced(s, "&gt;", ">");
            s = replaced(s, "&lt;", "<");
            s = replaced(s, "&apos;", "'");
            s = replaced(s, "&quot;", "\"");
            return replaced(s, "&amp;", "&");
        });
    };
    E._jsQuoteString = function(text) {
        return textOf(arguments, function(s) {
            s = replaced(s, "\"", "\\\"");
            s = replaced(s, "\n", "\\n");
            return replaced(s, "\r", "\\r");
        });
    };

    // ---- Values as XML

    E._arrayToXML = function(array) {
        var out = "<array>";
        for (var i = 0; i < array.length; i++) {
            out += "<property id=\"" + i + "\">" + flash.external.ExternalInterface._toXML(array[i]) + "</property>";
        }
        return out + "</array>";
    };
    // The function's name is the first; the arguments are the rest.
    E._argumentsToXML = function(args) {
        var out = "<arguments>";
        for (var i = 1; i < args.length; i++) {
            out += flash.external.ExternalInterface._toXML(args[i]);
        }
        return out + "</arguments>";
    };
    E._objectToXML = function(object) {
        var out = "<object>";
        for (var name in object) {
            out += "<property id=\"" + name + "\">" + flash.external.ExternalInterface._toXML(object[name]) +
                "</property>";
        }
        return out + "</object>";
    };
    E._toXML = function(value) {
        if (value === undefined) {
            return "<undefined/>";
        }
        if (value == null) {
            return "<null/>";
        }
        if (value === true) {
            return "<true/>";
        }
        if (value === false) {
            return "<false/>";
        }
        var type = typeof value;
        if (type === "number") {
            return "<number>" + value + "</number>";
        }
        if (type === "string") {
            return "<string>" + flash.external.ExternalInterface._escapeXML(value) + "</string>";
        }
        // Anything with a length of its own is an array (a String object's
        // characters each undefined).
        if (value.hasOwnProperty("length")) {
            return flash.external.ExternalInterface._arrayToXML(value);
        }
        if (type === "object") {
            return flash.external.ExternalInterface._objectToXML(value);
        }
        return "<null/>";
    };

    // ---- XML as values

    E._arrayToAS = function(node) {
        var out = [];
        var children = node.childNodes;
        for (var i = 0; i < children.length; i++) {
            var child = children[i];
            out[child.attributes.id] = flash.external.ExternalInterface._toAS(child.firstChild);
        }
        return out;
    };
    E._objectToAS = function(node) {
        var out = {};
        var children = node.childNodes;
        for (var i = 0; i < children.length; i++) {
            var child = children[i];
            out[child.attributes.id] = flash.external.ExternalInterface._toAS(child.firstChild);
        }
        return out;
    };
    E._argumentsToAS = function(node) {
        var out = [];
        var children = node.childNodes;
        for (var i = 0; i < children.length; i++) {
            out.push(flash.external.ExternalInterface._toAS(children[i]));
        }
        return out;
    };
    E._toAS = function(node) {
        var type = node.nodeName;
        if (type === "null") {
            return null;
        }
        if (type === "true") {
            return true;
        }
        if (type === "false") {
            return false;
        }
        if (type === "string") {
            return String(node.firstChild);
        }
        if (type === "number") {
            return Number(node.firstChild.toString());
        }
        if (type === "array") {
            return flash.external.ExternalInterface._arrayToAS(node);
        }
        if (type === "object") {
            return flash.external.ExternalInterface._objectToAS(node);
        }
        if (type === "arguments") {
            return flash.external.ExternalInterface._argumentsToAS(node);
        }
        return undefined;
    };

    // Hidden, kept, and read-only, as the player has them.
    ASSetPropFlags(E, null, 7);
})();
