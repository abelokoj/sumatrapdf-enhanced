struct MarkdownMathItem {
    Str token;
    Str source;
    Str original;
    bool display;
};

static void FreeMarkdownMath(Vec<MarkdownMathItem>& items) {
    for (auto& item : items) {
        str::Free(item.token);
        str::Free(item.source);
        str::Free(item.original);
    }
}

static bool MarkdownMathEscaped(Str text, int pos) {
    int slashes = 0;
    while (pos > 0 && text.s[--pos] == '\\') slashes++;
    return (slashes & 1) != 0;
}

static Str ProtectMarkdownMath(Str markdown, Vec<MarkdownMathItem>& items) {
    str::Builder output;
    TempStr prefix = StrL("SUMATRAMATHPLACEHOLDER");
    while (str::Contains(markdown, prefix)) prefix = str::JoinTemp(prefix, StrL("X"));
    char fence = 0;
    int fenceLength = 0;
    for (int i = 0; i < len(markdown);) {
        bool lineStart = i == 0 || markdown.s[i - 1] == '\n';
        if (lineStart) {
            int end = i;
            while (end < len(markdown) && markdown.s[end] != '\n') end++;
            int start = i;
            while (start < end && start - i < 4 && markdown.s[start] == ' ') start++;
            int run = start;
            if (run < end && (markdown.s[run] == '`' || markdown.s[run] == '~'))
                while (run < end && markdown.s[run] == markdown.s[start]) run++;
            int count = run - start;
            bool isFence = start - i < 4 && count >= 3;
            if (fence || isFence || start - i == 4 || (i < end && markdown.s[i] == '\t')) {
                if (!fence && isFence) {
                    fence = markdown.s[start];
                    fenceLength = count;
                } else if (fence && isFence && markdown.s[start] == fence && count >= fenceLength) {
                    bool onlySpace = true;
                    for (int p = run; p < end; p++)
                        onlySpace &= markdown.s[p] == ' ' || markdown.s[p] == '\r' || markdown.s[p] == '\t';
                    if (onlySpace) fence = 0;
                }
                output.Append(Str(markdown.s + i, end - i));
                i = end;
                if (i < len(markdown)) output.AppendChar(markdown.s[i++]);
                continue;
            }
        }
        if (markdown.s[i] == '`' && !MarkdownMathEscaped(markdown, i)) {
            int run = i;
            while (run < len(markdown) && markdown.s[run] == '`') run++;
            int end = run;
            while (end < len(markdown)) {
                if (markdown.s[end] != '`') {
                    end++;
                    continue;
                }
                int after = end;
                while (after < len(markdown) && markdown.s[after] == '`') after++;
                if (after - end == run - i) {
                    end = after;
                    break;
                }
                end = after;
            }
            output.Append(Str(markdown.s + i, end - i));
            i = end;
            continue;
        }
        int delimiter = 0;
        bool display = false;
        char closing = 0;
        bool slash = markdown.s[i] == '\\' && i + 1 < len(markdown) && !MarkdownMathEscaped(markdown, i);
        if (slash && (markdown.s[i + 1] == '(' || markdown.s[i + 1] == '[')) {
            delimiter = 2;
            display = markdown.s[i + 1] == '[';
            closing = display ? ']' : ')';
        } else if (markdown.s[i] == '$' && !MarkdownMathEscaped(markdown, i)) {
            display = i + 1 < len(markdown) && markdown.s[i + 1] == '$';
            delimiter = display ? 2 : 1;
            closing = '$';
            if (!display && (i + 1 == len(markdown) || str::IsWs(markdown.s[i + 1]))) delimiter = 0;
        }
        int end = i + delimiter;
        if (delimiter) {
            for (; end < len(markdown); end++) {
                if (!display && (markdown.s[end] == '\n' || markdown.s[end] == '\r')) break;
                if (MarkdownMathEscaped(markdown, end)) continue;
                bool match = slash
                                 ? markdown.s[end] == '\\' && end + 1 < len(markdown) && markdown.s[end + 1] == closing
                                 : markdown.s[end] == '$' &&
                                       (!display || (end + 1 < len(markdown) && markdown.s[end + 1] == '$'));
                if (!match) continue;
                if (!display && !slash &&
                    (end == i + delimiter || str::IsWs(markdown.s[end - 1]) ||
                     (end + 1 < len(markdown) && markdown.s[end + 1] >= '0' && markdown.s[end + 1] <= '9')))
                    continue;
                break;
            }
            bool found = end < len(markdown) && (display || (markdown.s[end] != '\n' && markdown.s[end] != '\r'));
            if (found && end - i - delimiter > 0 && end - i - delimiter <= 16384 && len(items) < 10000) {
                Str token = str::Dup(fmt("%s%dEND", prefix, len(items)));
                VecAppend(items, {token, str::Dup(Str(markdown.s + i + delimiter, end - i - delimiter)),
                                  str::Dup(Str(markdown.s + i, end - i + delimiter)), display});
                output.Append(token);
                i = end + delimiter;
                continue;
            }
        }
        output.AppendChar(markdown.s[i++]);
    }
    return output.TakeStr();
}

