package flash.xml
{
    /// A legacy XML document (ActionScript 1 and 2's XML, for ActionScript
    /// 3): the root of the nodes it parses, its declarations and the
    /// elements by their id attributes.
    public class XMLDocument extends XMLNode
    {
        public var docTypeDecl:Object = null;
        public var idMap:Object = {};
        public var ignoreWhite:Boolean = false;
        public var xmlDecl:Object = null;

        public function XMLDocument(source:String = null)
        {
            super(XMLNodeType.ELEMENT_NODE, null);
            if (source != null)
                parseXML(source);
        }

        public function createElement(name:String):XMLNode
        {
            return new XMLNode(XMLNodeType.ELEMENT_NODE, name);
        }

        public function createTextNode(text:String):XMLNode
        {
            return new XMLNode(XMLNodeType.TEXT_NODE, text);
        }

        private function build(descriptor:Array, parent:XMLNode):void
        {
            var node:XMLNode = new XMLNode(descriptor[0], descriptor[1]);
            if (descriptor[0] == XMLNodeType.ELEMENT_NODE)
            {
                var attributes:Array = descriptor[2];
                for (var i:int = 0; i < attributes.length; i += 2)
                {
                    node.attributes[attributes[i]] = attributes[i + 1];
                    if (attributes[i] == "id")
                        idMap[attributes[i + 1]] = node;
                }
                for each (var child:Array in descriptor[3])
                    build(child, node);
            }
            parent.appendChild(node);
        }

        /// What \a source holds, in place of what it had.
        public function parseXML(source:String):void
        {
            clearChildren();
            var parsed:Array = __parse(source, ignoreWhite);
            if (parsed[1] !== undefined)
                xmlDecl = parsed[1];
            if (parsed[2] !== undefined)
                docTypeDecl = parsed[2];
            for each (var descriptor:Array in parsed[3])
                build(descriptor, this);
        }

        override public function toString():String
        {
            var out:String = "";
            if (xmlDecl != null)
                out += String(xmlDecl);
            if (docTypeDecl != null)
                out += String(docTypeDecl);
            return out + super.toString();
        }
    }
}
