inline bool ValidMarkdownLayout(float width, float left, float right) {
    return isfinite(width) && isfinite(left) && isfinite(right) && width > 0 && width <= 100 && left >= 0 &&
           right >= 0 && left < 100 && right < 100 && std::abs(width + left + right - 100) < 0.05f;
}

inline bool DeriveMarkdownLayout(float& width, float& left, float& right) {
    if (!isfinite(width) || !isfinite(left) || !isfinite(right)) return false;
    if (width <= 0 || width > 100) return false;
    if (left < 0 || right < 0 || left >= 100 || right >= 100) return false;

    if (std::abs(width + left + right - 100.f) < 0.05f) {
        return true;
    }
    if (left == 0 && right == 0) {
        left = (100.f - width) / 2.f;
        right = 100.f - width - left;
        return true;
    }
    if (left > 0 && right == 0 && left < 100.f - width) {
        right = 100.f - width - left;
        return true;
    }
    if (left == 0 && right > 0 && right < 100.f - width) {
        left = 100.f - width - right;
        return true;
    }
    return false;
}

inline TempStr MarkdownLayoutCssTemp(float width, float left, float right) {
    if (!ValidMarkdownLayout(width, left, right)) {
        if (!DeriveMarkdownLayout(width, left, right)) return {};
    }
    return fmt(
        "body { max-width:none !important; width:%.4f%% !important; margin-left:%.4f%% !important; "
        "margin-right:%.4f%% !important; padding-left:0 !important; padding-right:0 !important; "
        "box-sizing:border-box !important; }\n",
        width, left, right);
}
