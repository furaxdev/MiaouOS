/* Miaou Web: JavaScript (QuickJS, ES2023) bindings: document, elements, events, timers, console, modules, fetch/XHR (via assets/web/prelude.js). */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web.h"
#include "quickjs/quickjs.h"

#define PRELUDE_PATH "/usr/share/miaou/web/prelude.js"
static JSClassID node_cid;
#define PGP(c) ((Page *)JS_GetContextOpaque(c))
#define CTX(p) ((JSContext *)(p)->J)
#define CSTR(a, i) cs(c, (a)[i])

static char *cs(JSContext *c, JSValueConst v) { const char *s = JS_ToCString(c, v); if (!s) return strdup(""); char *r = strdup(s); JS_FreeCString(c, s); return r; }
static Node *nd(JSContext *c, JSValueConst v) { return (Node *)JS_GetOpaque2(c, v, node_cid); }

static JSValue wrap(JSContext *c, Node *n) {
    if (!n) return JS_NULL;
    if (n->jsref) return JS_DupValue(c, *(JSValue *)n->jsref);
    Page *p = PGP(c); JSValue proto = JS_GetClassProto(c, node_cid);
    if (n == p->doc) { JSValue dp = JS_GetPropertyStr(c, proto, "__docproto"); JS_FreeValue(c, proto); proto = dp; }
    JSValue o = JS_NewObjectProtoClass(c, proto, node_cid); JS_FreeValue(c, proto);
    JS_SetOpaque(o, n);
    JSValue *keep = malloc(sizeof(JSValue)); *keep = JS_DupValue(c, o); n->jsref = (char *)keep;
    return o;
}
static JSValue list(JSContext *c, Node **v, int n) { JSValue a = JS_NewArray(c); for (int i = 0; i < n; i++) JS_SetPropertyUint32(c, a, (uint32_t)i, wrap(c, v[i])); return a; }

/* ---- error reporting, deadlines, promise jobs ---- */
void js_log(Page *p, const char *msg) { if (p->ncons < 6) snprintf(p->console[p->ncons++], 160, "%s", msg); else { memmove(p->console[0], p->console[1], 160 * 5); snprintf(p->console[5], 160, "%s", msg); } }
static void report(JSContext *c) {
    Page *p = PGP(c); JSValue ex = JS_GetException(c); const char *s = JS_ToCString(c, ex);
    char b[200]; snprintf(b, sizeof b, "Erreur JS : %.170s", s ? s : "exception"); js_log(p, b); snprintf(p->err, sizeof p->err, "%s", b);
    if (s) JS_FreeCString(c, s); JS_FreeValue(c, ex);
}
static int interrupt_cb(JSRuntime *rt, void *opaque) { Page *p = opaque; (void)rt; return p->js_deadline && now_ms() > p->js_deadline; }
static void arm(Page *p, long ms) { p->js_deadline = now_ms() + ms; }
static void pump(Page *p) {
    JSContext *c = CTX(p), *c1; if (!c) return;
    arm(p, 2000);
    for (int guard = 0; guard < 2000; guard++) { int r = JS_ExecutePendingJob(JS_GetRuntime(c), &c1); if (r == 0) break; if (r < 0) report(c1); }
    p->js_deadline = 0;
}
static JSValue call(Page *p, JSValueConst fn, JSValueConst thisv, int argc, JSValueConst *argv) {
    JSContext *c = CTX(p); arm(p, 2000);
    JSValue r = JS_Call(c, fn, thisv, argc, argv); if (JS_IsException(r)) report(c);
    p->js_deadline = 0; return r;
}

