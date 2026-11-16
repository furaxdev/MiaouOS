/* Miaou Web: DOM tree, forgiving HTML parser, serializer. */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web.h"

Page *page_new(void) { Page *p = calloc(1, sizeof *p); p->doc = wn_new(p, 0, "#doc"); p->next_timer = 1; return p; }
void page_free(Page *p) {
    if (!p) return;
    js_page_free(p);
    for (int i = 0; i < p->nall; i++) { Node *n = p->all[i]; for (int k = 0; k < n->na; k++) { free(n->an[k]); free(n->av[k]); } free(n->text); free(n); }
    free(p->all);
    for (int i = 0; i < p->nrules; i++) { free(p->rules[i].decl); free(p->rules[i].sel); }
    free(p->rules);
    for (int i = 0; i < p->nvars; i++) { free(p->vk[i]); free(p->vv[i]); }
    for (int i = 0; i < p->ndi; i++) free(p->di[i].s);
    free(p->di); free(p);
}
Node *wn_new(Page *p, int type, const char *tag) {
    Node *n = calloc(1, sizeof *n); n->type = type;
    if (tag) { size_t i = 0; for (; tag[i] && i < sizeof n->tag - 1; i++) n->tag[i] = (char)tolower((unsigned char)tag[i]); n->tag[i] = 0; }
    if (p->nall >= p->capall) { p->capall = p->capall ? p->capall * 2 : 256; p->all = realloc(p->all, (size_t)p->capall * sizeof(Node *)); }
    p->all[p->nall++] = n; return n;
}
void wn_unlink(Node *c) {
    if (!c->parent) return;
    Node *p = c->parent;
    if (c->prev) c->prev->next = c->next; else p->first = c->next;
    if (c->next) c->next->prev = c->prev; else p->last = c->prev;
    c->parent = c->prev = c->next = NULL;
}
void wn_append(Node *parent, Node *c) {
    wn_unlink(c); c->parent = parent; c->prev = parent->last; c->next = NULL;
    if (parent->last) parent->last->next = c; else parent->first = c;
    parent->last = c;
}
void wn_insert_before(Node *parent, Node *c, Node *ref) {
    if (!ref || ref->parent != parent) { wn_append(parent, c); return; }
    wn_unlink(c); c->parent = parent; c->next = ref; c->prev = ref->prev;
    if (ref->prev) ref->prev->next = c; else parent->first = c;
    ref->prev = c;
}
const char *wn_attr(const Node *n, const char *k) { for (int i = 0; i < n->na; i++) if (!strcmp(n->an[i], k)) return n->av[i]; return NULL; }
void wn_set_attr(Node *n, const char *k, const char *v) {
    for (int i = 0; i < n->na; i++) if (!strcmp(n->an[i], k)) { free(n->av[i]); n->av[i] = strdup(v); return; }
    if (n->na >= 14) return;
    n->an[n->na] = strdup(k); n->av[n->na] = strdup(v); n->na++;
}
void wn_del_attr(Node *n, const char *k) {
    for (int i = 0; i < n->na; i++) if (!strcmp(n->an[i], k)) { free(n->an[i]); free(n->av[i]); for (int j = i; j + 1 < n->na; j++) { n->an[j] = n->an[j + 1]; n->av[j] = n->av[j + 1]; } n->na--; return; }
}

static const struct { const char *n; int cp; } ENT[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xa0 }, { "eacute", 0xe9 }, { "egrave", 0xe8 }, { "agrave", 0xe0 }, { "ecirc", 0xea },
    { "ccedil", 0xe7 }, { "ucirc", 0xfb }, { "ocirc", 0xf4 }, { "icirc", 0xee }, { "acirc", 0xe2 }, { "ugrave", 0xf9 }, { "euml", 0xeb }, { "iuml", 0xef }, { "mdash", 0x2014 }, { "ndash", 0x2013 },
    { "hellip", 0x2026 }, { "laquo", 0xab }, { "raquo", 0xbb }, { "copy", 0xa9 }, { "reg", 0xae }, { "trade", 0x2122 }, { "bull", 0x2022 }, { "middot", 0xb7 }, { "rarr", 0x2192 }, { "larr", 0x2190 },
    { "euro", 0x20ac }, { "deg", 0xb0 }, { "times", 0xd7 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201c }, { "rdquo", 0x201d }, { "check", 0x2713 }, { "hearts", 0x2665 },
};
char *wn_decode(const char *s, size_t n) {
    char *o = malloc(n * 4 + 1); size_t k = 0;
    for (size_t i = 0; i < n; ) {
        if (s[i] == '&') {
            size_t j = i + 1; while (j < n && j - i < 10 && s[j] != ';' && !isspace((unsigned char)s[j])) j++;
            if (j < n && s[j] == ';') {
                int cp = -1;
                if (s[i + 1] == '#') cp = (s[i + 2] == 'x' || s[i + 2] == 'X') ? (int)strtol(s + i + 3, NULL, 16) : atoi(s + i + 2);
                else for (size_t e = 0; e < sizeof ENT / sizeof ENT[0]; e++) if (strlen(ENT[e].n) == j - i - 1 && !strncmp(s + i + 1, ENT[e].n, j - i - 1)) cp = ENT[e].cp;
                if (cp > 0) { k += (size_t)utf8_put(o + k, cp); i = j + 1; continue; }
            }
        }
        o[k++] = s[i++];
    }
    o[k] = 0; return o;
}