static Str MarkdownMathMarkup(const MarkdownMathItem& item) {
    str::Builder html;
    html.Append(item.display ? StrL("<span class=\"enhanced-math enhanced-math-display\" data-display=\"1\">")
                             : StrL("<span class=\"enhanced-math\" data-display=\"0\">"));
    html.Append(item.display ? StrL("$$") : StrL("$"));
    for (int i = 0; i < len(item.source); i++) {
        char c = item.source.s[i];
        if (c == '&')
            html.Append(StrL("&amp;"));
        else if (c == '<')
            html.Append(StrL("&lt;"));
        else if (c == '>')
            html.Append(StrL("&gt;"));
        else
            html.AppendChar(c);
    }
    html.Append(item.display ? StrL("$$</span>") : StrL("$</span>"));
    return html.TakeStr();
}

// Tokens in link destinations and raw HTML are source text, never equations.
static void RestoreMathChunk(cmark_node* node, cmark_chunk& chunk, const Vec<MarkdownMathItem>& items) {
    Str value = str::Dup(Str((char*)chunk.data, (int)chunk.len));
    for (const auto& item : items) {
        if (!str::Contains(value, item.token)) continue;
        Str restored = str::Dup(str::ReplaceTemp(value, item.token, item.original));
        str::Free(value);
        value = restored;
    }
    cmark_chunk_set_cstr(cmark_node_mem(node), &chunk, CStrTemp(value));
    str::Free(value);
}

static void RestoreMarkdownMath(cmark_node* parent, const Vec<MarkdownMathItem>& items) {
    for (cmark_node* node = cmark_node_first_child(parent); node;) {
        cmark_node* next = cmark_node_next(node);
        cmark_node_type type = cmark_node_get_type(node);
        if (type == CMARK_NODE_LINK || type == CMARK_NODE_IMAGE) {
            RestoreMathChunk(node, node->as.link.url, items);
            RestoreMathChunk(node, node->as.link.title, items);
        } else if (type == CMARK_NODE_HTML_INLINE || type == CMARK_NODE_HTML_BLOCK) {
            RestoreMathChunk(node, node->as.literal, items);
        }
        if (type != CMARK_NODE_TEXT) {
            RestoreMarkdownMath(node, items);
            node = next;
            continue;
        }
        Str text((char*)node->as.literal.data, (int)node->as.literal.len);
        Str terminated = str::Dup(text);
        text = terminated;
        int pos = 0;
        bool replaced = false;
        while (pos < len(text)) {
            int closest = len(text);
            const MarkdownMathItem* found = nullptr;
            for (const auto& item : items) {
                const char* hit = strstr(text.s + pos, CStrTemp(item.token));
                if (hit && hit - text.s < closest) {
                    closest = (int)(hit - text.s);
                    found = &item;
                }
            }
            if (!found) break;
            if (closest > pos) {
                auto* before = cmark_node_new(CMARK_NODE_TEXT);
                cmark_chunk_set_cstr(cmark_node_mem(before), &before->as.literal,
                                     CStrTemp(Str(text.s + pos, closest - pos)));
                cmark_node_insert_before(node, before);
            }
            auto* math = cmark_node_new(CMARK_NODE_CUSTOM_INLINE);
            Str markup = MarkdownMathMarkup(*found);
            cmark_node_set_on_enter(math, CStrTemp(markup));
            str::Free(markup);
            cmark_node_insert_before(node, math);
            pos = closest + len(found->token);
            replaced = true;
        }
        if (replaced) {
            if (pos < len(text)) {
                auto* after = cmark_node_new(CMARK_NODE_TEXT);
                cmark_chunk_set_cstr(cmark_node_mem(after), &after->as.literal,
                                     CStrTemp(Str(text.s + pos, len(text) - pos)));
                cmark_node_insert_before(node, after);
            }
            cmark_node_unlink(node);
            cmark_node_free(node);
        }
        str::Free(terminated);
        node = next;
    }
}
