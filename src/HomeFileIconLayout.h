inline Rect HomeFileIconBounds(Rect text, Size source, int iconDy, int gap, bool rtl) {
    if (source.dx <= 0 || source.dy <= 0 || iconDy <= 0) return {};
    int longest = std::max(source.dx, source.dy);
    int dx = std::max(1, source.dx * iconDy / longest);
    int dy = std::max(1, source.dy * iconDy / longest);
    int x = rtl ? text.Right() + gap : text.x - gap - dx;
    return {x, text.y + (text.dy - dy) / 2, dx, dy};
}
