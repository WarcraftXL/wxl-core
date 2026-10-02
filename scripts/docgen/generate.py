#!/usr/bin/env python3
# Generates the offline API reference (docs/site/) from the doc comments of the public headers.
# Standard library only; takes no arguments; include/wxl/offsets/ is never read.

import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
INCLUDE = ROOT / "include" / "wxl"
OUT = ROOT / "docs" / "site"
ASSETS = Path(__file__).resolve().parent

SCRIPT_TYPES = ["WorldScript", "RenderScript", "ModelScript", "ObjectScript", "AssetScript"]
OBJECT_BASE_FILES = ["Object.hpp", "Unit.hpp", "Player.hpp"]  # the base chain, documented in order
FRAMEWORK_NAMES = {"ScriptObject", "ScriptMgr", "WXL_DECLARE_EXTENSION"}
# The rest of the extension-authoring surface, one top-level header each. CfgParse.hpp is left out:
# it is the parser Config.hpp is built on, not something an extension calls.
PLUMBING_FILES = ["Hook.hpp", "Service.hpp", "Config.hpp"]


# ---------------------------------------------------------------- lexer

def lex(text):
    """Splits a C++ source into doc, comment, preprocessor and code tokens: (kind, text, line)."""
    toks = []
    i, n, line = 0, len(text), 1
    code, code_line = [], 1
    at_line_start = True

    def flush():
        nonlocal code
        if code:
            toks.append(("code", "".join(code), code_line))
            code = []

    while i < n:
        c = text[i]
        if c == "\n":
            if not code:
                code_line = line
            code.append(c)
            line += 1
            i += 1
            at_line_start = True
            continue
        if at_line_start and c in " \t":
            if not code:
                code_line = line
            code.append(c)
            i += 1
            continue
        if at_line_start and c == "#":
            flush()
            start, start_line = i, line
            while i < n:
                if text[i] == "\n":
                    if text[i - 1] == "\\" or (text[i - 1] == "\r" and text[i - 2] == "\\"):
                        line += 1
                        i += 1
                        continue
                    break
                i += 1
            toks.append(("pp", text[start:i], start_line))
            continue
        at_line_start = False
        if text.startswith("//", i):
            flush()
            end = text.find("\n", i)
            end = n if end < 0 else end
            body = text[i:end]
            if body.startswith("///") and not body.startswith("////") and not body.startswith("///<"):
                toks.append(("doc3", body[3:], line))
            else:
                toks.append(("comment", body, line))
            i = end
            continue
        if text.startswith("/*", i):
            flush()
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            body = text[i:end]
            is_doc = body.startswith("/**") and not body.startswith("/**/") and not body.startswith("/**<")
            toks.append(("doc" if is_doc else "comment", body, line))
            line += body.count("\n")
            i = end
            continue
        if c in "\"'":
            if not code:
                code_line = line
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            code.append(text[i:j + 1])
            i = j + 1
            continue
        if not code:
            code_line = line
        code.append(c)
        i += 1
    flush()
    return toks


# ---------------------------------------------------------------- doc comments

def doc_lines(kind, body):
    """The text lines of a doc token, without the comment markers."""
    if kind == "doc3":
        return [body[1:] if body.startswith(" ") else body]
    body = body[3:]
    if body.endswith("*/"):
        body = body[:-2]
    out = []
    for raw in body.split("\n"):
        s = raw.strip()
        if s.startswith("*"):
            s = s[1:]
            if s.startswith(" "):
                s = s[1:]
        out.append(s.rstrip())
    while out and not out[0].strip():
        out.pop(0)
    while out and not out[-1].strip():
        out.pop()
    return out


QUALIFIERS = {"const", "unsigned", "signed", "volatile", "struct", "class", "enum"}
COLON = re.compile(r"(?<!:):(?!:)")


def split_typed(rest):
    """'TYPE NAME : meaning' -> (type, name, meaning); the colon split is on the first single colon."""
    m = COLON.search(rest)
    head, meaning = (rest[:m.start()], rest[m.end():].strip()) if m else (rest, None)
    toks = head.split()
    typ = []
    while toks and toks[0] in QUALIFIERS and len(toks) > 1:
        typ.append(toks.pop(0))
    if toks:
        typ.append(toks.pop(0))
    return (" ".join(typ) or None), (" ".join(toks) or None), (meaning or None)


