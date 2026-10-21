// XML and XMLNode for ActionScript 1 and 2: a tree of nodes, an element's
// name and attributes and a text node's text, read through the properties
// the player has (firstChild, nextSibling, childNodes...). The text is parsed
// by the player ($phoenix.xml), leniently, a status saying what went wrong.
// Each node keeps what it is out of sight ($type, $value, $attributes,
// $children, $parent).

(function() {
    var player = _global.$phoenix;
    var natives = player.xml;
    var hide = function(object, names) {
        ASSetPropFlags(object, names, 1, 0);
    };
    var ELEMENT = 1;
    var TEXT = 3;

    var isNode = function(value) {
        return value instanceof Object && value.$type !== undefined;
    };
    // An object for attributes: no prototype, so only they are in it.
    var bareObject = function() {
        var made = new Object();
        made.__proto__ = undefined;
        return made;
    };
    var setUp = function(node, type, value) {
        node.$type = type;
        node.$value = value;
        node.$attributes = bareObject();
        node.$children = [];
        node.$parent = null;
        hide(node, ["$type", "$value", "$attributes", "$children", "$parent", "$childNodes"]);
    };
    // An element's attributes in their order: for-in's. (They are made
    // last first, so that it gives them as they were written.)
    var namesInOrder = function(object) {
        var names = [];
        for (var name in object) {
            names.push(name);
        }
        return names;
    };

    var XMLNode = function(type, value) {
        if (arguments.length >= 2) {
            setUp(this, (Number(type) >>> 0) & 255, String(value));
        } else {
            setUp(this, TEXT, "");
        }
    };
    _global.XMLNode = XMLNode;
    var n = XMLNode.prototype;

    // childNodes: one array, kept up to date as the children change.
    var refresh = function(node) {
        var cached = node.$childNodes;
        if (cached != undefined) {
            cached.length = 0;
            for (var i = 0; i < node.$children.length; i++) {
                cached.push(node.$children[i]);
            }
        }
    };
    var indexIn = function(list, item) {
        for (var i = 0; i < list.length; i++) {
            if (list[i] === item) {
                return i;
            }
        }
        return -1;
    };
    var detach = function(node) {
        var parent = node.$parent;
        if (parent != null) {
            var at = indexIn(parent.$children, node);
            if (at >= 0) {
                parent.$children.splice(at, 1);
            }
            node.$parent = null;
            refresh(parent);
        }
    };
    var escape = function(text) {
        text = String(text);
        text = text.split("&").join("&amp;");
        text = text.split("<").join("&lt;");
        text = text.split(">").join("&gt;");
        text = text.split("\"").join("&quot;");
        return text.split("'").join("&apos;");
    };
    var written = function(node) {
        if (node.$type != ELEMENT) {
            return escape(node.$value);
        }
        var children = node.$children;
        var inside = "";
        for (var i = 0; i < children.length; i++) {
            inside += written(children[i]);
        }
        if (node.$value == null) {
            return inside;
        }
        var out = "<" + node.$value;
        var names = namesInOrder(node.$attributes);
        for (var k = 0; k < names.length; k++) {
            out += " " + names[k] + "=\"" + escape(node.$attributes[names[k]]) + "\"";
        }
        if (children.length == 0) {
            return out + " />";
        }
        return out + ">" + inside + "</" + node.$value + ">";
    };

    n.cloneNode = function(deep) {
        if (!isNode(this)) {
            return undefined;
        }
        var made = new XMLNode(this.$type, this.$value);
        made.$value = this.$value;
        var names = namesInOrder(this.$attributes);
        for (var i = names.length - 1; i >= 0; i--) {
            made.$attributes[names[i]] = this.$attributes[names[i]];
        }
        if (deep) {
            for (var c = 0; c < this.$children.length; c++) {
                var child = this.$children[c].cloneNode(true);
                child.$parent = made;
                made.$children.push(child);
            }
        }
        return made;
    };
    n.removeNode = function() {
        if (isNode(this)) {
            detach(this);
        }
    };
    // Whether \a node is \a inner or holds it.
    var contains = function(node, inner) {
        for (var at = inner; at != null; at = at.$parent) {
            if (at === node) {
                return true;
            }
        }
        return false;
    };
    n.insertBefore = function(child, before) {
        if (!isNode(this) || !isNode(child) || !isNode(before) || indexIn(this.$children, child) >= 0 ||
            contains(child, this)) {
            return;
        }
        if (indexIn(this.$children, before) < 0) {
            return;
        }
        detach(child);
        this.$children.splice(indexIn(this.$children, before), 0, child);
        child.$parent = this;
        refresh(this);
    };
    n.appendChild = function(child) {
        if (!isNode(this) || !isNode(child) || indexIn(this.$children, child) >= 0 || contains(child, this)) {
            return;
        }
        detach(child);
        this.$children.push(child);
        child.$parent = this;
        refresh(this);
    };
    n.hasChildNodes = function() {
        return isNode(this) ? this.$children.length > 0 : undefined;
    };
    n.toString = function() {
        return isNode(this) ? written(this) : "";
    };
    // The URI a prefix's xmlns attribute gives, on this node or one around
    // it; any xmlns for the empty prefix.
    var namespaceOf = function(node, prefix) {
        for (var at = node; at != null; at = at.$parent) {
            var names = namesInOrder(at.$attributes);
            for (var i = 0; i < names.length; i++) {
                var name = names[i];
                if (name.substr(0, 5) == "xmlns" &&
                    (prefix == "" || name == "xmlns:" + prefix)) {
                    return at.$attributes[name];
                }
            }
        }
        return null;
    };
    n.getNamespaceForPrefix = function(prefix) {
        if (!isNode(this) || arguments.length == 0) {
            return undefined;
        }
        return namespaceOf(this, String(prefix));
    };
    n.getPrefixForNamespace = function(uri) {
        if (!isNode(this) || arguments.length == 0) {
            return undefined;
        }
        uri = String(uri);
        for (var at = this; at != null; at = at.$parent) {
            var names = namesInOrder(at.$attributes);
            for (var i = 0; i < names.length; i++) {
                var name = names[i];
                if (String(at.$attributes[name]) == uri && name.substr(0, 5) == "xmlns") {
                    return name.charAt(5) == ":" ? name.substr(6) : "";
                }
            }
        }
        return null;
    };

    // The node's parts as properties, each of a node only.
    var property = function(name, read, write) {
        n.addProperty(name, function() {
            return isNode(this) ? read.call(this) : undefined;
        }, write == undefined ? null : function(value) {
            if (isNode(this)) {
                write.call(this, value);
            }
        });
    };
    var nameOf = function(node) {
        return node.$type == ELEMENT ? node.$value : null;
    };
    var setValue = function(value) {
        if (value !== undefined) {
            this.$value = String(value);
        }
    };
    property("attributes", function() {
        return this.$attributes;
    });
    property("childNodes", function() {
        if (this.$childNodes == undefined) {
            this.$childNodes = [];
            refresh(this);
        }
        return this.$childNodes;
    });
    property("firstChild", function() {
        return this.$children.length > 0 ? this.$children[0] : null;
    });
    property("lastChild", function() {
        return this.$children.length > 0 ? this.$children[this.$children.length - 1] : null;
    });
    property("nextSibling", function() {
        var parent = this.$parent;
        if (parent == null) {
            return null;
        }
        var at = indexIn(parent.$children, this);
        return at >= 0 && at + 1 < parent.$children.length ? parent.$children[at + 1] : null;
    });
    property("nodeName", function() {
        return nameOf(this);
    }, setValue);
    property("nodeType", function() {
        return this.$type;
    });
    property("nodeValue", function() {
        return this.$type == ELEMENT ? null : this.$value;
    }, setValue);
    property("parentNode", function() {
        return this.$parent;
    });
    property("previousSibling", function() {
        var parent = this.$parent;
        if (parent == null) {
            return null;
        }
        var at = indexIn(parent.$children, this);
        return at > 0 ? parent.$children[at - 1] : null;
    });
    // The name's parts either side of its first colon (one at the end is
    // part of the name).
    property("prefix", function() {
        var name = nameOf(this);
        if (name == null) {
            return null;
        }
        var colon = name.indexOf(":");
        return colon >= 0 && colon + 1 < name.length ? name.substr(0, colon) : "";
    });
    property("localName", function() {
        var name = nameOf(this);
        if (name == null) {
            return null;
        }
        var colon = name.indexOf(":");
        return colon >= 0 && colon + 1 < name.length ? name.substr(colon + 1) : name;
    });
    property("namespaceURI", function() {
        var name = nameOf(this);
        if (name == null) {
            return null;
        }
        var colon = name.indexOf(":");
        var prefix = colon >= 0 && colon + 1 < name.length ? name.substr(0, colon) : "";
        var uri = namespaceOf(this, prefix);
        return uri == null ? "" : uri;
    });

    // ---- XML: a document, the root node of what it parses

    var build = function(document, descriptor, parent) {
        var node;
        if (descriptor[0] == ELEMENT) {
            node = new XMLNode(ELEMENT, descriptor[1]);
            var attributes = descriptor[2];
            for (var i = attributes.length - 2; i >= 0; i -= 2) {
                node.$attributes[attributes[i]] = attributes[i + 1];
            }
            for (var k = 0; k < attributes.length; k += 2) {
                if (attributes[k] == "id") {
                    document.$idMap[attributes[k + 1]] = node;
                }
            }
            var children = descriptor[3];
            for (var c = 0; c < children.length; c++) {
                build(document, children[c], node);
            }
        } else {
            node = new XMLNode(TEXT, descriptor[1]);
        }
        node.$parent = parent;
        parent.$children.push(node);
    };
    var parse = function(document, text) {
        var parsed = natives.parse(text, document.ignoreWhite);
        document.$status = parsed[0];
        if (parsed[1] !== undefined) {
            document.$xmlDecl = parsed[1];
        }
        if (parsed[2] !== undefined) {
            document.$docTypeDecl = parsed[2];
        }
        var nodes = parsed[3];
        for (var i = 0; i < nodes.length; i++) {
            build(document, nodes[i], document);
        }
        refresh(document);
    };

    var XML = function(text) {
        setUp(this, ELEMENT, null);
        this.$status = 0;
        this.$idMap = new Object();
        hide(this, ["$status", "$idMap", "$xmlDecl", "$docTypeDecl"]);
        if (arguments.length > 0) {
            parse(this, String(text));
        }
    };
    _global.XML = XML;
    var x = new Object();
    x.__proto__ = XMLNode.prototype;
    XML.prototype = x;
    x.constructor = XML;
    hide(x, ["constructor"]);

    var isDocument = function(value) {
        return isNode(value) && value.$idMap !== undefined;
    };
    x.createElement = function(name) {
        return new XMLNode(ELEMENT, name);
    };
    x.createTextNode = function(text) {
        return new XMLNode(TEXT, text);
    };
    x.parseXML = function(text) {
        if (!isDocument(this)) {
            return;
        }
        while (this.$children.length > 0) {
            detach(this.$children[this.$children.length - 1]);
        }
        if (arguments.length > 0) {
            parse(this, String(text));
        }
    };
    // Loading: what it says of how far it is, kept out of sight.
    var startLoading = function(target) {
        target._bytesLoaded = 0;
        target._bytesTotal = undefined;
        target.loaded = false;
        ASSetPropFlags(target, ["_bytesLoaded", "_bytesTotal", "loaded"], 3, 0);
    };
    x.load = function(url) {
        if (url === null || !isDocument(this)) {
            return false;
        }
        startLoading(this);
        natives.load(this, String(url));
        return true;
    };
    x.sendAndLoad = function(url, target) {
        if (url === null || !(target instanceof Object) || !isDocument(this)) {
            return;
        }
        startLoading(target);
        natives.load(target, String(url), this.toString());
    };
    x.onData = function(text) {
        if (text === undefined) {
            this.onLoad(false);
        } else {
            this.parseXML(String(text));
            this.loaded = true;
            this.onLoad(true);
        }
    };
    x.getBytesLoaded = function() {
        return this._bytesLoaded;
    };
    x.getBytesTotal = function() {
        return this._bytesTotal;
    };
    x.contentType = "application/x-www-form-urlencoded";
    x.addProperty("docTypeDecl", function() {
        return isDocument(this) ? this.$docTypeDecl : undefined;
    }, null);
    x.ignoreWhite = false;
    x.addProperty("status", function() {
        return isDocument(this) ? this.$status : undefined;
    }, null);
    x.addProperty("xmlDecl", function() {
        return isDocument(this) ? this.$xmlDecl : undefined;
    }, null);
    x.addProperty("idMap", function() {
        return isDocument(this) ? this.$idMap : undefined;
    }, null);
    ASSetPropFlags(x, "contentType", 4, 0);
})();
