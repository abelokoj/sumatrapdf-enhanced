# Shapes toolbar

Open **Shapes** beside the laser pointer in the main toolbar. The dropdown contains Line, Arrow, Dashed Line, Polyline, Rectangle, Ellipse, and Polygon on one row. It remembers your last shape.

Choose a border color and thickness, and optionally a fill color and fill opacity. Fill opacity uses 0–100%. Border thickness uses PDF points, from 0 to 64; 0 is a hairline. Type a value in the field beside either slider for precise settings. Each shape remembers its own border thickness, border color, fill color, and fill opacity.

The dropdown stays in place when you switch shapes. Close it or click the document to draw. Direct shape commands and keyboard shortcuts remain available.

Advanced settings store the last shape in `Annotations.ShapeTool` and widths in `LineBorderWidth`, `PolyLineBorderWidth`, `SquareBorderWidth`, `CircleBorderWidth`, and `PolygonBorderWidth`. Arrow and Dashed Line share Line's border, fill, and thickness defaults. Arrow uses a closed arrowhead; Dashed Line saves its dash pattern in the PDF so other readers show it too. Both use Line's two-point placement and Shift snapping, and support Undo/Redo.

`CmdCreateAnnotArrow` and `CmdCreateAnnotDashedLine` are available to custom commands, shortcuts, and pinned tools.

The lasso menu chooses Freehand or Rectangle selection. Selecting annotations opens a compact action strip beside them. Use Smooth for ink and polygonal paths, or Line pattern for Solid, Dashed, and Dotted strokes. Each group operation is one undo step. Cut keeps the originals until the whole selection is pasted successfully; subsequent pastes create copies. Copy and Cut are unavailable for annotations whose embedded data cannot be copied safely.

In **Advanced Settings**, `AnnotationModeIconStyle` offers Solid Pencil (the default) and Fine-tip Pen. Each choice has an icon preview. Optional live appearance preview updates the toolbar while browsing; Cancel restores the saved style and Save keeps the choice.

Use **+** to add an available color from the color menu. Use the palette icon to open the color picker and set a custom color.

Pin a shape to save its border, fill, opacity, and thickness together. Its toolbar icon shows the saved border and fill colors. Clicking the pinned shape restores that appearance.
