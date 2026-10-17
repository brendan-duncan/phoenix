package fl.text {
    import flash.display.DisplayObjectContainer;
    import flash.geom.Rectangle;
    import flash.text.TextField;
    import flash.text.TextFieldAutoSize;
    import flash.text.TextFormat;

    /**
     * What places a published movie's Text Layout Framework text: each text
     * object's markup, given to addInstance by the timeline it is on. The
     * framework itself comes from a runtime shared library, not the movie,
     * so here the text is shown as a classic text field instead, in its
     * box, with its first run's font, size and color and its paragraphs'
     * alignment. Used in place of the movie's own copy of this class.
     */
    public class RuntimeManager {
        private static var singleton:RuntimeManager;

        public static function getSingleton():RuntimeManager {
            if (!singleton) {
                singleton = new RuntimeManager();
            }
            return singleton;
        }

        public function addInstance(container:DisplayObjectContainer, name:String, bounds:Rectangle, xml:*,
                                    ...rest):void {
            if (!container || !name || container.getChildByName(name)) {
                return;
            }
            var markup:String = xml ? String(xml.toXMLString()) : "";
            var field:TextField = new TextField();
            field.name = name;
            field.selectable = false;
            field.multiline = true;
            field.wordWrap = attribute(markup, "type") != "Point";
            var padding:Number = Number(attribute(markup, "paddingLeft")) || 0;
            if (bounds) {
                field.x = bounds.x + padding;
                field.y = bounds.y + padding;
                field.width = Math.max(1, bounds.width - 2 * padding);
                field.height = Math.max(1, bounds.height - 2 * padding);
            }
            var span:int = markup.indexOf("<span");
            var tag:String = span >= 0 ? markup.substring(span, markup.indexOf(">", span)) : "";
            var format:TextFormat = new TextFormat();
            format.font = attribute(tag, "fontFamily") || "Arial";
            format.size = Number(attribute(tag, "fontSize")) || 12;
            var color:String = attribute(tag, "color");
            format.color = color ? uint("0x" + color.substr(1)) : 0;
            format.bold = attribute(tag, "fontWeight") == "bold";
            format.italic = attribute(tag, "fontStyle") == "italic";
            var paragraph:int = markup.indexOf("<p");
            var align:String = paragraph >= 0 ? attribute(markup.substring(paragraph, markup.indexOf(">", paragraph)),
                                                          "textAlign") : "";
            format.align = align == "center" ? "center" : (align == "right" || align == "end") ? "right" : "left";
            field.defaultTextFormat = format;
            var alpha:String = attribute(tag, "textAlpha");
            if (alpha) {
                field.alpha = Number(alpha);
            }
            field.text = textOf(markup);
            if (!field.wordWrap) {
                field.autoSize = TextFieldAutoSize.LEFT;
            }
            container.addChild(field);
        }

        public function addInstanceComplete(...rest):void {
        }

        public function addTargetInfo(...rest):void {
        }

        // The value of an attribute in a tag's text, or "".
        private static function attribute(tag:String, name:String):String {
            var at:int = tag.indexOf(" " + name + "=\"");
            if (at < 0) {
                return "";
            }
            at += name.length + 3;
            return unescape(tag.substring(at, tag.indexOf("\"", at)));
        }

        // The text of the markup's runs, a line for each paragraph.
        private static function textOf(markup:String):String {
            var flow:int = markup.indexOf("<TextFlow");
            var text:String = "";
            var at:int = flow >= 0 ? markup.indexOf(">", flow) + 1 : 0;
            var paragraphs:int = 0;
            while (at > 0 && at < markup.length) {
                var open:int = markup.indexOf("<", at);
                if (open < 0) {
                    break;
                }
                // (Not the line breaks and indents of printed markup.)
                var piece:String = markup.substring(at, open);
                if (piece.indexOf("\n") < 0 || piece.split("\n").join("").split(" ").join("").length > 0) {
                    text += unescape(piece);
                }
                var close:int = markup.indexOf(">", open);
                if (close < 0) {
                    break;
                }
                var tag:String = markup.substring(open + 1, close);
                if (tag.indexOf("p ") == 0 || tag == "p") {
                    if (paragraphs++ > 0) {
                        text += "\n";
                    }
                } else if (tag.indexOf("br") == 0) {
                    text += "\n";
                }
                at = close + 1;
            }
            return text;
        }

        private static function unescape(text:String):String {
            return text.split("&lt;").join("<").split("&gt;").join(">").split("&quot;").join("\"")
                .split("&apos;").join("'").split("&amp;").join("&");
        }
    }
}