def parse_doc(lines):
    """Normalizes an Eluna or a Doxygen block into {description, params, returns, style}."""
    text = "\n".join(lines)
    doxygen = "@brief" in text
    paras, cur = [], []
    params, returns = [], []
    target = None  # the param/return dict a continuation line extends

    def end_para():
        if cur:
            paras.append(" ".join(cur))
            cur.clear()

    for ln in lines:
        s = ln.strip()
        if not s:
            target = None
            end_para()
            continue
        m = re.match(r"@(\w+)\s*(.*)$", s)
        tag, rest = (m.group(1), m.group(2).strip()) if m else (None, None)
        if tag == "brief":
            target = None
            cur.append(rest)
        elif tag == "param":
            end_para()
            if doxygen:
                pm = re.match(r"(\S+)\s*(.*)$", rest)
                name, meaning = (pm.group(1), pm.group(2).strip()) if pm else (rest, "")
                target = {"type": None, "name": name, "meaning": meaning or None}
            else:
                typ, name, meaning = split_typed(rest)
                if name is None:
                    typ, name = None, typ
                target = {"type": typ, "name": name, "meaning": meaning}
            params.append(target)
        elif tag in ("return", "returns"):
            end_para()
            if doxygen:
                target = {"type": None, "name": None, "meaning": rest or None}
            else:
                typ, name, meaning = split_typed(rest)
                if meaning is None and name and len(name.split()) > 1:
                    name, meaning = None, name
                target = {"type": typ, "name": name, "meaning": meaning}
            returns.append(target)
        elif target is not None:
            target["meaning"] = ((target["meaning"] or "") + " " + s).strip()
        else:
            cur.append(s)
    end_para()
    return {"description": "\n\n".join(paras), "params": params, "returns": returns,
            "style": "doxygen" if doxygen else "eluna"}


# ---------------------------------------------------------------- C++ header parser

def strip_template(s):
    """Drops a leading template <...> prefix (balanced angle brackets)."""
    s = s.strip()
    while s.startswith("template"):
        j = s.find("<")
        if j < 0:
            break
        depth = 0
        for k in range(j, len(s)):
            if s[k] == "<":
                depth += 1
            elif s[k] == ">":
                depth -= 1
                if depth == 0:
                    s = s[k + 1:].strip()
                    break
        else:
            break
    return s


def strip_attributes(s):
    """Drops leading [[attr]] and ALL_CAPS(...) attribute macros such as WXL_DEPRECATED(...)."""
    while True:
        s = s.strip()
        if s.startswith("[["):
            j = s.find("]]")
            s = s[j + 2:] if j >= 0 else s
            continue
        m = re.match(r"[A-Z][A-Z0-9_]+\s*\(", s)
        if not m:
            return s
        depth = 0
        for k in range(m.end() - 1, len(s)):
            if s[k] == "(":
                depth += 1
            elif s[k] == ")":
                depth -= 1
                if depth == 0:
                    s = s[k + 1:]
                    break
        else:
            return s


def strip_decltype(s):
    """Drops a leading decltype(...) return type, whose parens are not a parameter list."""
    s = s.strip()
    while s.startswith("decltype("):
        depth = 0
        for k in range(len("decltype"), len(s)):
            if s[k] == "(":
                depth += 1
            elif s[k] == ")":
                depth -= 1
                if depth == 0:
                    s = s[k + 1:].strip()
                    break
        else:
            return s
    return s