/* ---- element properties ---- */
static const char *ATTR[] = { "id", "class", "href", "src", "name", "type", "title", "placeholder", "alt", "for", "action", "method", "rel", "target", "lang", "width", "height", "content", "charset" };
static const char *BOOLA[] = { "checked", "disabled", "hidden", "selected", "readonly", "required", "multiple", "autofocus" };
static JSValue attr_get(JSContext *c, JSValueConst t, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; const char *v = wn_attr(n, ATTR[m]); return JS_NewString(c, v ? v : ""); }
static JSValue attr_set(JSContext *c, JSValueConst t, JSValueConst val, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = cs(c, val); wn_set_attr(n, ATTR[m], s); free(s); PGP(c)->dirty = 1; return JS_UNDEFINED; }
static JSValue bool_get(JSContext *c, JSValueConst t, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; return JS_NewBool(c, wn_attr(n, BOOLA[m]) != NULL); }
static JSValue bool_set(JSContext *c, JSValueConst t, JSValueConst val, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; if (JS_ToBool(c, val)) wn_set_attr(n, BOOLA[m], ""); else wn_del_attr(n, BOOLA[m]); PGP(c)->dirty = 1; return JS_UNDEFINED; }
static JSValue p_tagName(JSContext *c, JSValueConst t, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char u[20]; size_t i = 0; for (; n->tag[i] && i < 19; i++) u[i] = (char)toupper((unsigned char)n->tag[i]); u[i] = 0; if (n->type) return JS_NewString(c, "#text"); return JS_NewString(c, u); }
static JSValue p_textContent(JSContext *c, JSValueConst t, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *x = wn_text(n); JSValue r = JS_NewString(c, x); free(x); return r; }
static JSValue s_textContent(JSContext *c, JSValueConst t, JSValueConst v, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = cs(c, v); wn_set_text(PGP(c), n, s); free(s); PGP(c)->dirty = 1; return JS_UNDEFINED; }
static JSValue p_innerHTML(JSContext *c, JSValueConst t, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *x = wn_inner_html(n); JSValue r = JS_NewString(c, x); free(x); return r; }
static JSValue s_innerHTML(JSContext *c, JSValueConst t, JSValueConst v, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = cs(c, v); wn_set_inner_html(PGP(c), n, s); free(s); PGP(c)->dirty = 1; return JS_UNDEFINED; }
static JSValue p_outerHTML(JSContext *c, JSValueConst t, int m) {
    Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *in = wn_inner_html(n); char *o = malloc(strlen(in) + 400); size_t k = (size_t)sprintf(o, "<%s", n->tag);
    for (int i = 0; i < n->na; i++) k += (size_t)sprintf(o + k, " %s=\"%s\"", n->an[i], n->av[i]); sprintf(o + k, ">%s</%s>", in, n->tag);
    JSValue r = JS_NewString(c, n->type ? (n->text ? n->text : "") : o); free(in); free(o); return r;
}
static JSValue p_value(JSContext *c, JSValueConst t, int m) {
    Node *n = nd(c, t); if (!n) return JS_EXCEPTION;
    if (!strcmp(n->tag, "textarea")) { char *x = wn_text(n); JSValue r = JS_NewString(c, x); free(x); return r; }
    if (!strcmp(n->tag, "select")) { Node *sel = NULL; for (Node *o = n->first; o; o = o->next) if (!o->type && !strcmp(o->tag, "option")) { if (!sel || wn_attr(o, "selected")) sel = o; if (wn_attr(o, "selected")) break; }
        if (!sel) return JS_NewString(c, ""); const char *v = wn_attr(sel, "value"); if (v) return JS_NewString(c, v); char *x = wn_text(sel); JSValue r = JS_NewString(c, x); free(x); return r; }
    const char *v = wn_attr(n, "value"); return JS_NewString(c, v ? v : "");
}
static JSValue s_value(JSContext *c, JSValueConst t, JSValueConst val, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = cs(c, val); if (!strcmp(n->tag, "textarea")) wn_set_text(PGP(c), n, s); else wn_set_attr(n, "value", s); free(s); return JS_UNDEFINED; }
static JSValue p_nav(JSContext *c, JSValueConst t, int m) {
    Node *n = nd(c, t), *r = NULL; if (!n) return JS_EXCEPTION;
    switch (m) {
    case 0: r = n->parent; break; case 1: r = n->first; break; case 2: r = n->last; break; case 3: r = n->next; break; case 4: r = n->prev; break;
    case 5: r = n->first; while (r && r->type) r = r->next; break; case 6: r = n->last; while (r && r->type) r = r->prev; break;
    case 7: r = n->next; while (r && r->type) r = r->next; break; case 8: r = n->prev; while (r && r->type) r = r->prev; break;
    }
    return wrap(c, r);
}
static JSValue p_children(JSContext *c, JSValueConst t, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; JSValue a = JS_NewArray(c); uint32_t i = 0; for (Node *k = n->first; k; k = k->next) if (m || !k->type) JS_SetPropertyUint32(c, a, i++, wrap(c, k)); return a; }
static JSValue p_zero(JSContext *c, JSValueConst t, int m) { return JS_NewInt32(c, 0); }
static JSValue p_nodetype(JSContext *c, JSValueConst t, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; return JS_NewInt32(c, n->type ? 3 : n == PGP(c)->doc ? 9 : 1); }
static JSValue p_nodevalue(JSContext *c, JSValueConst t, int m) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; return n->type ? JS_NewString(c, n->text ? n->text : "") : JS_NULL; }

/* ---- element methods ---- */
static JSValue m_getAttribute(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *k = CSTR(argv, 0); const char *v = wn_attr(n, k); free(k); return v ? JS_NewString(c, v) : JS_NULL; }
static JSValue m_setAttribute(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *k = CSTR(argv, 0), *v = CSTR(argv, 1); for (char *q = k; *q; q++) *q = (char)tolower((unsigned char)*q); wn_set_attr(n, k, v); free(k); free(v); PGP(c)->dirty = 1; return JS_UNDEFINED; }
static JSValue m_removeAttribute(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *k = CSTR(argv, 0); wn_del_attr(n, k); free(k); PGP(c)->dirty = 1; return JS_UNDEFINED; }
static JSValue m_hasAttribute(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *k = CSTR(argv, 0); int r = wn_attr(n, k) != NULL; free(k); return JS_NewBool(c, r); }
static void adopt(Node *parent, Node *ch, Node *before) {
    if (!strcmp(ch->tag, "#frag")) { while (ch->first) { Node *k = ch->first; if (before) wn_insert_before(parent, k, before); else wn_append(parent, k); } return; }
    if (before) wn_insert_before(parent, ch, before); else wn_append(parent, ch);
}
static JSValue m_appendChild(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *p = nd(c, t), *k = nd(c, argv[0]); if (!p || !k) return JS_EXCEPTION; for (Node *x = p; x; x = x->parent) if (x == k) return JS_ThrowTypeError(c, "HierarchyRequestError"); adopt(p, k, NULL); PGP(c)->dirty = 1; return JS_DupValue(c, argv[0]); }
static JSValue m_insertBefore(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *p = nd(c, t), *k = nd(c, argv[0]); if (!p || !k) return JS_EXCEPTION; Node *r = (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) ? nd(c, argv[1]) : NULL; adopt(p, k, r); PGP(c)->dirty = 1; return JS_DupValue(c, argv[0]); }
static JSValue m_removeChild(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *p = nd(c, t), *k = nd(c, argv[0]); if (!p || !k) return JS_EXCEPTION; if (k->parent == p) wn_unlink(k); PGP(c)->dirty = 1; return JS_DupValue(c, argv[0]); }
static JSValue m_replaceChild(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *p = nd(c, t), *nw = nd(c, argv[0]), *old = nd(c, argv[1]); if (!p || !nw || !old) return JS_EXCEPTION; adopt(p, nw, old); wn_unlink(old); PGP(c)->dirty = 1; return JS_DupValue(c, argv[1]); }
static JSValue m_remove(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; wn_unlink(n); PGP(c)->dirty = 1; return JS_UNDEFINED; }
static Node *clone(Page *p, Node *n, int deep) {
    Node *k = wn_new(p, n->type, n->type ? NULL : n->tag); if (n->type) k->text = strdup(n->text ? n->text : "");
    for (int i = 0; i < n->na; i++) wn_set_attr(k, n->an[i], n->av[i]);
    if (deep) for (Node *x = n->first; x; x = x->next) wn_append(k, clone(p, x, 1));
    return k;
}
static JSValue m_cloneNode(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; return wrap(c, clone(PGP(c), n, argc > 0 && JS_ToBool(c, argv[0]))); }
static JSValue m_contains(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *a = nd(c, t), *b = nd(c, argv[0]); if (!a || !b) return JS_EXCEPTION; for (Node *x = b; x; x = x->parent) if (x == a) return JS_TRUE; return JS_FALSE; }
static JSValue m_qs(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = CSTR(argv, 0); Node *r = css_query_first(n, s); free(s); return wrap(c, r); }
static JSValue m_qsa(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = CSTR(argv, 0); Node *buf[1024]; int k = css_query_all(n, s, buf, 1024); free(s); return list(c, buf, k); }
static JSValue m_closest(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = CSTR(argv, 0); Node *all[2048]; int cnt = css_query_all(PGP(c)->doc, s, all, 2048); free(s);
    for (Node *x = n; x; x = x->parent) for (int i = 0; i < cnt; i++) if (all[i] == x) return wrap(c, x); return JS_NULL; }
