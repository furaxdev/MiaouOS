/* Miaou Web: JavaScript (mujs, ES5) bindings: document, elements, events, timers, console, XHR. */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "web.h"
#include "mujs/mujs.h"

#define PG ((Page *)js_getcontext(J))
static Node *nd(js_State *J, int i) { return (Node *)js_touserdata(J, i, "Node"); }

static void pushnode(js_State *J, Node *n) {
    if (!n) { js_pushnull(J); return; }
    if (n->jsref) { js_getregistry(J, n->jsref); return; }
    js_getregistry(J, n == PG->doc ? "Document" : "Element");
    js_newuserdata(J, "Node", n, NULL);
    js_copy(J, -1); n->jsref = (char *)js_ref(J);
}
static void def_fn(js_State *J, const char *name, js_CFunction f, int n) { js_newcfunction(J, f, name, n); js_setproperty(J, -2, name); }
static void def_prop(js_State *J, const char *name, js_CFunction g, js_CFunction s) {
    js_newcfunction(J, g, name, 0); if (s) js_newcfunction(J, s, name, 1); else js_pushundefined(J);
    js_defaccessor(J, -3, name, 0);
}
static void def_attr(js_State *J, const char *name, js_CFunction g, js_CFunction s, const char *attr) {
    js_newcfunctionx(J, g, name, 0, (void *)attr, NULL); js_newcfunctionx(J, s, name, 1, (void *)attr, NULL); js_defaccessor(J, -3, name, 0);
}
static void push_list(js_State *J, Node **v, int n) { js_newarray(J); for (int i = 0; i < n; i++) { pushnode(J, v[i]); js_setindex(J, -2, i); } }

/* ---- attribute-backed properties ---- */
static void attr_get(js_State *J) { Node *n = nd(J, 0); const char *a = (const char *)js_currentfunctiondata(J); const char *v = wn_attr(n, a); js_pushstring(J, v ? v : ""); }
static void attr_set(js_State *J) { Node *n = nd(J, 0); const char *a = (const char *)js_currentfunctiondata(J); wn_set_attr(n, a, js_tostring(J, 1)); PG->dirty = 1; js_pushundefined(J); }
static void bool_get(js_State *J) { Node *n = nd(J, 0); js_pushboolean(J, wn_attr(n, (const char *)js_currentfunctiondata(J)) != NULL); }
static void bool_set(js_State *J) { Node *n = nd(J, 0); const char *a = (const char *)js_currentfunctiondata(J); if (js_toboolean(J, 1)) wn_set_attr(n, a, ""); else wn_del_attr(n, a); PG->dirty = 1; js_pushundefined(J); }
static void def_bool(js_State *J, const char *name, const char *attr) { js_newcfunctionx(J, bool_get, name, 0, (void *)attr, NULL); js_newcfunctionx(J, bool_set, name, 1, (void *)attr, NULL); js_defaccessor(J, -3, name, 0); }