def classify(sig):
    """(kind, name) of a subject signature."""
    core = strip_decltype(strip_attributes(strip_template(sig)))
    if core.startswith("friend"):
        return "friend", None
    m = re.match(r"namespace\s+([\w:]+)", core)
    if m:
        return "namespace", m.group(1)
    m = re.match(r"(class|struct|union)\s+(?:alignas\([^)]*\)\s*)?(\w+)", core)
    if m and "(" not in core[m.end():].split(":")[0]:
        return ("class" if m.group(1) == "class" else "struct"), m.group(2)
    m = re.match(r"enum(?:\s+(?:class|struct))?\s+(\w+)", core)
    if m:
        return "enum", m.group(1)
    m = re.match(r"using\s+(\w+)", core)
    if m:
        return "alias", m.group(1)
    m = re.match(r"typedef\b.*?(\w+)\s*;?$", core)
    if m:
        return "alias", m.group(1)
    p = core.find("(")
    if p >= 0 and "=" not in core[:p].replace("operator=", "").replace("==", "").replace("!=", ""):
        head = core[:p].rstrip()
        # operator() and operator[] carry their own brackets, so the first '(' is part of the name.
        if head.endswith("operator"):
            for sym in ("()", "[]"):
                if core[p:].startswith(sym):
                    return "function", "operator" + sym
        m = re.search(r"(operator\s*(?:[\w:]+(?:\s*[*&])*|[^\w\s(]+))$", head)
        if m:
            return "function", re.sub(r"\s+", " ", m.group(1))
        m = re.search(r"(~?\w+)$", head)
        return "function", (m.group(1) if m else head)
    m = re.match(r"([^=\[{;]*?)(\w+)\s*(?:\[[^\]]*\]\s*)?(?:=|\{|;|$)", core)
    return "field", (m.group(2) if m else core)


def signature_params(sig):
    """{name: type} from the outermost parameter list of a function signature."""
    core = strip_attributes(strip_template(sig))
    p = core.find("(")
    if p < 0:
        return {}
    depth, end = 0, None
    for k in range(p, len(core)):
        if core[k] in "(<[":
            depth += 1
        elif core[k] in ")>]":
            depth -= 1
            if depth == 0 and core[k] == ")":
                end = k
                break
    if end is None:
        return {}
    parts, depth, buf = [], 0, ""
    for ch in core[p + 1:end]:
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(buf)
            buf = ""
        else:
            buf += ch
    parts.append(buf)
    out = {}
    for part in parts:
        part = part.split("=")[0].strip()
        m = re.match(r"^(.*?)(\w+)\s*(\[[^\]]*\])?\s*$", part)
        if m and m.group(1).strip():
            out[m.group(2)] = (m.group(1).strip() + (m.group(3) or "")).strip()
    return out


def drop_init_list(sig):
    """Cuts a constructor's member-initializer list: 'X(void* p) : p_(p)' -> 'X(void* p)'."""
    p = sig.find("(")
    if p < 0:
        return sig
    depth = 0
    for k in range(p, len(sig)):
        if sig[k] == "(":
            depth += 1
        elif sig[k] == ")":
            depth -= 1
            if depth == 0:
                m = COLON.search(sig, k + 1)
                return sig[:m.start()].rstrip() if m else sig
    return sig


class Scope:
    def __init__(self, kind, name, parent, item=None, visible=True, documented=False):
        self.kind = kind
        self.name = name
        self.parent = parent
        self.item = item
        self.visible = visible
        self.documented = documented
        self.access = "private" if kind == "class" else "public"
        self.container = item["children"] if item is not None else (parent.container if parent else None)
        self.qual = (parent.qual if parent else []) + ([name] if name and kind in ("namespace", "class", "struct") else [])

    def emittable(self):
        if not self.visible:
            return False
        if self.kind in ("file", "namespace"):
            return True
        return self.kind in ("class", "struct") and self.access == "public"

    def ancestor_documented(self):
        s = self
        while s:
            if s.documented:
                return True
            s = s.parent
        return False


def tidy(s):
    return re.sub(r"\s+", " ", s).strip()