static int is_void(const char *t) {
    static const char *V[] = { "br", "hr", "img", "input", "meta", "link", "area", "base", "col", "embed", "source", "track", "wbr", "param", NULL };
    for (int i = 0; V[i]; i++) if (!strcmp(t, V[i])) return 1; return 0;
}
static int is_blockish(const char *t) {
    static const char *B[] = { "p", "div", "ul", "ol", "dl", "h1", "h2", "h3", "h4", "h5", "h6", "table", "form", "pre", "blockquote", "hr", "section", "article", "nav", "header", "footer", "main", "aside", "fieldset", "figure", "address", NULL };
    for (int i = 0; B[i]; i++) if (!strcmp(t, B[i])) return 1; return 0;
}

void wn_parse(Page *pg, const char *h, Node *root) {
    Node *stack[256]; int sp = 0; stack[sp++] = root;
    const char *p = h;
    #define TOP (stack[sp - 1])
    while (*p) {
        if (*p == '<') {
            if (!strncmp(p, "<!--", 4)) { const char *c = strstr(p + 4, "-->"); p = c ? c + 3 : p + strlen(p); continue; }
            if (p[1] == '!' || p[1] == '?') { const char *e = strchr(p, '>'); p = e ? e + 1 : p + strlen(p); continue; }
            if (p[1] == '/') {
                const char *q = p + 2; char name[16]; size_t k = 0; while (isalnum((unsigned char)*q) && k < 15) name[k++] = (char)tolower((unsigned char)*q++); name[k] = 0;
                const char *e = strchr(p, '>'); p = e ? e + 1 : p + strlen(p);
                for (int i = sp - 1; i > 0; i--) if (!strcmp(stack[i]->tag, name)) { sp = i; break; }
                continue;
            }
            if (isalpha((unsigned char)p[1])) {
                const char *q = p + 1; char name[16]; size_t k = 0;
                while ((isalnum((unsigned char)*q) || *q == '-' || *q == ':') && k < 15) name[k++] = (char)tolower((unsigned char)*q++); name[k] = 0;
                /* implied end tags */
                if (!strcmp(name, "li")) { for (int i = sp - 1; i > 0; i--) { if (!strcmp(stack[i]->tag, "li")) { sp = i; break; } if (!strcmp(stack[i]->tag, "ul") || !strcmp(stack[i]->tag, "ol")) break; } }
                else if (!strcmp(name, "dt") || !strcmp(name, "dd")) { for (int i = sp - 1; i > 0; i--) { if (!strcmp(stack[i]->tag, "dt") || !strcmp(stack[i]->tag, "dd")) { sp = i; break; } if (!strcmp(stack[i]->tag, "dl")) break; } }
                else if (!strcmp(name, "tr")) { for (int i = sp - 1; i > 0; i--) { if (!strcmp(stack[i]->tag, "tr")) { sp = i; break; } if (!strcmp(stack[i]->tag, "table")) break; } }
                else if (!strcmp(name, "td") || !strcmp(name, "th")) { for (int i = sp - 1; i > 0; i--) { if (!strcmp(stack[i]->tag, "td") || !strcmp(stack[i]->tag, "th")) { sp = i; break; } if (!strcmp(stack[i]->tag, "tr") || !strcmp(stack[i]->tag, "table")) break; } }
                else if (!strcmp(name, "option")) { if (!strcmp(TOP->tag, "option")) sp--; }
                if (is_blockish(name) && !strcmp(TOP->tag, "p")) sp--;
                Node *n = wn_new(pg, 0, name);
                /* attributes */
                for (;;) {
                    while (*q && isspace((unsigned char)*q)) q++;
                    if (!*q || *q == '>' || (*q == '/' && q[1] == '>')) break;
                    if (*q == '/') { q++; continue; }
                    char an[40]; size_t al = 0; while (*q && !isspace((unsigned char)*q) && *q != '=' && *q != '>' && !(*q == '/' && q[1] == '>') && al < 39) an[al++] = (char)tolower((unsigned char)*q++); an[al] = 0;
                    while (*q && isspace((unsigned char)*q)) q++;
                    char *val = NULL;
                    if (*q == '=') {
                        q++; while (*q && isspace((unsigned char)*q)) q++;
                        if (*q == '"' || *q == '\'') { char qc = *q++; const char *s = q; while (*q && *q != qc) q++; val = wn_decode(s, (size_t)(q - s)); if (*q) q++; }
                        else { const char *s = q; while (*q && !isspace((unsigned char)*q) && *q != '>') q++; val = wn_decode(s, (size_t)(q - s)); }
                    } else val = strdup("");
                    if (al) wn_set_attr(n, an, val);
                    free(val);
                }
                int selfclose = (*q == '/'); const char *e = strchr(q, '>'); p = e ? e + 1 : p + strlen(p);
                if (sp < 255) { wn_append(TOP, n); }
                if (!strcmp(name, "script") || !strcmp(name, "style") || !strcmp(name, "textarea") || !strcmp(name, "title")) {
                    char endtag[20]; snprintf(endtag, sizeof endtag, "</%s", name);
                    const char *c = p; while (*c && strncasecmp(c, endtag, strlen(endtag))) c++;
                    if (c > p) { Node *t = wn_new(pg, 1, NULL); t->text = (name[0] == 's') ? strndup(p, (size_t)(c - p)) : wn_decode(p, (size_t)(c - p)); wn_append(n, t); }
                    const char *ce = *c ? strchr(c, '>') : NULL; p = ce ? ce + 1 : c;
                    continue;
                }
                if (!is_void(name) && !selfclose && sp < 255) stack[sp++] = n;
                continue;
            }
        }
        const char *s = p; while (*p && !(*p == '<' && (isalpha((unsigned char)p[1]) || p[1] == '/' || p[1] == '!' || p[1] == '?'))) p++;
        if (p == s) { p++; continue; }
        Node *t = wn_new(pg, 1, NULL); t->text = wn_decode(s, (size_t)(p - s)); wn_append(TOP, t);
    }
    #undef TOP
}