static void p_tagName(js_State *J) { Node *n = nd(J, 0); char u[20]; size_t i = 0; for (; n->tag[i] && i < 19; i++) u[i] = (char)toupper((unsigned char)n->tag[i]); u[i] = 0; js_pushstring(J, u); }
static void p_textContent(js_State *J) { char *t = wn_text(nd(J, 0)); js_pushstring(J, t); free(t); }
static void s_textContent(js_State *J) { wn_set_text(PG, nd(J, 0), js_tostring(J, 1)); PG->dirty = 1; js_pushundefined(J); }
static void p_innerHTML(js_State *J) { char *t = wn_inner_html(nd(J, 0)); js_pushstring(J, t); free(t); }
static void s_innerHTML(js_State *J) { wn_set_inner_html(PG, nd(J, 0), js_tostring(J, 1)); PG->dirty = 1; js_pushundefined(J); }
static void p_value(js_State *J) {
    Node *n = nd(J, 0);
    if (!strcmp(n->tag, "textarea")) { char *t = wn_text(n); js_pushstring(J, t); free(t); return; }
    if (!strcmp(n->tag, "select")) { Node *sel = NULL; for (Node *o = n->first; o; o = o->next) if (!o->type && !strcmp(o->tag, "option")) { if (!sel || wn_attr(o, "selected")) sel = o; if (wn_attr(o, "selected")) break; }
        if (!sel) { js_pushstring(J, ""); return; } const char *v = wn_attr(sel, "value"); if (v) js_pushstring(J, v); else { char *t = wn_text(sel); js_pushstring(J, t); free(t); } return; }
    const char *v = wn_attr(n, "value"); js_pushstring(J, v ? v : "");
}
static void s_value(js_State *J) { Node *n = nd(J, 0); if (!strcmp(n->tag, "textarea")) wn_set_text(PG, n, js_tostring(J, 1)); else wn_set_attr(n, "value", js_tostring(J, 1)); js_pushundefined(J); }
static void p_parent(js_State *J) { pushnode(J, nd(J, 0)->parent); }
static void p_first(js_State *J) { pushnode(J, nd(J, 0)->first); }
static void p_last(js_State *J) { pushnode(J, nd(J, 0)->last); }
static void p_next(js_State *J) { pushnode(J, nd(J, 0)->next); }
static void p_prev(js_State *J) { pushnode(J, nd(J, 0)->prev); }
static void p_firstel(js_State *J) { Node *c = nd(J, 0)->first; while (c && c->type) c = c->next; pushnode(J, c); }
static void p_lastel(js_State *J) { Node *c = nd(J, 0)->last; while (c && c->type) c = c->prev; pushnode(J, c); }
static void p_nextel(js_State *J) { Node *c = nd(J, 0)->next; while (c && c->type) c = c->next; pushnode(J, c); }
static void p_prevel(js_State *J) { Node *c = nd(J, 0)->prev; while (c && c->type) c = c->prev; pushnode(J, c); }
static void p_children(js_State *J) { Node *n = nd(J, 0); js_newarray(J); int i = 0; for (Node *c = n->first; c; c = c->next) if (!c->type) { pushnode(J, c); js_setindex(J, -2, i++); } }
static void p_childnodes(js_State *J) { Node *n = nd(J, 0); js_newarray(J); int i = 0; for (Node *c = n->first; c; c = c->next) { pushnode(J, c); js_setindex(J, -2, i++); } }
static void p_zero(js_State *J) { js_pushnumber(J, 0); }
static void p_nodetype(js_State *J) { Node *n = nd(J, 0); js_pushnumber(J, n->type ? 3 : n == PG->doc ? 9 : 1); }
static void p_nodevalue(js_State *J) { Node *n = nd(J, 0); if (n->type) js_pushstring(J, n->text ? n->text : ""); else js_pushnull(J); }

/* ---- style object ---- */
static void kebab(const char *in, char *out, size_t n) { size_t k = 0; for (; *in && k + 2 < n; in++) { if (isupper((unsigned char)*in)) { out[k++] = '-'; out[k++] = (char)tolower((unsigned char)*in); } else out[k++] = *in; } out[k] = 0; }
static int st_has(js_State *J, void *p, const char *name) {
    static const char *skip[] = { "setProperty", "getPropertyValue", "removeProperty", "toString", "valueOf", "constructor", "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable", "toLocaleString", "length", "__proto__", NULL };
    for (int i = 0; skip[i]; i++) if (!strcmp(name, skip[i])) return 0;
    char k[60]; kebab(name, k, sizeof k);
    if (!strcmp(name, "cssText")) { const char *v = wn_attr((Node *)p, "style"); js_pushstring(J, v ? v : ""); return 1; }
    char *v = css_style_get((Node *)p, k); js_pushstring(J, v ? v : ""); free(v); return 1;
}
static int st_put(js_State *J, void *p, const char *name) {
    char k[60]; kebab(name, k, sizeof k);
    if (!strcmp(name, "cssText")) wn_set_attr((Node *)p, "style", js_tostring(J, -1)); else css_style_prop((Node *)p, k, js_tostring(J, -1));
    PG->dirty = 1; return 1;
}
static void p_style(js_State *J) { Node *n = nd(J, 0); js_getregistry(J, "Style"); js_newuserdatax(J, "Style", n, st_has, st_put, NULL, NULL); }
static void style_set(js_State *J) { Node *n = (Node *)js_touserdata(J, 0, "Style"); css_style_prop(n, js_tostring(J, 1), js_tostring(J, 2)); PG->dirty = 1; js_pushundefined(J); }
static void style_get(js_State *J) { Node *n = (Node *)js_touserdata(J, 0, "Style"); char *v = css_style_get(n, js_tostring(J, 1)); js_pushstring(J, v ? v : ""); free(v); }
static void style_rm(js_State *J) { Node *n = (Node *)js_touserdata(J, 0, "Style"); css_style_prop(n, js_tostring(J, 1), ""); PG->dirty = 1; js_pushundefined(J); }