def parse_header(path):
    """Every doc-commented public declaration of a header, nested under its class or namespace."""
    text = path.read_text(encoding="utf-8", errors="replace")
    rel = path.relative_to(ROOT).as_posix()
    result = {"file": rel, "namespaces": [], "description": "", "banner": banner(text, True), "items": []}
    root = Scope("file", None, None)
    root.container = result["items"]
    scope = root
    pending = None       # (doc dict, line)
    stmt = []            # code since the last ; { }
    sig_start = None     # index into stmt where the pending subject begins
    last_doc3_line = -10

    def make_item(kind, name, sig, doc, line, sc):
        if kind == "function":
            sig = drop_init_list(sig)
        item = {"kind": kind, "name": name, "qname": "::".join(sc.qual + [name]) if name else None,
                "signature": sig, "description": doc["description"], "params": doc["params"],
                "returns": doc["returns"], "file": rel, "line": line}
        if kind == "function" and doc["style"] == "doxygen":
            types = signature_params(sig)
            for p in item["params"]:
                if p["type"] is None and p["name"] in types:
                    p["type"] = types[p["name"]]
        if kind in ("class", "struct", "namespace"):
            item["children"] = []
        return item

    for kind, body, line in lex(text):
        if kind in ("doc", "doc3"):
            lines = doc_lines(kind, body)
            if kind == "doc3" and pending is not None and pending[2] == "doc3" and line == last_doc3_line + 1 \
                    and sig_start is not None and not "".join(stmt[sig_start:]).strip():
                pending = (pending[0] + lines, pending[1], "doc3")
            else:
                pending = (lines, line, kind)
            if kind == "doc3":
                last_doc3_line = line
            sig_start = len(stmt)
            continue
        if kind == "comment":
            if pending is not None and not "".join(stmt[sig_start:]).strip():
                pending = None
            continue
        if kind == "pp":
            if pending is not None:
                m = re.match(r"#\s*define\s+(\w+)(\([^)]*\))?", body)
                if m and scope.emittable():
                    doc = parse_doc(pending[0])
                    item = make_item("macro", m.group(1), tidy(m.group(0)), doc, pending[1], scope)
                    item["qname"] = m.group(1)
                    scope.container.append(item)
                pending = None
            continue
        # code
        for ch in body:
            if ch == "{":
                head = "".join(stmt)
                text_stmt = tidy(head)
                core = strip_attributes(strip_template(text_stmt))
                kind_s = "block"
                name_s = None
                mm = re.match(r"(?:inline\s+)?namespace\b\s*([\w:]*)", core)
                if mm:
                    kind_s, name_s = "namespace", mm.group(1) or None
                else:
                    mm = re.match(r"(class|struct|union)\s+(?:alignas\([^)]*\)\s*)?(\w+)", core)
                    if mm and "(" not in core[mm.end():]:
                        kind_s, name_s = ("class" if mm.group(1) == "class" else "struct"), mm.group(2)
                    elif re.match(r"enum\b", core):
                        kind_s = "enum"
                item = None
                documented = False
                if pending is not None:
                    sig = tidy("".join(stmt[sig_start:]))
                    doc = parse_doc(pending[0])
                    k, nm = classify(sig)
                    if k == "namespace" and not scope.ancestor_documented() and scope.kind in ("file", "namespace"):
                        documented = True
                        if doc["description"]:
                            result["description"] = (result["description"] + "\n\n" + doc["description"]).strip()
                    elif k != "friend" and scope.emittable() and not (k == "namespace" and nm == "detail"):
                        item = make_item(k, nm, sig, doc, pending[1], scope)
                        item["_open_line"] = line
                        scope.container.append(item)
                        documented = k == "namespace"
                    pending = None
                emit_ok = scope.emittable() and kind_s in ("namespace", "class", "struct") and name_s != "detail"
                child_item = item if (item is not None and kind_s in ("namespace", "class", "struct")) else None
                new = Scope(kind_s, name_s, scope, child_item, visible=emit_ok, documented=documented)
                if kind_s == "namespace" and name_s and scope.kind in ("file", "namespace") and name_s not in result["namespaces"] \
                        and name_s.startswith("wxl"):
                    result["namespaces"].append(name_s)
                new.opened_item = item
                scope = new
                stmt = []
                sig_start = None
            elif ch == "}":
                if pending is not None and sig_start is not None and not "".join(stmt[sig_start:]).strip():
                    pending = None
                if scope.parent is not None:
                    it = getattr(scope, "opened_item", None)
                    if it is not None and it.get("_open_line") is not None:
                        it["_close_line"] = line
                    scope = scope.parent
                stmt = []
                sig_start = None
            elif ch == ";":
                if pending is not None:
                    sig = tidy("".join(stmt[sig_start:]))
                    doc = parse_doc(pending[0])
                    k, nm = classify(sig)
                    if sig and k != "friend" and scope.emittable():
                        scope.container.append(make_item(k, nm, sig, doc, pending[1], scope))
                    pending = None
                stmt = []
                sig_start = None
            else:
                stmt.append(ch)
                if ch == ":":
                    s = "".join(stmt).strip()
                    if s in ("public:", "private:", "protected:"):
                        scope.access = s[:-1]
                        stmt = []
                        sig_start = 0 if pending is not None else None
            if ch == "\n":
                line += 1
        if pending is not None and sig_start is None:
            sig_start = 0
    one_liners(result["items"], text.split("\n"))
    return result


