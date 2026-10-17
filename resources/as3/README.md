# The built-in ActionScript library

ActionScript 3 classes Phoenix ships for movies to use: the `fl.*` user
interface components (Button, CheckBox, ComboBox, List and the rest), so that
FLA files built with the standard components run in Test Movie. They are
plain ActionScript written against the public `flash.*` API only, compiled
and run by libas3 like the movie's own classes.

## How the runtime finds them

The folder is built into Phoenix as a Qt resource (`as3` in CMakeLists.txt,
prefix `/`, base `resources`), so `fl/controls/Button.as` here is
`:/as3/fl/controls/Button.as` in the app. The class resolver looks for a
class in the movie's own classpath first and then here: a qualified name
`fl.controls.Button` is the file `:/as3/fl/controls/Button.as`. A movie that
has its own copy of a class keeps it.

## What is here

| Package | Classes |
|---|---|
| `fl.core` | `UIComponent` (sizing, styles, invalidation and drawing on render, focus), `InvalidationType`, `ComponentShim` (an empty clip, so the library's shim symbol resolves) |
| `fl.controls` | `BaseButton`, `LabelButton`, `Button`, `CheckBox`, `RadioButton`, `RadioButtonGroup`, `ButtonLabelPlacement`, `TextInput`, `TextArea`, `Label`, `ScrollBar`, `UIScrollBar`, `ScrollBarDirection`, `ScrollPolicy`, `Slider`, `SliderDirection`, `NumericStepper`, `SelectableList`, `List`, `ComboBox`, `ColorPicker` |
| `fl.controls.listClasses` | `CellRenderer`, `ICellRenderer`, `ListData` |
| `fl.containers` | `BaseScrollPane` |
| `fl.data` | `DataProvider`, `SimpleCollectionItem` |
| `fl.events` | `ComponentEvent`, `ListEvent`, `SliderEvent`, `SliderEventClickTarget`, `ColorPickerEvent`, `ScrollEvent`, `DataChangeEvent`, `DataChangeType`, `InteractionInputType` |
| `fl.managers` | `StyleManager`, `FocusManager`, `IFocusManager`, `IFocusManagerComponent`, `IFocusManagerGroup` |
| `fl.motion` | `AdjustColor`, `ColorMatrix`, `DynamicMatrix` (the color math of the Adjust Color filter) |
| `fl.skins` | `DefaultSkin`: the look drawn when a movie has no skin symbols (Phoenix's own, not a standard class) |

Not here yet: `DataGrid`, `TileList`, `ScrollPane`, `UILoader`, `ProgressBar`,
the `fl.accessibility` classes and live preview.

## How the components behave

The architecture is the standard one, so code written for the components
behaves the same:

- **Size.** `width`, `height` and `setSize` size a component without scaling
  it; `scaleX` and `scaleY` set the size as a multiple of the size it was
  placed at. `move(x, y)` and `x`, `y` place it on whole pixels (the
  unrounded position is what `x` and `y` read back). `RESIZE` and `MOVE`
  (ComponentEvent) follow.
- **Drawing.** Setting a property marks what it affects invalid and asks for
  `draw()` on the stage's next `Event.RENDER` (after `stage.invalidate()`) or
  the next `Event.ENTER_FRAME`, whichever comes first, so a run of changes
  draws once. `drawNow()` draws at once, `validateNow()` draws everything.
- **Events.** Values set in code dispatch nothing; the user's doing does:
  `Event.CHANGE` from a List, ComboBox, NumericStepper or toggle button, and
  `SliderEvent.CHANGE` (which is `"change"`), `THUMB_PRESS`, `THUMB_DRAG` and
  `THUMB_RELEASE` from a Slider (`CHANGE` while dragging only with
  `liveDragging`, else on release), `ColorPickerEvent.CHANGE` from a swatch.
  A CheckBox (any toggle button) flips `selected` on a click before other
  listeners of that click run. Buttons do not let the mouse reach their
  children (`mouseChildren = false`), so a click's target is the button.
- **Lists.** A click on a row dispatches `ListEvent.ITEM_CLICK` (cancelable)
  before selecting; selecting a different row then dispatches `Event.CHANGE`;
  the click bubbles on to the list after that. The selection follows the
  items as the DataProvider adds, removes and sorts them. Icons (`iconField`,
  `iconFunction`) may be classes, class names or display objects. A ComboBox
  without a `prompt` selects its first item as soon as it has one; its list
  opens on the stage, below it or above it when there is no room.
- **ColorPicker.** The 216 web colors in 18 columns unless `colors` is set;
  the `colors` getter is the palette's own array, so pushing colors onto it
  adds swatches (to every picker sharing the default palette).
- **Ranges.** A Slider (0 to 10) and NumericStepper (0 to 10, step 1, value 1)
  keep their value in range; a stepper's value is always the nearest
  multiple of `stepSize`; a slider with `snapInterval` steps by it.
- **Styles.** A component's style is its own (`setStyle`), else its class's
  (`StyleManager.setComponentStyle`), else the global one
  (`StyleManager.setStyle`, starting as UIComponent's defaults), else its
  class's default (its static `getStyleDefinition()`, or its nearest
  superclass's). The default text format is `_sans` 11 black, disabled gray
  (0x999999).

Default sizes, the same as the standard component symbols' avatars: Button,
CheckBox, RadioButton, ComboBox, TextInput and Label 100 by 22; List and
TextArea 100 by 100; NumericStepper 80 by 22; Slider 80 by 3 (the track; the
thumb hangs below it); ColorPicker 24 by 24; UIScrollBar and ScrollBar 15 by
100.

## Skins

Each part is drawn by a skin named in a style: `upSkin: "Button_upSkin"`,
`upIcon: "CheckBox_upIcon"`, `thumbUpSkin: "SliderThumb_upSkin"` and so on,
the names of the symbols in the movie library's Component Assets folder
(exported for ActionScript under those class names). A component makes a
skin with `getDefinitionByName(name)` and `new`, puts it in place and sets
its `width` and `height`: the symbols have scale-9 grids, so they stretch
the way they were drawn to. A style may also be a Class or a display object.

When the movie has no class of a skin's name (an FLA without the component
assets), `fl.skins.DefaultSkin` draws a plain one for that part and state,
at the symbol's natural size and again at each size it is given. Names that
are neither (an icon style naming nothing) give no skin.

## What the components need from `flash.*`

- **Library symbols as classes.** `getDefinitionByName("Button_upSkin")`
  finds a symbol exported for ActionScript by its linkage class name (no
  package) and `new` makes an instance showing its first frame, with its
  scale-9 grid applied as `width` and `height` change. Symbols without a
  class file of their own get a generated MovieClip subclass, as the player
  makes. A failed lookup throws a ReferenceError (it is caught, and
  remembered).
- **Display.** Sprite, Shape and Graphics (`beginFill`, `drawRect`,
  `drawCircle`, `moveTo`, `lineTo`, `endFill`, `clear`); `x`, `y`, `width`,
  `height`, `scaleX`, `scaleY` (negative too), `rotation`, `visible`, `alpha`,
  `name`, `scrollRect` (a List's rows), `transform.colorTransform` with
  `ColorTransform.color` (swatches); `mouseX`, `mouseY`, `localToGlobal`,
  `globalToLocal`; the child list (`addChildAt`, `removeChild`,
  `setChildIndex`, `getChildByName`, `contains`); `mouseEnabled`,
  `mouseChildren`, `buttonMode`, `useHandCursor`, `tabEnabled`, `tabChildren`,
  `tabIndex`. `focusRect`, `doubleClickEnabled` and `embedFonts` are set in
  `try` blocks and may be missing.
- **Stage.** `focus` (get and set, dispatching FocusEvents), `stageWidth`,
  `stageHeight`, `invalidate()` then `Event.RENDER`, and children added to it
  (a ComboBox's list, a ColorPicker's palette).
- **Events.** Bubbling, capture listeners (the focus manager listens on the
  stage), `preventDefault` on cancelable events, `stopPropagation`,
  dispatching script subclasses of Event (re-dispatched events cloned).
  MouseEvent `CLICK`, `DOUBLE_CLICK`, `MOUSE_DOWN`, `MOUSE_UP` and
  `MOUSE_MOVE` (on the stage, for drags), `ROLL_OVER`, `ROLL_OUT`,
  `MOUSE_OVER`, `MOUSE_OUT`, `MOUSE_WHEEL` (`delta`), with `shiftKey` and
  `ctrlKey`; KeyboardEvent `KEY_DOWN` and `KEY_UP`; FocusEvent `FOCUS_IN`,
  `FOCUS_OUT`, `KEY_FOCUS_CHANGE`; TextEvent `TEXT_INPUT`; Event
  `ENTER_FRAME` (also for objects off the display list), `RENDER`,
  `ADDED_TO_STAGE`, `REMOVED_FROM_STAGE`, `CHANGE` and `SCROLL` from text
  fields.
- **Text.** TextField `text`, `htmlText`, `textWidth`, `textHeight`, `width`,
  `height`, `type`, `selectable`, `defaultTextFormat`, `setTextFormat`,
  `maxChars`, `restrict`, `displayAsPassword`, `scrollV`, `maxScrollV`,
  `bottomScrollV`, `scrollH`, `maxScrollH`, `setSelection`,
  `selectionBeginIndex`, `selectionEndIndex`, `length`, `appendText`,
  `getLineMetrics`, `wordWrap`, `multiline`, `autoSize`; TextFormat's
  thirteen-argument constructor.
- **Other.** `flash.utils.Timer` (its `delay` changed while running, for
  repeating buttons) and TimerEvent, `Dictionary`, `getDefinitionByName`,
  `getQualifiedClassName`, `getQualifiedSuperclassName`; `flash.geom.Point`,
  `Rectangle`, `ColorTransform`; `flash.ui.Keyboard`.

## Components placed on a timeline

A component instance on the stage is a `DOMComponentInstance` in the FLA's
XML, naming a `DOMComponentItem` in the library (its `linkageClassName`, for
example `fl.controls.CheckBox`). From bunnyPaint.fla:

```xml
<DOMComponentInstance libraryItemName="CheckBox" name="lockPainting" ...>
  <matrix><Matrix a="1.75" tx="53.3" ty="142.25"/></matrix>
  <parametersAsXML><![CDATA[
    <property id="enabled">
      <Inspectable name="enabled" variable="enabled" defaultValue="true" type="Boolean" .../>
    </property>
    <property id="label">
      <Inspectable name="label" variable="label" defaultValue="Lock Painting From Quotes:" type="String" .../>
    </property>
    <property id="labelPlacement">
      <Inspectable name="labelPlacement" variable="labelPlacement" defaultValue="left"
                   enumeration="left,right,top,bottom" type="List" .../>
    </property>
    ...
  ]]></parametersAsXML>
</DOMComponentInstance>
```

The runtime places one in four steps, as the player does:

1. **Make it with its avatar.** The component symbol's first frame holds a
   placeholder rectangle, the avatar (`Component Assets/_private/Component_avatar`,
   an 80 by 22 shape the symbol scales to the component's default size: 1.25
   wide in CheckBox, so 100 by 22). Make the instance of the linkage class
   with that first frame's content as its children and the instance's matrix
   as its transform, and give it its name, *before* the ActionScript
   constructors run (`vm.newInstance`, attach the host state and children,
   then `vm.runConstructor`). The constructor reads its size from the
   avatar and its scale (lockPainting: 1.75 by 100 = 175 wide, 22 high),
   sets the scale back to 1, removes the avatar and puts itself on whole
   pixels. Only the first frame: the symbol's second frame holds its skin
   assets (even other components: a ComboBox's TextInput and List) and is
   never shown. The same goes for `new CheckBox()` in code when the library
   has the symbol linked to that class: the player attaches the symbol's
   first frame. Without the symbol, the component takes the default size.
2. **Set its parameters.** For each `property` of the instance's own
   `parametersAsXML`, in order: the property is the Inspectable's `variable`
   (else its `name`), the value its `defaultValue` (which holds this
   instance's value, not the class's default), by `type`:
   - `Boolean`: `"true"` is true, anything else false;
   - `Number`: `Number(value)`;
   - `String`, `List` (one of `enumeration`), `Font Name`: the text as is;
   - `Color`: `"#RRGGBB"` as a uint (ColorPicker `selectedColor`);
   - `Array`: the text split at commas (no brackets);
   - `Collection` (a List's or ComboBox's `dataProvider`):
     `"fl.data.DataProvider, fl.data.SimpleCollectionItem, item, 0, 0"` is an
     empty one, to skip; bunnyPaint has no other.

   Set them as ActionScript property sets (the components' setters do the
   work), each in its own `try` so one failing does not stop the rest,
   between `instance.componentInspectorSetting = true` and
   `instance.componentInspectorSetting = false`. Do this after every child
   of that frame is placed and named, and before the frame's scripts run:
   UIScrollBar's `scrollTargetName = "messageEdit"` looks the text field up
   among its siblings with `parent.getChildByName`, and bunnyPaint's frame
   script reads `lockPainting.selected` right after.
3. **Name it.** As any named instance: the timeline's property of that name
   (`lockPainting`) is the instance. An instance without a name still gets
   its parameters (bunnyPaint's UIScrollBar has none).
4. **Draw.** Nothing more: it draws at the next render or enter frame.

bunnyPaint.fla's two: `lockPainting` (CheckBox; `enabled` true, `label`
"Lock Painting From Quotes:", `labelPlacement` "left", `selected` false,
`visible` true) and an unnamed UIScrollBar (Matrix d=0.94, so 15 by 94;
`direction` "vertical", `scrollTargetName` "messageEdit", `visible` true).
The test `tests/as3_components/cases/StagePlacementTest.as` places both this
way against stand-ins.

## Testing

`tests/as3_components` builds a small driver against libas3 that compiles
every file here, loads every class through a class resolver, and runs the
test cases in `tests/as3_components/cases` against stand-in `flash.*` classes
written in ActionScript (`tests/as3_components/stubs`). It also loads
bunnyPaint's classes and runs its panels (BP_SliderControl, BP_ColorControl,
BP_ColorButton, BP_LayerOptionsPanel, BP_EffectsWindow, BP_AdjustColorFilter)
when its sources are at `D:/Backup/hulabunny/bunnyPaint`.

```bash
cmake -S tests/as3_components -B build-as3c -DPHOENIX_LIBAS3_SOURCE=D:/src/libas3
cmake --build build-as3c --config Release
build-as3c/Release/as3_components resources/as3 tests/as3_components [name filter]
```

The stand-ins measure text roughly and draw nothing, so the cases check the
components' logic and layout arithmetic, not their look.