/* ---- element methods ---- */
static void m_getAttribute(js_State *J) { const char *v = wn_attr(nd(J, 0), js_tostring(J, 1)); if (v) js_pushstring(J, v); else js_pushnull(J); }
static void m_setAttribute(js_State *J) { wn_set_attr(nd(J, 0), js_tostring(J, 1), js_tostring(J, 2)); PG->dirty = 1; js_pushundefined(J); }
static void m_removeAttribute(js_State *J) { wn_del_attr(nd(J, 0), js_tostring(J, 1)); PG->dirty = 1; js_pushundefined(J); }
static void m_hasAttribute(js_State *J) { js_pushboolean(J, wn_attr(nd(J, 0), js_tostring(J, 1)) != NULL); }
static void m_appendChild(js_State *J) { Node *p = nd(J, 0), *c = nd(J, 1); if (c == p) js_error(J, "HierarchyRequestError"); wn_append(p, c); PG->dirty = 1; js_copy(J, 1); }
static void m_insertBefore(js_State *J) { Node *p = nd(J, 0), *c = nd(J, 1); Node *r = js_isnull(J, 2) || js_isundefined(J, 2) ? NULL : nd(J, 2); wn_insert_before(p, c, r); PG->dirty = 1; js_copy(J, 1); }
static void m_removeChild(js_State *J) { Node *c = nd(J, 1); if (c->parent == nd(J, 0)) wn_unlink(c); PG->dirty = 1; js_copy(J, 1); }
static void m_replaceChild(js_State *J) { Node *p = nd(J, 0), *nw = nd(J, 1), *old = nd(J, 2); wn_insert_before(p, nw, old); wn_unlink(old); PG->dirty = 1; js_copy(J, 2); }
static void m_remove(js_State *J) { wn_unlink(nd(J, 0)); PG->dirty = 1; js_pushundefined(J); }
static Node *clone(Page *p, Node *n, int deep) {
    Node *c = wn_new(p, n->type, n->type ? NULL : n->tag); if (n->type) c->text = strdup(n->text ? n->text : "");
    for (int i = 0; i < n->na; i++) wn_set_attr(c, n->an[i], n->av[i]);
    if (deep) for (Node *k = n->first; k; k = k->next) wn_append(c, clone(p, k, 1));
    return c;
}
static void m_cloneNode(js_State *J) { pushnode(J, clone(PG, nd(J, 0), js_toboolean(J, 1))); }
static void m_contains(js_State *J) { Node *a = nd(J, 0), *b = nd(J, 1); for (Node *x = b; x; x = x->parent) if (x == a) { js_pushboolean(J, 1); return; } js_pushboolean(J, 0); }
static void m_qs(js_State *J) { pushnode(J, css_query_first(nd(J, 0), js_tostring(J, 1))); }
static void m_qsa(js_State *J) { Node *buf[512]; int n = css_query_all(nd(J, 0), js_tostring(J, 1), buf, 512); push_list(J, buf, n); }
static void m_closest(js_State *J) { Node *n = nd(J, 0); const char *sel = js_tostring(J, 1); Node *root = PG->doc; Node *all[2048]; int cnt = css_query_all(root, sel, all, 2048);
    for (Node *x = n; x; x = x->parent) for (int i = 0; i < cnt; i++) if (all[i] == x) { pushnode(J, x); return; } js_pushnull(J); }
static void m_matches(js_State *J) { Node *n = nd(J, 0); Node *all[2048]; int cnt = css_query_all(PG->doc, js_tostring(J, 1), all, 2048); for (int i = 0; i < cnt; i++) if (all[i] == n) { js_pushboolean(J, 1); return; } js_pushboolean(J, 0); }
static void walk_tag(Node *n, const char *tag, Node **out, int *c, int max) { for (Node *k = n->first; k && *c < max; k = k->next) if (!k->type) { if (!strcmp(tag, "*") || !strcasecmp(k->tag, tag)) out[(*c)++] = k; walk_tag(k, tag, out, c, max); } }
static void m_byTag(js_State *J) { Node *buf[1024]; int c = 0; walk_tag(nd(J, 0), js_tostring(J, 1), buf, &c, 1024); push_list(J, buf, c); }
static void m_byClass(js_State *J) { char sel[100]; snprintf(sel, sizeof sel, ".%s", js_tostring(J, 1)); Node *buf[1024]; int n = css_query_all(nd(J, 0), sel, buf, 1024); push_list(J, buf, n); }
static void m_getById(js_State *J) { char sel[100]; snprintf(sel, sizeof sel, "#%s", js_tostring(J, 1)); pushnode(J, css_query_first(PG->doc, sel)); }
static void m_byName(js_State *J) { char sel[100]; snprintf(sel, sizeof sel, "[name=%s]", js_tostring(J, 1)); Node *buf[256]; int n = css_query_all(PG->doc, sel, buf, 256); push_list(J, buf, n); }
static void m_createElement(js_State *J) { pushnode(J, wn_new(PG, 0, js_tostring(J, 1))); }
static void m_createTextNode(js_State *J) { Node *t = wn_new(PG, 1, NULL); t->text = strdup(js_tostring(J, 1)); pushnode(J, t); }
static void m_noop(js_State *J) { js_pushundefined(J); }
static void m_click(js_State *J) { js_dispatch(PG, nd(J, 0), "click", NULL); js_pushundefined(J); }
static void m_focus(js_State *J) { PG->focus = nd(J, 0); js_pushundefined(J); }
static void m_blur(js_State *J) { if (PG->focus == nd(J, 0)) PG->focus = NULL; js_pushundefined(J); }