def one_liners(items, src):
    """A class, struct, enum or namespace defined on one line shows its whole definition."""
    for it in items:
        ol, cl = it.pop("_open_line", None), it.pop("_close_line", None)
        if ol is not None and ol == cl and it["kind"] in ("class", "struct", "enum", "namespace"):
            full = tidy(src[ol - 1].split("//")[0])
            sig = it["signature"]
            if full.startswith(sig):
                it["signature"] = full
            elif sig.endswith(full.split("{")[0].strip()):
                it["signature"] = sig + full[len(full.split("{")[0].rstrip()):]
        elif ol is not None and cl is not None and it["kind"] in ("enum", "namespace") and cl - ol <= 12:
            body = [ln.split("//")[0].rstrip() for ln in src[ol - 1:cl]]
            if not any(ln.lstrip().startswith("#") for ln in body):
                indent = min(len(ln) - len(ln.lstrip()) for ln in body if ln.strip())
                text = "\n".join(ln[indent:] for ln in body if ln.strip())
                head = it["signature"]
                it["signature"] = text if text.startswith(head) else head + "\n" + text
        if it.get("children"):
            one_liners(it["children"], src)


def banner(text, paragraph):
    """The file's own opening // prose, before the license; the first line or the first paragraph."""
    out = []
    for ln in text.split("\n"):
        s = ln.strip()
        if not s.startswith("//") or s.startswith("// Copyright"):
            break
        s = s[2:].strip()
        if not s:
            if paragraph and out:
                break
            continue
        out.append(s)
        if not paragraph:
            break
    return " ".join(out)


# ---------------------------------------------------------------- .def tables

def parse_table(path, macro):
    """Every MACRO(...) row of a .def table with the doc block above it: (doc, call text, line)."""
    text = path.read_text(encoding="utf-8", errors="replace")
    rows = []
    pending = None
    call_re = re.compile(r"\b" + macro + r"\s*\(")
    for kind, body, line in lex(text):
        if kind in ("doc", "doc3"):
            pending = parse_doc(doc_lines(kind, body))
            continue
        if kind in ("comment", "pp"):
            pending = None
            continue
        pos = 0
        while True:
            m = call_re.search(body, pos)
            if not m:
                if body[pos:].strip():
                    pending = None
                break
            if body[pos:m.start()].strip():
                pending = None
            depth, k = 0, m.end() - 1
            while k < len(body):
                if body[k] == "(":
                    depth += 1
                elif body[k] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            call = body[m.start():k + 1]
            rows.append((pending, call, line + body.count("\n", 0, m.start())))
            pending = None
            pos = k + 1
    return rows, text


def split_args(inner):
    parts, depth, buf = [], 0, ""
    for ch in inner:
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(buf.strip())
            buf = ""
        else:
            buf += ch
    parts.append(buf.strip())
    return parts


EMPTY_DOC = {"description": "", "params": [], "returns": []}