static void tc(const Node *n, char **buf, size_t *len, size_t *cap) {
    if (n->type == 1) { size_t l = strlen(n->text ? n->text : ""); if (*len + l + 1 > *cap) { *cap = (*len + l + 1) * 2; *buf = realloc(*buf, *cap); } memcpy(*buf + *len, n->text ? n->text : "", l); *len += l; (*buf)[*len] = 0; return; }
    if (!strcmp(n->tag, "script") || !strcmp(n->tag, "style")) { if (n->parent == NULL) return; }
    for (const Node *c = n->first; c; c = c->next) tc(c, buf, len, cap);
}
char *wn_text(const Node *n) { size_t len = 0, cap = 256; char *b = malloc(cap); b[0] = 0; tc(n, &b, &len, &cap); return b; }
void wn_set_text(Page *p, Node *n, const char *s) {
    while (n->first) wn_unlink(n->first);
    if (n->type == 1) { free(n->text); n->text = strdup(s); return; }
    Node *t = wn_new(p, 1, NULL); t->text = strdup(s); wn_append(n, t);
}
static void ser(const Node *n, char **buf, size_t *len, size_t *cap) {
    #define PUT(str) do { const char *_s = (str); size_t _l = strlen(_s); if (*len + _l + 1 > *cap) { *cap = (*len + _l + 1) * 2; *buf = realloc(*buf, *cap); } memcpy(*buf + *len, _s, _l); *len += _l; (*buf)[*len] = 0; } while (0)
    if (n->type == 1) { for (const char *c = n->text ? n->text : ""; *c; c++) { if (*c == '<') PUT("&lt;"); else if (*c == '>') PUT("&gt;"); else if (*c == '&') PUT("&amp;"); else { char t[2] = { *c, 0 }; PUT(t); } } return; }
    if (strcmp(n->tag, "#doc")) {
        PUT("<"); PUT(n->tag);
        for (int i = 0; i < n->na; i++) { PUT(" "); PUT(n->an[i]); PUT("=\""); PUT(n->av[i]); PUT("\""); }
        PUT(">");
    }
    if (is_void(n->tag)) return;
    for (const Node *c = n->first; c; c = c->next) ser(c, buf, len, cap);
    if (strcmp(n->tag, "#doc")) { PUT("</"); PUT(n->tag); PUT(">"); }
    #undef PUT
}
char *wn_inner_html(const Node *n) { size_t len = 0, cap = 256; char *b = malloc(cap); b[0] = 0; for (const Node *c = n->first; c; c = c->next) ser(c, &b, &len, &cap); return b; }
void wn_set_inner_html(Page *p, Node *n, const char *html) { while (n->first) wn_unlink(n->first); wn_parse(p, html, n); }
Node *wn_find_tag(Node *root, const char *tag) {
    if (root->type == 0 && !strcmp(root->tag, tag)) return root;
    for (Node *c = root->first; c; c = c->next) { Node *r = wn_find_tag(c, tag); if (r) return r; }
    return NULL;
}