/* ---- events ---- */
static void m_addEventListener(js_State *J) {
    if (!js_iscallable(J, 2)) { js_pushundefined(J); return; }
    const char *type = js_tostring(J, 1);
    if (!js_hasproperty(J, 0, "__ev")) { js_newobject(J); js_copy(J, -1); js_setproperty(J, 0, "__ev"); }
    if (!js_hasproperty(J, -1, type)) { js_newarray(J); js_copy(J, -1); js_setproperty(J, -3, type); }
    int n = js_getlength(J, -1); js_copy(J, 2); js_setindex(J, -2, n);
    js_pop(J, 2); js_pushundefined(J);
}
static void ev_prevent(js_State *J) { js_pushboolean(J, 1); js_setproperty(J, 0, "defaultPrevented"); js_pushundefined(J); }
static void ev_stop(js_State *J) { js_pushboolean(J, 1); js_setproperty(J, 0, "__stop"); js_pushundefined(J); }
/* run an inline on<event> attribute: the code becomes a function(event) called with this = node */
static void run_attr(js_State *J, Node *n, const char *code, int ev) {
    size_t l = strlen(code); char *src = malloc(l + 64); snprintf(src, l + 64, "__h = function(event){%s\n}", code);
    if (js_dostring(J, src)) { free(src); return; }
    free(src);
    js_getglobal(J, "__h"); pushnode(J, n); js_copy(J, ev);
    if (js_pcall(J, 1)) js_report(J, js_tostring(J, -1));
    else if (js_isboolean(J, -1) && !js_toboolean(J, -1)) { js_pushboolean(J, 1); js_setproperty(J, ev, "defaultPrevented"); }
    js_pop(J, 1);
}
int js_run_inline(Page *p, Node *n, const char *code) {
    js_State *J = p->J; if (!J) return 0;
    js_newobject(J); int ev = js_gettop(J) - 1; run_attr(J, n, code, ev); js_pop(J, 1); return 1;
}
int js_dispatch(Page *p, Node *target, const char *type, const char *key) {
    js_State *J = p->J; if (!J) return 0;
    int prevented = 0;
    js_newobject(J); int E = js_gettop(J) - 1;
    js_pushstring(J, type); js_setproperty(J, E, "type");
    if (key) { js_pushstring(J, key); js_setproperty(J, E, "key"); }
    pushnode(J, target); js_setproperty(J, E, "target");
    js_newcfunction(J, ev_prevent, "preventDefault", 0); js_setproperty(J, E, "preventDefault");
    js_newcfunction(J, ev_stop, "stopPropagation", 0); js_setproperty(J, E, "stopPropagation");
    js_newcfunction(J, ev_stop, "stopImmediatePropagation", 0); js_setproperty(J, E, "stopImmediatePropagation");
    js_pushboolean(J, 0); js_setproperty(J, E, "defaultPrevented");
    char onattr[40]; snprintf(onattr, sizeof onattr, "on%s", type);
    for (Node *n = target; n; n = n->parent) {
        pushnode(J, n); int N = js_gettop(J) - 1;
        js_copy(J, N); js_setproperty(J, E, "currentTarget");
        const char *code = n->type == 0 ? wn_attr(n, onattr) : NULL;
        if (code && *code) run_attr(J, n, code, E);
        if (js_hasproperty(J, N, onattr)) { if (js_iscallable(J, -1)) { js_copy(J, N); js_copy(J, E); if (js_pcall(J, 1)) js_report(J, js_tostring(J, -1)); js_pop(J, 1); } js_pop(J, 1); }
        if (js_hasproperty(J, N, "__ev")) {
            if (js_hasproperty(J, -1, type)) {
                int len = js_getlength(J, -1);
                for (int i = 0; i < len; i++) { js_getindex(J, -1, i); js_copy(J, N); js_copy(J, E); if (js_pcall(J, 1)) js_report(J, js_tostring(J, -1)); js_pop(J, 1); }
                js_pop(J, 1);
            }
            js_pop(J, 1);
        }
        js_pop(J, 1);
        js_getproperty(J, E, "__stop"); int st = js_toboolean(J, -1); js_pop(J, 1); if (st) break;
    }
    js_getproperty(J, E, "defaultPrevented"); prevented = js_toboolean(J, -1); js_pop(J, 1);
    js_pop(J, 1);
    return prevented;
}