def build_events():
    path = INCLUDE / "events" / "Events.def"
    rows, _ = parse_table(path, "WXL_EVENT")
    header = parse_header(INCLUDE / "Events.hpp")
    structs = {it["name"]: it for it in header["items"] if it["kind"] == "struct"}
    events, used = [], set()
    for doc, call, line in rows:
        name, eid, args = split_args(call[call.index("(") + 1:-1])
        doc = doc or EMPTY_DOC
        st = structs.get(args)
        used.add(args)
        events.append({"name": name, "id": int(eid, 0), "args": args, "description": doc["description"],
                       "params": st["params"] if st else [], "argsDescription": st["description"] if st else "",
                       "argsSignature": st["signature"] if st else None,
                       "call": tidy(call), "file": path.relative_to(ROOT).as_posix(), "line": line})
    others = [it for it in header["items"] if not (it["kind"] == "struct" and it["name"] in used)]
    return events, others, header


def build_scripts(framework):
    classes = {it["name"]: it for it in framework["items"] if it["kind"] == "class"}
    scripts = []
    for t in SCRIPT_TYPES:
        path = INCLUDE / "scripts" / (t + ".def")
        rows, text = parse_table(path, "WXL_SCRIPT_HOOK")
        hooks = []
        for doc, call, line in rows:
            parts = split_args(call[call.index("(") + 1:-1])
            doc = doc or EMPTY_DOC
            hooks.append({"name": parts[0], "params_tuple": tidy(parts[1]) if len(parts) > 1 else "",
                          "description": doc["description"], "params": doc["params"],
                          "returns": doc["returns"], "call": tidy(call),
                          "file": path.relative_to(ROOT).as_posix(), "line": line})
        cls = classes.get(t)
        scripts.append({"name": t, "file": path.relative_to(ROOT).as_posix(), "banner": banner(text, False),
                        "description": cls["description"] if cls else "",
                        "signature": cls["signature"] if cls else None, "hooks": hooks})
    return scripts


def count_items(items):
    return sum(1 + count_items(it.get("children", [])) for it in items)


def main():
    if not INCLUDE.is_dir():
        sys.exit("include/wxl not found under " + str(ROOT))

    events, event_types, _ = build_events()

    framework = parse_header(INCLUDE / "Script.hpp")
    scripts = build_scripts(framework)
    framework_items = [it for it in framework["items"] if it["name"] in FRAMEWORK_NAMES]
    for f in PLUMBING_FILES:
        framework_items.extend(parse_header(INCLUDE / f)["items"])

    names = [p.name for p in sorted((INCLUDE / "objects").glob("*.hpp"))]
    ordered = OBJECT_BASE_FILES + [n for n in names if n not in OBJECT_BASE_FILES]

    objects = []
    for f in ordered:
        h = parse_header(INCLUDE / "objects" / f)
        for it in h["items"]:
            if it["kind"] in ("class", "struct"):
                objects.append(it)

    bindings = []
    for p in sorted((INCLUDE / "game").glob("*.hpp")):
        h = parse_header(p)
        ns = ", ".join(h["namespaces"])
        bindings.append({"file": h["file"], "name": p.name, "namespace": ns,
                         "label": p.stem + (" (" + ns + ")" if ns else ""),
                         "description": h["description"] or h["banner"], "items": h["items"]})

    api = {"events": events, "eventTypes": event_types, "scripts": scripts,
           "framework": {"description":
                             "What an extension is built out of: the script registry, a typed hook, "
                             "the cross-extension services and the per-extension config reader.",
                         "items": framework_items},
           "objects": objects, "bindings": bindings}

    OUT.mkdir(parents=True, exist_ok=True)
    for child in OUT.iterdir():
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()
    (OUT / "data.js").write_text("const WXL_API = " + json.dumps(api, ensure_ascii=False, separators=(",", ":"))
                                 + ";\n", encoding="utf-8")
    for name in ("index.html", "style.css", "app.js"):
        shutil.copyfile(ASSETS / "site" / name, OUT / name)

    print("events: %d, hooks: %s, framework items: %d, objects: %d (%d members), binding files: %d (%d items)" % (
        len(events), ", ".join("%s %d" % (s["name"], len(s["hooks"])) for s in scripts),
        count_items(framework_items), len(objects), count_items(objects) - len(objects),
        len(bindings), sum(count_items(b["items"]) for b in bindings)))
    print("wrote " + str(OUT))


if __name__ == "__main__":
    main()
