package flash.xml
{
    /// A node of legacy XML (XMLDocument's): an element, with its name and
    /// attributes, or a piece of text. Its neighbors are fields, kept up to
    /// date as nodes are added and taken away.
    public class XMLNode
    {
        public var nodeType:uint;
        public var nodeName:String = null;
        public var nodeValue:String = null;
        public var parentNode:XMLNode = null;
        public var firstChild:XMLNode = null;
        public var lastChild:XMLNode = null;
        public var previousSibling:XMLNode = null;
        public var nextSibling:XMLNode = null;

        private var _attributes:Object = {};
        private var _children:Array = [];

        public function XMLNode(type:uint, value:String)
        {
            nodeType = type;
            if (type == XMLNodeType.ELEMENT_NODE)
                nodeName = value;
            else
                nodeValue = value;
        }

        public function get attributes():Object
        {
            return _attributes;
        }

        public function set attributes(value:Object):void
        {
            _attributes = value;
        }

        public function get childNodes():Array
        {
            return _children;
        }

        public function hasChildNodes():Boolean
        {
            return _children.length > 0;
        }

        public function cloneNode(deep:Boolean):XMLNode
        {
            var made:XMLNode = new XMLNode(nodeType, nodeType == XMLNodeType.ELEMENT_NODE ? nodeName : nodeValue);
            for (var name:String in _attributes)
                made._attributes[name] = _attributes[name];
            if (deep)
                for each (var child:XMLNode in _children)
                    made.appendChild(child.cloneNode(true));
            return made;
        }

        // The links between the children and to their parent, from the
        // list.
        private function relink():void
        {
            firstChild = _children.length > 0 ? _children[0] : null;
            lastChild = _children.length > 0 ? _children[_children.length - 1] : null;
            for (var i:int = 0; i < _children.length; i++)
            {
                var child:XMLNode = _children[i];
                child.parentNode = this;
                child.previousSibling = i > 0 ? _children[i - 1] : null;
                child.nextSibling = i + 1 < _children.length ? _children[i + 1] : null;
            }
        }

        public function removeNode():void
        {
            var parent:XMLNode = parentNode;
            if (parent)
            {
                var at:int = parent._children.indexOf(this);
                if (at >= 0)
                    parent._children.splice(at, 1);
                parent.relink();
            }
            parentNode = null;
            previousSibling = null;
            nextSibling = null;
        }

        /// Whether this is \a node or holds it.
        private function holds(node:XMLNode):Boolean
        {
            for (var at:XMLNode = node; at; at = at.parentNode)
                if (at == this)
                    return true;
            return false;
        }

        public function insertBefore(node:XMLNode, before:XMLNode):void
        {
            if (!node || node.holds(this))
                return;
            var at:int = before ? _children.indexOf(before) : -1;
            if (at < 0)
            {
                appendChild(node);
                return;
            }
            node.removeNode();
            at = _children.indexOf(before);
            _children.splice(at, 0, node);
            relink();
        }

        public function appendChild(node:XMLNode):void
        {
            if (!node || node.holds(this))
                return;
            node.removeNode();
            _children.push(node);
            relink();
        }

        /// The URI a prefix's xmlns attribute gives, on this node or one
        /// around it.
        public function getNamespaceForPrefix(prefix:String):String
        {
            for (var at:XMLNode = this; at; at = at.parentNode)
                for (var name:String in at._attributes)
                    if (name.indexOf("xmlns") == 0 && (prefix == "" ? (name == "xmlns") : name == "xmlns:" + prefix))
                        return at._attributes[name];
            return null;
        }

        public function getPrefixForNamespace(ns:String):String
        {
            for (var at:XMLNode = this; at; at = at.parentNode)
                for (var name:String in at._attributes)
                    if (name.indexOf("xmlns") == 0 && at._attributes[name] == ns)
                        return name.charAt(5) == ":" ? name.substr(6) : "";
            return null;
        }

        public function get localName():String
        {
            if (nodeName == null)
                return null;
            var colon:int = nodeName.indexOf(":");
            return colon >= 0 ? nodeName.substr(colon + 1) : nodeName;
        }

        public function get prefix():String
        {
            if (nodeName == null)
                return null;
            var colon:int = nodeName.indexOf(":");
            return colon >= 0 ? nodeName.substr(0, colon) : "";
        }

        public function get namespaceURI():String
        {
            if (nodeName == null)
                return null;
            if (prefix)
                return getNamespaceForPrefix(prefix);
            for (var at:XMLNode = this; at; at = at.parentNode)
                if (at._attributes.xmlns != undefined)
                    return at._attributes.xmlns;
            return null;
        }

        internal static function escaped(text:String):String
        {
            if (text == null)
                return "null";
            return text.split("&").join("&amp;").split("<").join("&lt;").split(">").join("&gt;")
                .split("\"").join("&quot;").split("'").join("&apos;");
        }

        public function toString():String
        {
            if (nodeType != XMLNodeType.ELEMENT_NODE)
                return escaped(nodeValue);
            var out:String = "";
            if (nodeName != null)
            {
                out += "<" + nodeName;
                for (var name:String in _attributes)
                    out += " " + name + "=\"" + escaped(String(_attributes[name])) + "\"";
            }
            if (_children.length > 0)
            {
                if (nodeName != null)
                    out += ">";
                for each (var child:XMLNode in _children)
                    out += child.toString();
                if (nodeName != null)
                    out += "</" + nodeName + ">";
            }
            else if (nodeName != null)
                out += " />";
            return out;
        }

        internal function clearChildren():void
        {
            for each (var child:XMLNode in _children)
            {
                child.parentNode = null;
                child.previousSibling = null;
                child.nextSibling = null;
            }
            _children = [];
            relink();
        }
    }
}