/* ---- timers, console, misc globals ---- */
static void g_timer(js_State *J) {
    Page *p = PG; int every = (int)(intptr_t)js_currentfunctiondata(J);
    if (!js_iscallable(J, 1) || p->ntimers >= 32) { js_pushnumber(J, 0); return; }
    long ms = js_tointeger(J, 2); if (ms < 0) ms = 0; if (every && ms < 10) ms = 10;
    js_copy(J, 1); const char *ref = js_ref(J);
    Timer *t = &p->timers[p->ntimers++]; t->id = p->next_timer++; t->due = now_ms() + ms; t->every = every ? ms : -1; t->ref = strdup(ref);
    js_pushnumber(J, t->id);
}
static void g_clear(js_State *J) {
    Page *p = PG; int id = js_tointeger(J, 1);
    for (int i = 0; i < p->ntimers; i++) if (p->timers[i].id == id) { free(p->timers[i].ref); p->timers[i] = p->timers[--p->ntimers]; break; }
    js_pushundefined(J);
}
void js_log(Page *p, const char *msg) { if (p->ncons < 6) snprintf(p->console[p->ncons++], 160, "%s", msg); else { memmove(p->console[0], p->console[1], 160 * 5); snprintf(p->console[5], 160, "%s", msg); } }
static void g_log(js_State *J) { char buf[300] = ""; int n = js_gettop(J); for (int i = 1; i < n; i++) { const char *s = js_tostring(J, i); if (strlen(buf) + strlen(s) + 2 < sizeof buf) { strcat(buf, s); strcat(buf, " "); } } js_log(PG, buf); js_pushundefined(J); }
static void g_alert(js_State *J) { toast("%.100s", js_tostring(J, 1)); js_pushundefined(J); }
static void g_confirm(js_State *J) { toast("%.100s", js_tostring(J, 1)); js_pushboolean(J, 1); }
static void g_prompt(js_State *J) { js_pushnull(J); }
static void g_fetch(js_State *J) {
    Page *p = PG; if (!p->fetch) { js_pushnull(J); return; }
    size_t len = 0; char *b = p->fetch(js_tostring(J, 1), &len, p->fctx);
    if (!b) { js_pushnull(J); return; } js_pushstring(J, b); free(b);
}
static void g_nav(js_State *J) { Page *p = PG; const char *u = js_tostring(J, 1); snprintf(p->nav_url, sizeof p->nav_url, "%s", u); p->pending_nav = 1; js_pushundefined(J); }
static void g_nav_code(js_State *J) { PG->pending_nav = (int)(intptr_t)js_currentfunctiondata(J); js_pushundefined(J); }
static void d_title_get(js_State *J) { Node *t = wn_find_tag(PG->doc, "title"); if (!t) { js_pushstring(J, ""); return; } char *x = wn_text(t); js_pushstring(J, x); free(x); }
static void d_title_set(js_State *J) { Page *p = PG; Node *t = wn_find_tag(p->doc, "title"); if (!t) { Node *h = wn_find_tag(p->doc, "head"); t = wn_new(p, 0, "title"); wn_append(h ? h : p->doc, t); } wn_set_text(p, t, js_tostring(J, 1)); snprintf(p->title, sizeof p->title, "%s", js_tostring(J, 1)); js_pushundefined(J); }
static void d_body(js_State *J) { pushnode(J, wn_find_tag(PG->doc, "body")); }
static void d_head(js_State *J) { pushnode(J, wn_find_tag(PG->doc, "head")); }
static void d_html(js_State *J) { pushnode(J, wn_find_tag(PG->doc, "html")); }
static void d_cookie_get(js_State *J) { js_pushstring(J, ""); }
static void d_cookie_set(js_State *J) { js_pushundefined(J); }
static void d_ready(js_State *J) { js_pushstring(J, "complete"); }
static void d_url(js_State *J) { js_pushstring(J, PG->url); }
static void d_write(js_State *J) { Page *p = PG; Node *b = wn_find_tag(p->doc, "body"); wn_parse(p, js_tostring(J, 1), b ? b : p->doc); p->dirty = 1; js_pushundefined(J); }
static void l_href_get(js_State *J) { js_pushstring(J, PG->url); }
static void d_location(js_State *J) { js_getglobal(J, "location"); }

static void report(js_State *J, const char *msg) { Page *p = PG; snprintf(p->err, sizeof p->err, "%s", msg); char b[200]; snprintf(b, sizeof b, "Erreur JS : %.170s", msg); js_log(p, b); }