static JSValue m_matches(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = CSTR(argv, 0); Node *all[2048]; int cnt = css_query_all(PGP(c)->doc, s, all, 2048); free(s); for (int i = 0; i < cnt; i++) if (all[i] == n) return JS_TRUE; return JS_FALSE; }
static void walk_tag(Node *n, const char *tag, Node **out, int *k, int max) { for (Node *x = n->first; x && *k < max; x = x->next) if (!x->type) { if (!strcmp(tag, "*") || !strcasecmp(x->tag, tag)) out[(*k)++] = x; walk_tag(x, tag, out, k, max); } }
static JSValue m_byTag(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = CSTR(argv, 0); Node *buf[2048]; int k = 0; walk_tag(n, s, buf, &k, 2048); free(s); return list(c, buf, k); }
static JSValue m_byClass(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; char *s = CSTR(argv, 0), sel[200]; snprintf(sel, sizeof sel, ".%s", s); free(s); Node *buf[1024]; int k = css_query_all(n, sel, buf, 1024); return list(c, buf, k); }
static JSValue m_noop(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { return JS_UNDEFINED; }
static JSValue m_focus(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; PGP(c)->focus = n; return JS_UNDEFINED; }
static JSValue m_blur(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; if (PGP(c)->focus == n) PGP(c)->focus = NULL; return JS_UNDEFINED; }
static JSValue m_addEventListener(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    if (argc < 2 || !JS_IsFunction(c, argv[1])) return JS_UNDEFINED;
    JSValue ev = JS_GetPropertyStr(c, t, "__ev"); if (!JS_IsObject(ev)) { JS_FreeValue(c, ev); ev = JS_NewObject(c); JS_DefinePropertyValueStr(c, t, "__ev", JS_DupValue(c, ev), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE); }
    char *type = CSTR(argv, 0); JSValue arr = JS_GetPropertyStr(c, ev, type);
    if (!JS_IsObject(arr)) { JS_FreeValue(c, arr); arr = JS_NewArray(c); JS_SetPropertyStr(c, ev, type, JS_DupValue(c, arr)); }
    JSValue lenv = JS_GetPropertyStr(c, arr, "length"); int32_t len = 0; JS_ToInt32(c, &len, lenv); JS_FreeValue(c, lenv);
    JS_SetPropertyUint32(c, arr, (uint32_t)len, JS_DupValue(c, argv[1])); free(type); JS_FreeValue(c, arr); JS_FreeValue(c, ev); return JS_UNDEFINED;
}
/* events */
static JSValue ev_prevent(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { JS_SetPropertyStr(c, t, "defaultPrevented", JS_TRUE); return JS_UNDEFINED; }
static JSValue ev_stop(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { JS_SetPropertyStr(c, t, "__stop", JS_TRUE); return JS_UNDEFINED; }
static int dispatch_obj(Page *p, Node *target, JSValueConst ev) {
    JSContext *c = CTX(p); int prevented = 0;
    char *type = NULL; { JSValue tv = JS_GetPropertyStr(c, ev, "type"); type = cs(c, tv); JS_FreeValue(c, tv); }
    char onattr[60]; snprintf(onattr, sizeof onattr, "on%s", type);
    JS_SetPropertyStr(c, (JSValue)ev, "target", wrap(c, target));
    for (Node *n = target; n; n = n->parent) {
        JSValue w = wrap(c, n); JS_SetPropertyStr(c, (JSValue)ev, "currentTarget", JS_DupValue(c, w));
        const char *code = n->type == 0 ? wn_attr(n, onattr) : NULL;
        if (code && *code) {
            size_t l = strlen(code); char *src = malloc(l + 40); snprintf(src, l + 40, "(function(event){%s\n})", code);
            JSValue fn = JS_Eval(c, src, strlen(src), "<attr>", JS_EVAL_TYPE_GLOBAL); free(src);
            if (JS_IsException(fn)) report(c); else { JSValue a[1] = { ev }; JSValue r = call(p, fn, w, 1, a); if (JS_IsBool(r) && !JS_ToBool(c, r)) JS_SetPropertyStr(c, (JSValue)ev, "defaultPrevented", JS_TRUE); JS_FreeValue(c, r); }
            JS_FreeValue(c, fn);
        }
        JSValue h = JS_GetPropertyStr(c, w, onattr);
        if (JS_IsFunction(c, h)) { JSValue a[1] = { ev }; JSValue r = call(p, h, w, 1, a); if (JS_IsBool(r) && !JS_ToBool(c, r)) JS_SetPropertyStr(c, (JSValue)ev, "defaultPrevented", JS_TRUE); JS_FreeValue(c, r); }
        JS_FreeValue(c, h);
        JSValue evs = JS_GetPropertyStr(c, w, "__ev");
        if (JS_IsObject(evs)) {
            JSValue arr = JS_GetPropertyStr(c, evs, type);
            if (JS_IsObject(arr)) {
                JSValue lv = JS_GetPropertyStr(c, arr, "length"); int32_t len = 0; JS_ToInt32(c, &len, lv); JS_FreeValue(c, lv);
                for (int32_t i = 0; i < len; i++) { JSValue f = JS_GetPropertyUint32(c, arr, (uint32_t)i); if (JS_IsFunction(c, f)) { JSValue a[1] = { ev }; JSValue r = call(p, f, w, 1, a); JS_FreeValue(c, r); } JS_FreeValue(c, f); }
            }
            JS_FreeValue(c, arr);
        }
        JS_FreeValue(c, evs); JS_FreeValue(c, w);
        JSValue st = JS_GetPropertyStr(c, ev, "__stop"); int stop = JS_ToBool(c, st); JS_FreeValue(c, st); if (stop) break;
    }
    JSValue dp = JS_GetPropertyStr(c, ev, "defaultPrevented"); prevented = JS_ToBool(c, dp); JS_FreeValue(c, dp);
    free(type); pump(p); return prevented;
}
int js_dispatch(Page *p, Node *target, const char *type, const char *key) {
    JSContext *c = CTX(p); if (!c) return 0;
    JSValue ev = JS_NewObject(c);
    JS_SetPropertyStr(c, ev, "type", JS_NewString(c, type)); if (key) JS_SetPropertyStr(c, ev, "key", JS_NewString(c, key));
    JS_SetPropertyStr(c, ev, "bubbles", JS_TRUE); JS_SetPropertyStr(c, ev, "defaultPrevented", JS_FALSE);
    JS_SetPropertyStr(c, ev, "preventDefault", JS_NewCFunction(c, ev_prevent, "preventDefault", 0));
    JS_SetPropertyStr(c, ev, "stopPropagation", JS_NewCFunction(c, ev_stop, "stopPropagation", 0));
    JS_SetPropertyStr(c, ev, "stopImmediatePropagation", JS_NewCFunction(c, ev_stop, "stopImmediatePropagation", 0));
    int r = dispatch_obj(p, target, ev); JS_FreeValue(c, ev); return r;
}
static JSValue m_dispatchEvent(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n || argc < 1) return JS_EXCEPTION; return JS_NewBool(c, !dispatch_obj(PGP(c), n, argv[0])); }
static JSValue m_click(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, t); if (!n) return JS_EXCEPTION; js_dispatch(PGP(c), n, "click", NULL); return JS_UNDEFINED; }
int js_run_inline(Page *p, Node *n, const char *code) {
    JSContext *c = CTX(p); if (!c) return 0;
    size_t l = strlen(code); char *src = malloc(l + 40); snprintf(src, l + 40, "(function(event){%s\n})", code);
    JSValue fn = JS_Eval(c, src, strlen(src), "<inline>", JS_EVAL_TYPE_GLOBAL); free(src);
    if (JS_IsException(fn)) { report(c); return 0; }
    JSValue w = wrap(c, n), ev = JS_NewObject(c); JSValue a[1] = { ev }; JSValue r = call(p, fn, w, 1, a);
    JS_FreeValue(c, r); JS_FreeValue(c, ev); JS_FreeValue(c, w); JS_FreeValue(c, fn); pump(p); return 1;
}

/* ---- document ---- */
static JSValue d_getById(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { char *s = CSTR(argv, 0), sel[300]; snprintf(sel, sizeof sel, "[id=\"%s\"]", s); free(s); return wrap(c, css_query_first(PGP(c)->doc, sel)); }
static JSValue d_byName(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { char *s = CSTR(argv, 0), sel[300]; snprintf(sel, sizeof sel, "[name=\"%s\"]", s); free(s); Node *buf[256]; int k = css_query_all(PGP(c)->doc, sel, buf, 256); return list(c, buf, k); }
static JSValue d_create(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { char *s = CSTR(argv, 0); Node *n = wn_new(PGP(c), 0, s); free(s); return wrap(c, n); }
static JSValue d_text(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = wn_new(PGP(c), 1, NULL); n->text = cs(c, argv[0]); return wrap(c, n); }
static JSValue d_frag(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { return wrap(c, wn_new(PGP(c), 0, "#frag")); }
static JSValue d_title_get(JSContext *c, JSValueConst t, int m) { Node *tt = wn_find_tag(PGP(c)->doc, "title"); if (!tt) return JS_NewString(c, ""); char *x = wn_text(tt); JSValue r = JS_NewString(c, x); free(x); return r; }
static JSValue d_title_set(JSContext *c, JSValueConst t, JSValueConst v, int m) { Page *p = PGP(c); Node *tt = wn_find_tag(p->doc, "title"); if (!tt) { Node *h = wn_find_tag(p->doc, "head"); tt = wn_new(p, 0, "title"); wn_append(h ? h : p->doc, tt); } char *s = cs(c, v); wn_set_text(p, tt, s); snprintf(p->title, sizeof p->title, "%s", s); free(s); return JS_UNDEFINED; }
static JSValue d_tag(JSContext *c, JSValueConst t, int m) { static const char *T[] = { "body", "head", "html" }; return wrap(c, wn_find_tag(PGP(c)->doc, T[m])); }
static JSValue d_active(JSContext *c, JSValueConst t, int m) { Page *p = PGP(c); return wrap(c, p->focus ? p->focus : wn_find_tag(p->doc, "body")); }
static JSValue d_str(JSContext *c, JSValueConst t, int m) { return JS_NewString(c, m == 0 ? "" : m == 1 ? "complete" : PGP(c)->url); }
static JSValue d_cookie_set(JSContext *c, JSValueConst t, JSValueConst v, int m) { return JS_UNDEFINED; }
static JSValue d_write(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Page *p = PGP(c); Node *b = wn_find_tag(p->doc, "body"); char *s = CSTR(argv, 0); wn_parse(p, s, b ? b : p->doc); free(s); p->dirty = 1; return JS_UNDEFINED; }
static JSValue d_window(JSContext *c, JSValueConst t, int m) { return JS_GetGlobalObject(c); }

/* ---- globals ---- */
static JSValue g_timer(JSContext *c, JSValueConst t, int argc, JSValueConst *argv, int every) {
    Page *p = PGP(c); if (argc < 1 || !JS_IsFunction(c, argv[0]) || p->ntimers >= 32) return JS_NewInt32(c, 0);
    int32_t ms = 0; if (argc > 1) JS_ToInt32(c, &ms, argv[1]); if (ms < 0) ms = 0; if (every && ms < 10) ms = 10;
    Timer *tm = &p->timers[p->ntimers++]; tm->id = p->next_timer++; tm->due = now_ms() + ms; tm->every = every ? ms : -1;
    JSValue *f = malloc(sizeof(JSValue)); *f = JS_DupValue(c, argv[0]); tm->fn = f;
    return JS_NewInt32(c, tm->id);
}
static JSValue g_clear(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    Page *p = PGP(c); int32_t id = 0; if (argc > 0) JS_ToInt32(c, &id, argv[0]);
    for (int i = 0; i < p->ntimers; i++) if (p->timers[i].id == id) { JS_FreeValue(c, *(JSValue *)p->timers[i].fn); free(p->timers[i].fn); p->timers[i] = p->timers[--p->ntimers]; break; }
    return JS_UNDEFINED;
}
static JSValue g_log(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    char buf[300] = ""; for (int i = 0; i < argc; i++) { char *s = CSTR(argv, i); if (strlen(buf) + strlen(s) + 2 < sizeof buf) { strcat(buf, s); strcat(buf, " "); } free(s); }
    js_log(PGP(c), buf); return JS_UNDEFINED;
}
static JSValue g_alert(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { char *s = CSTR(argv, 0); toast("%.100s", s); free(s); return JS_UNDEFINED; }
static JSValue g_confirm(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { char *s = CSTR(argv, 0); toast("%.100s", s); free(s); return JS_TRUE; }
static JSValue g_prompt(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { return JS_NULL; }
static JSValue g_fetch(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    Page *p = PGP(c); if (!p->fetch) return JS_NULL; char *u = CSTR(argv, 0); size_t len = 0; char *b = p->fetch(u, &len, p->fctx); free(u);
    if (!b) return JS_NULL; JSValue r = JS_NewStringLen(c, b, len); free(b); return r;
}
static JSValue g_nav(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Page *p = PGP(c); char *u = CSTR(argv, 0); snprintf(p->nav_url, sizeof p->nav_url, "%s", u); free(u); p->pending_nav = 1; return JS_UNDEFINED; }
static JSValue g_navcode(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { int32_t k = 0; JS_ToInt32(c, &k, argv[0]); PGP(c)->pending_nav = k; return JS_UNDEFINED; }
static JSValue g_href(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { return JS_NewString(c, PGP(c)->url); }
static JSValue g_sget(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, argv[0]); if (!n) return JS_EXCEPTION; char *k = CSTR(argv, 1); char *v = css_style_get(n, k); free(k); JSValue r = JS_NewString(c, v ? v : ""); free(v); return r; }
static JSValue g_sset(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { Node *n = nd(c, argv[0]); if (!n) return JS_EXCEPTION; char *k = CSTR(argv, 1), *v = CSTR(argv, 2); css_style_prop(n, k, v); free(k); free(v); PGP(c)->dirty = 1; return JS_UNDEFINED; }
static JSValue g_ctor(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { return JS_UNDEFINED; }

/* ---- ES modules: relative specifiers resolved against the page / importing module, fetched with the page loader ---- */
static char *mod_normalize(JSContext *c, const char *base, const char *name, void *opaque) {
    char out[800];
    if (!strncmp(name, "http://", 7) || !strncmp(name, "https://", 8) || !strncmp(name, "file://", 7)) snprintf(out, sizeof out, "%s", name);
    else {
        const char *origin_end = strstr(base, "://"); origin_end = origin_end ? strchr(origin_end + 3, '/') : NULL; size_t ol = origin_end ? (size_t)(origin_end - base) : strlen(base);
        char dir[800]; snprintf(dir, sizeof dir, "%s", base); char *sl = strrchr(dir, '/'); if (sl && sl > dir + 8) sl[1] = 0;
        if (name[0] == '/') snprintf(out, sizeof out, "%.*s%s", (int)ol, base, name); else snprintf(out, sizeof out, "%s%s", dir, name);
    }
    char *r = js_malloc(c, strlen(out) + 1); strcpy(r, out); return r;
}
static JSModuleDef *mod_load(JSContext *c, const char *name, void *opaque) {
    Page *p = PGP(c); size_t len = 0; char *src = p->fetch ? p->fetch(name, &len, p->fctx) : NULL;
    if (!src) { JS_ThrowReferenceError(c, "could not load module '%s'", name); return NULL; }
    JSValue v = JS_Eval(c, src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY); free(src);
    if (JS_IsException(v)) return NULL;
    JSModuleDef *m = JS_VALUE_GET_PTR(v); JS_FreeValue(c, v); return m;
}

static const JSCFunctionListEntry EL_FUNCS[] = {
    JS_CGETSET_MAGIC_DEF("id", attr_get, attr_set, 0), JS_CGETSET_MAGIC_DEF("className", attr_get, attr_set, 1), JS_CGETSET_MAGIC_DEF("href", attr_get, attr_set, 2),
    JS_CGETSET_MAGIC_DEF("src", attr_get, attr_set, 3), JS_CGETSET_MAGIC_DEF("name", attr_get, attr_set, 4), JS_CGETSET_MAGIC_DEF("type", attr_get, attr_set, 5),
    JS_CGETSET_MAGIC_DEF("title", attr_get, attr_set, 6), JS_CGETSET_MAGIC_DEF("placeholder", attr_get, attr_set, 7), JS_CGETSET_MAGIC_DEF("alt", attr_get, attr_set, 8),
    JS_CGETSET_MAGIC_DEF("htmlFor", attr_get, attr_set, 9), JS_CGETSET_MAGIC_DEF("action", attr_get, attr_set, 10), JS_CGETSET_MAGIC_DEF("method", attr_get, attr_set, 11),
    JS_CGETSET_MAGIC_DEF("rel", attr_get, attr_set, 12), JS_CGETSET_MAGIC_DEF("target", attr_get, attr_set, 13), JS_CGETSET_MAGIC_DEF("lang", attr_get, attr_set, 14),
    JS_CGETSET_MAGIC_DEF("width", attr_get, attr_set, 15), JS_CGETSET_MAGIC_DEF("height", attr_get, attr_set, 16), JS_CGETSET_MAGIC_DEF("content", attr_get, attr_set, 17), JS_CGETSET_MAGIC_DEF("charset", attr_get, attr_set, 18),
    JS_CGETSET_MAGIC_DEF("checked", bool_get, bool_set, 0), JS_CGETSET_MAGIC_DEF("disabled", bool_get, bool_set, 1), JS_CGETSET_MAGIC_DEF("hidden", bool_get, bool_set, 2),
    JS_CGETSET_MAGIC_DEF("selected", bool_get, bool_set, 3), JS_CGETSET_MAGIC_DEF("readOnly", bool_get, bool_set, 4), JS_CGETSET_MAGIC_DEF("required", bool_get, bool_set, 5),
    JS_CGETSET_MAGIC_DEF("multiple", bool_get, bool_set, 6), JS_CGETSET_MAGIC_DEF("autofocus", bool_get, bool_set, 7),
    JS_CGETSET_MAGIC_DEF("tagName", p_tagName, NULL, 0), JS_CGETSET_MAGIC_DEF("nodeName", p_tagName, NULL, 0),
    JS_CGETSET_MAGIC_DEF("textContent", p_textContent, s_textContent, 0), JS_CGETSET_MAGIC_DEF("innerText", p_textContent, s_textContent, 0),
    JS_CGETSET_MAGIC_DEF("innerHTML", p_innerHTML, s_innerHTML, 0), JS_CGETSET_MAGIC_DEF("outerHTML", p_outerHTML, NULL, 0), JS_CGETSET_MAGIC_DEF("value", p_value, s_value, 0),
    JS_CGETSET_MAGIC_DEF("parentNode", p_nav, NULL, 0), JS_CGETSET_MAGIC_DEF("parentElement", p_nav, NULL, 0), JS_CGETSET_MAGIC_DEF("firstChild", p_nav, NULL, 1), JS_CGETSET_MAGIC_DEF("lastChild", p_nav, NULL, 2),
    JS_CGETSET_MAGIC_DEF("nextSibling", p_nav, NULL, 3), JS_CGETSET_MAGIC_DEF("previousSibling", p_nav, NULL, 4), JS_CGETSET_MAGIC_DEF("firstElementChild", p_nav, NULL, 5),
    JS_CGETSET_MAGIC_DEF("lastElementChild", p_nav, NULL, 6), JS_CGETSET_MAGIC_DEF("nextElementSibling", p_nav, NULL, 7), JS_CGETSET_MAGIC_DEF("previousElementSibling", p_nav, NULL, 8),
    JS_CGETSET_MAGIC_DEF("children", p_children, NULL, 0), JS_CGETSET_MAGIC_DEF("childNodes", p_children, NULL, 1),
    JS_CGETSET_MAGIC_DEF("offsetWidth", p_zero, NULL, 0), JS_CGETSET_MAGIC_DEF("offsetHeight", p_zero, NULL, 0), JS_CGETSET_MAGIC_DEF("clientWidth", p_zero, NULL, 0),
    JS_CGETSET_MAGIC_DEF("clientHeight", p_zero, NULL, 0), JS_CGETSET_MAGIC_DEF("offsetTop", p_zero, NULL, 0), JS_CGETSET_MAGIC_DEF("offsetLeft", p_zero, NULL, 0),
    JS_CGETSET_MAGIC_DEF("scrollHeight", p_zero, NULL, 0), JS_CGETSET_MAGIC_DEF("scrollWidth", p_zero, NULL, 0),
    JS_CGETSET_MAGIC_DEF("nodeType", p_nodetype, NULL, 0), JS_CGETSET_MAGIC_DEF("nodeValue", p_nodevalue, NULL, 0),
    JS_CFUNC_DEF("getAttribute", 1, m_getAttribute), JS_CFUNC_DEF("setAttribute", 2, m_setAttribute), JS_CFUNC_DEF("removeAttribute", 1, m_removeAttribute), JS_CFUNC_DEF("hasAttribute", 1, m_hasAttribute),
    JS_CFUNC_DEF("appendChild", 1, m_appendChild), JS_CFUNC_DEF("insertBefore", 2, m_insertBefore), JS_CFUNC_DEF("removeChild", 1, m_removeChild), JS_CFUNC_DEF("replaceChild", 2, m_replaceChild), JS_CFUNC_DEF("remove", 0, m_remove),
    JS_CFUNC_DEF("cloneNode", 1, m_cloneNode), JS_CFUNC_DEF("contains", 1, m_contains), JS_CFUNC_DEF("querySelector", 1, m_qs), JS_CFUNC_DEF("querySelectorAll", 1, m_qsa), JS_CFUNC_DEF("closest", 1, m_closest),
    JS_CFUNC_DEF("matches", 1, m_matches), JS_CFUNC_DEF("getElementsByTagName", 1, m_byTag), JS_CFUNC_DEF("getElementsByClassName", 1, m_byClass),
    JS_CFUNC_DEF("addEventListener", 2, m_addEventListener), JS_CFUNC_DEF("dispatchEvent", 1, m_dispatchEvent), JS_CFUNC_DEF("click", 0, m_click), JS_CFUNC_DEF("focus", 0, m_focus), JS_CFUNC_DEF("blur", 0, m_blur),
    JS_CFUNC_DEF("scrollIntoView", 0, m_noop), JS_CFUNC_DEF("scrollTo", 0, m_noop), JS_CFUNC_DEF("select", 0, m_noop),
};
static const JSCFunctionListEntry DOC_FUNCS[] = {
    JS_CGETSET_MAGIC_DEF("title", d_title_get, d_title_set, 0), JS_CGETSET_MAGIC_DEF("body", d_tag, NULL, 0), JS_CGETSET_MAGIC_DEF("head", d_tag, NULL, 1), JS_CGETSET_MAGIC_DEF("documentElement", d_tag, NULL, 2),
    JS_CGETSET_MAGIC_DEF("activeElement", d_active, NULL, 0), JS_CGETSET_MAGIC_DEF("cookie", d_str, d_cookie_set, 0), JS_CGETSET_MAGIC_DEF("readyState", d_str, NULL, 1),
    JS_CGETSET_MAGIC_DEF("URL", d_str, NULL, 2), JS_CGETSET_MAGIC_DEF("referrer", d_str, NULL, 0), JS_CGETSET_MAGIC_DEF("defaultView", d_window, NULL, 0), JS_CGETSET_MAGIC_DEF("scripts", p_children, NULL, 0),
    JS_CFUNC_DEF("getElementById", 1, d_getById), JS_CFUNC_DEF("getElementsByName", 1, d_byName), JS_CFUNC_DEF("createElement", 1, d_create), JS_CFUNC_DEF("createTextNode", 1, d_text),
    JS_CFUNC_DEF("createDocumentFragment", 0, d_frag), JS_CFUNC_DEF("write", 1, d_write), JS_CFUNC_DEF("writeln", 1, d_write), JS_CFUNC_DEF("hasFocus", 0, m_noop),
};

static JSValue timer_set(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { return g_timer(c, t, argc, argv, 0); }
static JSValue timer_int(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) { return g_timer(c, t, argc, argv, 1); }
static void defg(JSContext *c, JSValueConst g, const char *name, JSCFunction *f, int n) { JS_SetPropertyStr(c, g, name, JS_NewCFunction(c, f, name, n)); }

void js_page_init(Page *p) {
    JSRuntime *rt = JS_NewRuntime(); if (!rt) return;
    JS_SetMemoryLimit(rt, 128u << 20); JS_SetMaxStackSize(rt, 1u << 20); JS_SetInterruptHandler(rt, interrupt_cb, p);
    JS_SetModuleLoaderFunc(rt, mod_normalize, mod_load, p);
    JSContext *c = JS_NewContext(rt); if (!c) { JS_FreeRuntime(rt); return; }
    p->J = c; JS_SetContextOpaque(c, p);
    JS_NewClassID(&node_cid);
    JSClassDef cd = { "Node", NULL, NULL, NULL, NULL }; JS_NewClass(rt, node_cid, &cd);
    JSValue proto = JS_NewObject(c); JS_SetPropertyFunctionList(c, proto, EL_FUNCS, sizeof EL_FUNCS / sizeof EL_FUNCS[0]);
    JSValue dp = JS_NewObjectProto(c, proto); JS_SetPropertyFunctionList(c, dp, DOC_FUNCS, sizeof DOC_FUNCS / sizeof DOC_FUNCS[0]);
    JS_DefinePropertyValueStr(c, proto, "__docproto", dp, 0);
    JS_SetClassProto(c, node_cid, JS_DupValue(c, proto));
    JSValue g = JS_GetGlobalObject(c);
    JSValue ctor = JS_NewCFunction2(c, g_ctor, "Element", 0, JS_CFUNC_constructor, 0); JS_SetPropertyStr(c, ctor, "prototype", JS_DupValue(c, proto)); JS_SetPropertyStr(c, g, "Element", ctor);
    JS_FreeValue(c, proto);
    JS_SetPropertyStr(c, g, "document", wrap(c, p->doc));
    defg(c, g, "setTimeout", timer_set, 2); defg(c, g, "setInterval", timer_int, 2); defg(c, g, "clearTimeout", g_clear, 1); defg(c, g, "clearInterval", g_clear, 1);
    defg(c, g, "alert", g_alert, 1); defg(c, g, "confirm", g_confirm, 1); defg(c, g, "prompt", g_prompt, 1);
    defg(c, g, "__log", g_log, 1); defg(c, g, "__fetch", g_fetch, 1); defg(c, g, "__nav", g_nav, 1); defg(c, g, "__navcode", g_navcode, 1); defg(c, g, "__href", g_href, 0);
    defg(c, g, "__sget", g_sget, 2); defg(c, g, "__sset", g_sset, 3);
    JS_FreeValue(c, g);
    size_t n = 0; char *pre = read_file(PRELUDE_PATH, &n, 200000);
    if (pre) { arm(p, 3000); JSValue r = JS_Eval(c, pre, n, "<prelude>", JS_EVAL_TYPE_GLOBAL); if (JS_IsException(r)) report(c); JS_FreeValue(c, r); p->js_deadline = 0; free(pre); }
    else js_log(p, "prelude.js introuvable : JavaScript limité");
}
void js_page_free(Page *p) {
    JSContext *c = CTX(p);
    if (c) {
        for (int i = 0; i < p->ntimers; i++) { JS_FreeValue(c, *(JSValue *)p->timers[i].fn); free(p->timers[i].fn); }
        for (int i = 0; i < p->nall; i++) { Node *n = p->all[i]; if (n->jsref) { JS_FreeValue(c, *(JSValue *)n->jsref); free(n->jsref); n->jsref = NULL; } }
        JSRuntime *rt = JS_GetRuntime(c); JS_FreeContext(c); JS_FreeRuntime(rt); p->J = NULL;
    }
    p->ntimers = 0;
}
static void run_script_nodes(Page *p, Node *n) {
    JSContext *c = CTX(p);
    for (Node *k = n->first; k; k = k->next) {
        if (k->type) continue;
        if (!strcmp(k->tag, "script")) {
            const char *ty = wn_attr(k, "type"); int mod = ty && !strcasecmp(ty, "module");
            if (ty && *ty && !mod && strcasecmp(ty, "text/javascript") && strcasecmp(ty, "application/javascript") && strcasecmp(ty, "text/ecmascript") && strcasecmp(ty, "text/babel")) continue;
            if (wn_attr(k, "nomodule")) continue;
            const char *src = wn_attr(k, "src"); char *code = NULL; size_t len = 0; const char *name = "<script>"; char full[800] = "";
            if (src && *src) { if (!p->fetch) continue; code = p->fetch(src, &len, p->fctx); name = src; if (!code) continue; if (len > 900000) { free(code); continue; } }
            else if (k->first && k->first->text) { code = strdup(k->first->text); len = strlen(code); }
            else continue;
            (void)full;
            arm(p, 4000);
            JSValue r = JS_Eval(c, code, len, mod ? (src ? name : p->url) : name, mod ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(r)) report(c); JS_FreeValue(c, r); free(code); p->js_deadline = 0; pump(p);
        } else if (strcmp(k->tag, "noscript") && strcmp(k->tag, "template")) run_script_nodes(p, k);
    }
}
void js_run_scripts(Page *p) {
    JSContext *c = CTX(p); if (!c) return;
    run_script_nodes(p, p->doc);
    js_dispatch(p, p->doc, "DOMContentLoaded", NULL);
    arm(p, 2000); JSValue r = JS_Eval(c, "__fire('DOMContentLoaded');__fire('load');", 42, "<load>", JS_EVAL_TYPE_GLOBAL); if (JS_IsException(r)) report(c); JS_FreeValue(c, r); p->js_deadline = 0; pump(p);
}
void js_run_timers(Page *p) {
    JSContext *c = CTX(p); if (!c) return;
    long now = now_ms(); int ran = 0;
    for (int i = 0; i < p->ntimers && ran < 8; i++) {
        Timer *t = &p->timers[i]; if (t->due > now) continue;
        JSValue fn = *(JSValue *)t->fn; int every = t->every >= 0; long iv = t->every; JSValue keep = JS_DupValue(c, fn);
        if (every) t->due = now + iv; else { JS_FreeValue(c, fn); free(t->fn); p->timers[i] = p->timers[--p->ntimers]; i--; }
        JSValue r = call(p, keep, JS_UNDEFINED, 0, NULL); JS_FreeValue(c, r); JS_FreeValue(c, keep); ran++;
    }
    pump(p); if (ran) JS_RunGC(JS_GetRuntime(c));
}