static const char *PRELUDE =
"var window=this, self=this, globalThis=this, parent=this, top=this, frames=this;\n"
"Element.prototype.append=function(){for(var i=0;i<arguments.length;i++){var a=arguments[i];this.appendChild(typeof a==='object'&&a!==null?a:document.createTextNode(String(a)));}};\n"
"Element.prototype.prepend=function(a){this.insertBefore(typeof a==='object'?a:document.createTextNode(String(a)),this.firstChild);};\n"
"Element.prototype.removeEventListener=function(t,f){var l=this.__ev&&this.__ev[t];if(l)for(var i=0;i<l.length;i++)if(l[i]===f){l.splice(i,1);break;}};\n"
"Element.prototype.insertAdjacentHTML=function(pos,h){if(pos==='beforeend')this.innerHTML=this.innerHTML+h;else if(pos==='afterbegin')this.innerHTML=h+this.innerHTML;};\n"
"Object.defineProperty(Element.prototype,'classList',{get:function(){var el=this;return{contains:function(c){return(' '+el.className+' ').indexOf(' '+c+' ')>=0;},"
"add:function(){for(var i=0;i<arguments.length;i++){if(!this.contains(arguments[i]))el.className=(el.className+' '+arguments[i]).replace(/^\\s+/,'');}},"
"remove:function(){for(var i=0;i<arguments.length;i++){el.className=(' '+el.className+' ').replace(' '+arguments[i]+' ',' ').replace(/^\\s+|\\s+$/g,'');}},"
"toggle:function(c){if(this.contains(c)){this.remove(c);return false;}this.add(c);return true;}};}});\n"
"Object.defineProperty(Element.prototype,'dataset',{get:function(){var o={},a=this.attributes||[];return o;}});\n"
"function __store(){var d={};return{getItem:function(k){return d.hasOwnProperty(k)?d[k]:null;},setItem:function(k,v){d[k]=String(v);},removeItem:function(k){delete d[k];},clear:function(){d={};},key:function(i){return Object.keys(d)[i]||null;}};}\n"
"var localStorage=__store(), sessionStorage=__store();\n"
"var navigator={userAgent:'Mozilla/5.0 (X11; MiaouOS) MiaouWeb/1.0',language:'fr-FR',languages:['fr-FR'],platform:'MiaouOS',cookieEnabled:false,onLine:true};\n"
"var screen={width:1280,height:800,availWidth:1280,availHeight:800};\n"
"var innerWidth=1000, innerHeight=600, devicePixelRatio=1;\n"
"function requestAnimationFrame(f){return setTimeout(function(){f(Date.now());},16);}\n"
"function cancelAnimationFrame(i){clearTimeout(i);}\n"
"function getComputedStyle(el){return el.style;}\n"
"var __wl={};\n"
"function addEventListener(t,f){(__wl[t]=__wl[t]||[]).push(f);}\n"
"function removeEventListener(t,f){var l=__wl[t]||[];for(var i=0;i<l.length;i++)if(l[i]===f){l.splice(i,1);break;}}\n"
"function dispatchEvent(e){__fire(e.type);}\n"
"function __fire(t){var l=(__wl[t]||[]).slice();for(var i=0;i<l.length;i++){try{l[i]({type:t,target:window});}catch(e){console.error(String(e));}}if(typeof window['on'+t]==='function'){try{window['on'+t]({type:t});}catch(e){console.error(String(e));}}}\n"
"var console={log:__log,info:__log,warn:__log,error:__log,debug:__log};\n"
"var history={back:__back,forward:__fwd,go:function(){},length:1,pushState:function(){},replaceState:function(){}};\n"
"function XMLHttpRequest(){this.readyState=0;this.status=0;this.responseText='';}\n"
"XMLHttpRequest.prototype.open=function(m,u){this.method=m;this.url=u;this.readyState=1;};\n"
"XMLHttpRequest.prototype.setRequestHeader=function(){};\n"
"XMLHttpRequest.prototype.getResponseHeader=function(){return null;};\n"
"XMLHttpRequest.prototype.abort=function(){};\n"
"XMLHttpRequest.prototype.send=function(){var s=this;setTimeout(function(){var r=__fetch(s.url);s.readyState=4;if(r===null){s.status=0;if(s.onerror)s.onerror();}else{s.status=200;s.responseText=r;s.response=r;if(s.onreadystatechange)s.onreadystatechange();if(s.onload)s.onload();}},0);};\n"
"document.addEventListener('DOMContentLoaded',function(){});\n";

void js_page_init(Page *p) {
    js_State *J = js_newstate(NULL, NULL, 0); p->J = J; if (!J) return;
    js_setcontext(J, p); js_setreport(J, report);
    /* prototypes */
    js_newobject(J);                                                     /* Element proto */
    def_attr(J, "id", attr_get, attr_set, "id"); def_attr(J, "className", attr_get, attr_set, "class"); def_attr(J, "href", attr_get, attr_set, "href");
    def_attr(J, "src", attr_get, attr_set, "src"); def_attr(J, "name", attr_get, attr_set, "name"); def_attr(J, "type", attr_get, attr_set, "type"); def_attr(J, "title", attr_get, attr_set, "title");
    def_attr(J, "placeholder", attr_get, attr_set, "placeholder"); def_attr(J, "alt", attr_get, attr_set, "alt"); def_attr(J, "htmlFor", attr_get, attr_set, "for"); def_attr(J, "action", attr_get, attr_set, "action");
    def_attr(J, "method", attr_get, attr_set, "method"); def_attr(J, "rel", attr_get, attr_set, "rel"); def_attr(J, "target", attr_get, attr_set, "target"); def_attr(J, "lang", attr_get, attr_set, "lang");
    def_bool(J, "checked", "checked"); def_bool(J, "disabled", "disabled"); def_bool(J, "hidden", "hidden"); def_bool(J, "selected", "selected"); def_bool(J, "readOnly", "readonly");
    def_prop(J, "tagName", p_tagName, NULL); def_prop(J, "nodeName", p_tagName, NULL); def_prop(J, "textContent", p_textContent, s_textContent); def_prop(J, "innerText", p_textContent, s_textContent);
    def_prop(J, "innerHTML", p_innerHTML, s_innerHTML); def_prop(J, "value", p_value, s_value); def_prop(J, "style", p_style, NULL);
    def_prop(J, "parentNode", p_parent, NULL); def_prop(J, "parentElement", p_parent, NULL); def_prop(J, "firstChild", p_first, NULL); def_prop(J, "lastChild", p_last, NULL);
    def_prop(J, "nextSibling", p_next, NULL); def_prop(J, "previousSibling", p_prev, NULL); def_prop(J, "firstElementChild", p_firstel, NULL); def_prop(J, "lastElementChild", p_lastel, NULL);
    def_prop(J, "nextElementSibling", p_nextel, NULL); def_prop(J, "previousElementSibling", p_prevel, NULL); def_prop(J, "children", p_children, NULL); def_prop(J, "childNodes", p_childnodes, NULL);
    def_prop(J, "offsetWidth", p_zero, NULL); def_prop(J, "offsetHeight", p_zero, NULL); def_prop(J, "clientWidth", p_zero, NULL); def_prop(J, "clientHeight", p_zero, NULL);
    def_prop(J, "scrollTop", p_zero, m_noop); def_prop(J, "nodeType", p_nodetype, NULL); def_prop(J, "nodeValue", p_nodevalue, NULL);
    def_fn(J, "getAttribute", m_getAttribute, 1); def_fn(J, "setAttribute", m_setAttribute, 2); def_fn(J, "removeAttribute", m_removeAttribute, 1); def_fn(J, "hasAttribute", m_hasAttribute, 1);
    def_fn(J, "appendChild", m_appendChild, 1); def_fn(J, "insertBefore", m_insertBefore, 2); def_fn(J, "removeChild", m_removeChild, 1); def_fn(J, "replaceChild", m_replaceChild, 2); def_fn(J, "remove", m_remove, 0);
    def_fn(J, "cloneNode", m_cloneNode, 1); def_fn(J, "contains", m_contains, 1); def_fn(J, "querySelector", m_qs, 1); def_fn(J, "querySelectorAll", m_qsa, 1); def_fn(J, "closest", m_closest, 1);
    def_fn(J, "matches", m_matches, 1); def_fn(J, "getElementsByTagName", m_byTag, 1); def_fn(J, "getElementsByClassName", m_byClass, 1);
    def_fn(J, "addEventListener", m_addEventListener, 2); def_fn(J, "click", m_click, 0); def_fn(J, "focus", m_focus, 0); def_fn(J, "blur", m_blur, 0);
    def_fn(J, "scrollIntoView", m_noop, 0); def_fn(J, "dispatchEvent", m_noop, 1);
    js_copy(J, -1); js_setregistry(J, "Element");
    js_newcfunction(J, m_noop, "Element", 0); js_copy(J, -2); js_setproperty(J, -2, "prototype"); js_setglobal(J, "Element"); js_pop(J, 1);
    /* Document proto inherits Element */
    js_getregistry(J, "Element"); js_newobjectx(J);
    def_prop(J, "title", d_title_get, d_title_set); def_prop(J, "body", d_body, NULL); def_prop(J, "head", d_head, NULL); def_prop(J, "documentElement", d_html, NULL);
    def_prop(J, "cookie", d_cookie_get, d_cookie_set); def_prop(J, "readyState", d_ready, NULL); def_prop(J, "URL", d_url, NULL); def_prop(J, "location", d_location, NULL);
    def_fn(J, "getElementById", m_getById, 1); def_fn(J, "createElement", m_createElement, 1); def_fn(J, "createTextNode", m_createTextNode, 1); def_fn(J, "getElementsByName", m_byName, 1); def_fn(J, "write", d_write, 1); def_fn(J, "writeln", d_write, 1);
    js_setregistry(J, "Document");
    /* Style proto */
    js_newobject(J); def_fn(J, "setProperty", style_set, 2); def_fn(J, "getPropertyValue", style_get, 1); def_fn(J, "removeProperty", style_rm, 1); js_setregistry(J, "Style");
    /* globals */
    pushnode(J, p->doc); js_setglobal(J, "document");
    js_newcfunctionx(J, g_timer, "setTimeout", 2, (void *)0, NULL); js_setglobal(J, "setTimeout");
    js_newcfunctionx(J, g_timer, "setInterval", 2, (void *)1, NULL); js_setglobal(J, "setInterval");
    js_newcfunction(J, g_clear, "clearTimeout", 1); js_setglobal(J, "clearTimeout");
    js_newcfunction(J, g_clear, "clearInterval", 1); js_setglobal(J, "clearInterval");
    js_newcfunction(J, g_log, "__log", 1); js_setglobal(J, "__log");
    js_newcfunction(J, g_alert, "alert", 1); js_setglobal(J, "alert");
    js_newcfunction(J, g_confirm, "confirm", 1); js_setglobal(J, "confirm");
    js_newcfunction(J, g_prompt, "prompt", 1); js_setglobal(J, "prompt");
    js_newcfunction(J, g_fetch, "__fetch", 1); js_setglobal(J, "__fetch");
    js_newcfunctionx(J, g_nav_code, "__back", 0, (void *)2, NULL); js_setglobal(J, "__back");
    js_newcfunctionx(J, g_nav_code, "__fwd", 0, (void *)3, NULL); js_setglobal(J, "__fwd");
    js_newobject(J);
    def_prop(J, "href", l_href_get, g_nav);
    def_fn(J, "assign", g_nav, 1); def_fn(J, "replace", g_nav, 1);
    js_newcfunctionx(J, g_nav_code, "reload", 0, (void *)4, NULL); js_setproperty(J, -2, "reload");
    js_setglobal(J, "location");
    js_dostring(J, PRELUDE);
}
void js_page_free(Page *p) {
    for (int i = 0; i < p->ntimers; i++) free(p->timers[i].ref);
    p->ntimers = 0;
    if (p->J) { js_freestate((js_State *)p->J); p->J = NULL; }
}
static void run_script_nodes(Page *p, Node *n) {
    for (Node *c = n->first; c; c = c->next) {
        if (c->type) continue;
        if (!strcmp(c->tag, "script")) {
            const char *ty = wn_attr(c, "type"); if (ty && *ty && strcasecmp(ty, "text/javascript") && strcasecmp(ty, "application/javascript") && strcasecmp(ty, "text/ecmascript")) continue;
            const char *src = wn_attr(c, "src");
            if (src && *src) {
                if (!p->fetch) continue;
                size_t len = 0; char *code = p->fetch(src, &len, p->fctx);
                if (code) { if (len < 600000) js_dostring((js_State *)p->J, code); free(code); }
            } else if (c->first && c->first->text) js_dostring((js_State *)p->J, c->first->text);
        } else if (strcmp(c->tag, "noscript") && strcmp(c->tag, "template")) run_script_nodes(p, c);
    }
}
void js_run_scripts(Page *p) {
    if (!p->J) return;
    run_script_nodes(p, p->doc);
    js_dispatch(p, p->doc, "DOMContentLoaded", NULL);
    js_dostring((js_State *)p->J, "__fire('DOMContentLoaded');__fire('load');");
}
void js_run_timers(Page *p) {
    js_State *J = p->J; if (!J) return;
    long now = now_ms(); int ran = 0;
    for (int i = 0; i < p->ntimers && ran < 8; i++) {
        Timer *t = &p->timers[i]; if (t->due > now) continue;
        char *ref = strdup(t->ref); int every = t->every >= 0; long iv = t->every;
        if (every) t->due = now + iv; else { free(t->ref); p->timers[i] = p->timers[--p->ntimers]; i--; }
        js_getregistry(J, ref); js_pushundefined(J);
        if (js_pcall(J, 0)) js_report(J, js_tostring(J, -1));
        js_pop(J, 1); free(ref); ran++;
    }
    js_gc(J, 0);
}
