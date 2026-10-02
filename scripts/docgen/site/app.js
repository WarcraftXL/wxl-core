// Builds the navigation tree and the pages from WXL_API (data.js); routes on location.hash.
(function () {
  "use strict";

  var api = window.WXL_API || (typeof WXL_API !== "undefined" ? WXL_API : null);
  var nodes = {};          // id -> node
  var activeRow = null;

  // Tailwind utilities, carried next to the semantic class names. style.css gives every
  // semantic class the same look, so the page stands without the CDN; these only add the
  // polish the CDN can express. Sets are kept disjoint: no two of them set one property.
  var TW = {
    side: "flex-none w-[304px] min-w-[240px] flex flex-col bg-side border-r border-line",
    head: "sticky top-0 z-10 px-[14px] pt-[14px] pb-3 bg-side border-b border-line",
    brand: "flex items-center gap-2 m-0 mb-[10px] text-[14px] font-semibold tracking-[0.01em]",
    filter: "w-full px-[10px] py-[7px] text-[13px] text-fg bg-bg border border-line rounded-md " +
      "outline-none transition duration-150 focus:border-accent focus:shadow-[0_0_0_3px_var(--active)]",
    tree: "flex-1 overflow-y-auto overflow-x-hidden px-[6px] pt-2 pb-7",
    treeRoot: "list-none m-0 p-0",
    sub: "list-none m-0 ml-[14px] pl-[6px] border-l border-line",
    row: "flex items-center gap-[6px] px-2 py-[3px] rounded-md text-[13px] cursor-pointer " +
      "overflow-hidden whitespace-nowrap transition-colors duration-100 hover:bg-hover",
    twisty: "flex-none w-[14px] text-center text-[9px] leading-[14px] text-muted select-none",
    label: "overflow-hidden text-ellipsis",
    kind: "ml-auto pl-2 font-mono text-[10.5px] text-muted",
    main: "flex-1 overflow-auto px-10 pt-7 pb-24",
    title: "m-0 mb-[6px] text-[23px] leading-[1.25] font-semibold tracking-[-0.01em] break-words",
    meta: "m-0 mb-[18px] font-mono text-[11.5px] text-muted",
    sec: "mt-[30px] mb-3 pb-[7px] border-b border-line text-[11px] font-bold uppercase tracking-[0.07em] text-muted",
    para: "m-0 mb-3 max-w-[78ch]",
    empty: "text-muted italic",
    badge: "items-center px-[7px] py-px border rounded-full text-[10px] font-bold leading-[1.5] uppercase tracking-[0.06em]",
    chip: "inline-flex mr-2 bg-badge border-line text-muted",
    lead: "flex w-fit mb-2 bg-active border-transparent text-accent",
    sig: "m-0 px-[14px] py-[11px] bg-code border border-line border-l-[3px] border-l-accent rounded-md " +
      "font-mono text-[12.5px] leading-[1.55] overflow-x-auto whitespace-pre-wrap break-words",
    icode: "px-[5px] py-px bg-code rounded font-mono text-[0.92em]",
    ccode: "font-mono text-[12.5px]",
    tbl: "w-full max-w-[1040px] text-[13px] border border-line rounded-lg border-separate border-spacing-0",
    th: "px-3 py-[7px] text-left align-bottom border-b border-line text-[11px] font-semibold uppercase tracking-[0.06em] text-muted",
    td: "px-3 py-2 text-left align-top border-b border-line",
    nowrap: "whitespace-nowrap",
    first: "w-[1%] whitespace-nowrap font-mono",
    olink: "font-medium"
  };

  function cls() {
    return Array.prototype.join.call(arguments, " ");
  }

  function esc(s) {
    return String(s == null ? "" : s)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }

  // Prose: paragraphs on blank lines, @p name and `name` as inline code.
  function prose(text) {
    var ic = '<code class="icode ' + TW.icode + '">';
    if (!text) return '<p class="para empty ' + cls(TW.para, TW.empty) + '">No description.</p>';
    return text.split(/\n\s*\n/).map(function (p) {
      var h = esc(p).replace(/@p\s+([\w:.]+)/g, ic + "$1</code>").replace(/`([^`]+)`/g, ic + "$1</code>");
      return '<p class="para ' + TW.para + '">' + h + "</p>";
    }).join("");
  }

  function firstSentence(text) {
    if (!text) return "";
    var t = text.split(/\n\s*\n/)[0];
    var m = t.match(/^(.+?[.;:])(\s|$)/);
    t = m ? m[1] : t;
    return t.length > 160 ? t.slice(0, 157) + "..." : t;
  }

  function code(s) {
    return '<pre class="sig ' + TW.sig + '"><code class="scode">' + esc(s) + "</code></pre>";
  }

  function badge(text, lead) {
    return '<span class="badge' + (lead ? " badge-lead" : "") + " " +
      cls(TW.badge, lead ? TW.lead : TW.chip) + '">' + esc(text) + "</span>";
  }

  function section(title) {
    return '<h3 class="sec ' + TW.sec + '">' + title + "</h3>";
  }

  function paramTable(list, withName) {
    var cell = function (klass, inner) { return '<td class="tbl-c ' + klass + '">' + inner + "</td>"; };
    var mono = function (s) { return '<code class="ccode ' + TW.ccode + '">' + esc(s) + "</code>"; };
    var rows = list.map(function (p) {
      return "<tr>" +
        cell(cls("t", TW.td, TW.nowrap), mono(p.type || "")) +
        (withName ? cell(cls("n", TW.td, TW.nowrap), mono(p.name || "")) : "") +
        cell(TW.td, esc(p.meaning || "")) + "</tr>";
    }).join("");
    var th = function (t) { return '<th class="tbl-h ' + TW.th + '">' + t + "</th>"; };
    return '<table class="tbl ' + TW.tbl + '"><thead><tr>' + th("Type") + (withName ? th("Name") : "") +
      th("Meaning") + "</tr></thead><tbody>" + rows + "</tbody></table>";
  }

  function paramsBlock(params, returns, title) {
    var h = "";
    if (params && params.length) h += section(title || "Parameters") + paramTable(params, true);
    if (returns && returns.length) {
      var named = returns.some(function (r) { return r.name; });
      h += section("Returns") + paramTable(returns, named);
    }
    return h;
  }

  function link(node) {
    return '<a class="olink ' + TW.olink + '" href="#' + encodeURIComponent(node.id) + '">' + esc(node.label) + "</a>";
  }

  function overview(node) {
    if (!node.children.length) {
      return '<p class="para empty ' + cls(TW.para, TW.empty) + '">Nothing documented here.</p>';
    }
    var rows = node.children.map(function (c) {
      return '<tr><td class="tbl-c ' + cls(TW.td, TW.first) + '">' + link(c) + "</td>" +
        '<td class="tbl-c ' + TW.td + '">' + (c.kind ? badge(c.kind) : "") + esc(firstSentence(c.desc)) + "</td></tr>";
    }).join("");
    return '<table class="tbl overview ' + TW.tbl + '"><tbody>' + rows + "</tbody></table>";
  }

  function heading(kind, text) {
    return '<h2 class="title ' + TW.title + '">' + badge(kind, true) + esc(text) + "</h2>";
  }

  function plain(text) {
    return '<h2 class="title ' + TW.title + '">' + esc(text) + "</h2>";
  }

  function where(file, line) {
    return file ? '<div class="meta ' + TW.meta + '">' + esc(file) + (line ? ":" + line : "") + "</div>" : "";
  }

  // ---- node construction

  function add(parent, node) {
    node.children = node.children || [];
    node.parent = parent;
    nodes[node.id] = node;
    if (parent) parent.children.push(node);
    return node;
  }

  function itemNode(parent, it, prefix) {
    var label = it.name || it.signature;
    var id = prefix + "/" + (it.qname || label);
    if (nodes[id]) id += "@" + it.line;  // overloads share a name
    var node = add(parent, {
      id: id, label: label, kind: it.kind, desc: it.description, item: it,
      render: function () { return renderItem(node); }
    });
    (it.children || []).forEach(function (c) { itemNode(node, c, prefix); });
    return node;
  }

  function renderItem(node) {
    var it = node.item;
    var h = heading(it.kind, it.qname || it.name) +
      where(it.file, it.line) + '<div class="desc">' + prose(it.description) + "</div>";
    if (it.signature) h += section("Declaration") + code(it.signature);
    h += paramsBlock(it.params, it.returns);
    if (node.children.length) h += section("Members") + overview(node);
    return h;
  }

  function build() {
    var roots = [];

    // Events
    var ev = add(null, { id: "events", label: "Events", group: true,
      desc: "The event catalogue (include/wxl/events/Events.def): one row per event; the id is the ABI.",
      render: function () {
        return plain("Events") + '<div class="desc">' + prose(ev.desc) + "</div>" +
          section("All events") + overview(ev);
      } });
    roots.push(ev);
    api.events.forEach(function (e) {
      add(ev, { id: "event/" + e.name, label: e.name, kind: "#" + e.id, desc: e.description,
        render: function () {
          var h = heading("event " + e.id, e.name) + where(e.file, e.line) +
            '<div class="desc">' + prose(e.description) + "</div>";
          h += section("Table row") + code(e.call);
          h += section('Args: <code class="icode ' + TW.icode + ' normal-case tracking-normal">' +
            esc(e.args) + "</code>");
          if (e.argsDescription) h += '<div class="desc">' + prose(e.argsDescription) + "</div>";
          if (e.argsSignature) h += code(e.argsSignature);
          if (e.params.length) h += paramsBlock(e.params, null, "Fields");
          var hooks = [];
          api.scripts.forEach(function (s) {
            s.hooks.forEach(function (k) { if (k.name === e.name) hooks.push(nodes["hook/" + s.name + "/" + k.name]); });
          });
          if (hooks.length) h += section("Script hook") + '<p class="para ' + TW.para + '">' + hooks.map(function (k) {
            return link(k) + " in " + link(k.parent);
          }).join(", ") + "</p>";
          return h;
        } });
    });
    if (api.eventTypes && api.eventTypes.length) {
      var et = add(ev, { id: "events/types", label: "Event bus types", group: true,
        desc: "The enum, handler and helpers of include/wxl/Events.hpp.",
        render: function () { return plain("Event bus types") + '<div class="desc">' + prose(et.desc) + "</div>" + overview(et); } });
      api.eventTypes.forEach(function (it) { itemNode(et, it, "events/types"); });
    }

    // Script hooks
    var sh = add(null, { id: "hooks", label: "Script Hooks", group: true,
      desc: "Derive a script type, override the hooks you need, register it with ScriptMgr::Add.",
      render: function () { return plain("Script Hooks") + '<div class="desc">' + prose(sh.desc) + "</div>" + overview(sh); } });
    roots.push(sh);
    api.scripts.forEach(function (s) {
      var sn = add(sh, { id: "hooks/" + s.name, label: s.name, group: true, desc: s.banner || s.description,
        render: function () {
          var h = heading("script type", s.name) + where(s.file) +
            '<div class="desc">' + prose(s.banner) + (s.description && s.description !== s.banner ? prose(s.description) : "") + "</div>";
          if (s.signature) h += section("Declaration") + code(s.signature);
          return h + section("Hooks") + overview(sn);
        } });
      s.hooks.forEach(function (k) {
        add(sn, { id: "hook/" + s.name + "/" + k.name, label: k.name, kind: "hook", desc: k.description,
          render: function () {
            var h = heading("hook", s.name + "::" + k.name) + where(k.file, k.line) +
              '<div class="desc">' + prose(k.description) + "</div>";
            h += section("Override") + code("void " + k.name + k.params_tuple + " override");
            h += section("Table row") + code(k.call);
            h += paramsBlock(k.params, k.returns);
            if (nodes["event/" + k.name]) {
              h += section("Event") + '<p class="para ' + TW.para + '">' + link(nodes["event/" + k.name]) + "</p>";
            }
            return h;
          } });
      });
    });

    // Extension API: the script registry plus the hook, service and config headers. Each item
    // carries its own file, so the group itself names none.
    var fw = add(null, { id: "framework", label: "Extension API", group: true, desc: api.framework.description,
      render: function () { return plain("Extension API") +
        '<div class="desc">' + prose(fw.desc) + "</div>" + overview(fw); } });
    roots.push(fw);
    api.framework.items.forEach(function (it) { itemNode(fw, it, "framework"); });

    // Objects
    var ob = add(null, { id: "objects", label: "Objects", group: true,
      desc: "Handles over resident client objects: Object, Unit : Object, Player : Unit.",
      render: function () { return plain("Objects") + '<div class="desc">' + prose(ob.desc) + "</div>" + overview(ob); } });
    roots.push(ob);
    api.objects.forEach(function (it) { itemNode(ob, it, "objects"); });

    // Bindings
    var bd = add(null, { id: "bindings", label: "Bindings", group: true,
      desc: "Typed engine bindings, one header per area under include/wxl/game/.",
      render: function () { return plain("Bindings") + '<div class="desc">' + prose(bd.desc) + "</div>" + overview(bd); } });
    roots.push(bd);
    api.bindings.forEach(function (b) {
      var bn = add(bd, { id: "bindings/" + b.name, label: b.label, group: true, desc: b.description,
        render: function () {
          return plain(b.name) + '<div class="meta ' + TW.meta + '">' + esc(b.file) +
            (b.namespace ? " &middot; <code class=\"ccode " + TW.ccode + "\">" + esc(b.namespace) + "</code>" : "") + "</div>" +
            '<div class="desc">' + prose(b.description) + "</div>" + section("Contents") + overview(bn);
        } });
      b.items.forEach(function (it) { itemNode(bn, it, "bindings/" + b.name); });
    });

    return roots;
  }

  // ---- sidebar

  function renderTree(list, ul) {
    list.forEach(function (n) {
      var li = document.createElement("li");
      var row = document.createElement("div");
      var hasKids = n.children.length > 0;
      row.className = "row" + (n.group ? " group" : "") + " " + TW.row;
      row.innerHTML = '<span class="twisty ' + TW.twisty + '">' + (hasKids ? "▾" : "") +
        '</span><span class="label ' + TW.label + '">' + esc(n.label) + "</span>" +
        (n.kind && !n.group ? '<span class="kind ' + TW.kind + '">' + esc(n.kind) + "</span>" : "");
      row.title = n.label;
      li.appendChild(row);
      n.li = li;
      n.row = row;
      // A group matches on its label only, so a word in its intro does not reveal every child.
      n.search = (n.group ? n.label : n.label + " " + (n.item && n.item.qname ? n.item.qname : "") + " " + (n.desc || "")).toLowerCase();
      if (hasKids) {
        var sub = document.createElement("ul");
        sub.className = TW.sub;
        renderTree(n.children, sub);
        li.appendChild(sub);
        if (!n.group || n.parent) li.classList.add("closed");
        row.querySelector(".twisty").addEventListener("click", function (e) {
          e.stopPropagation();
          toggle(n);
        });
        if (li.classList.contains("closed")) row.querySelector(".twisty").textContent = "▸";
      }
      row.addEventListener("click", function () {
        if (location.hash === "#" + encodeURIComponent(n.id)) show(n.id);
        else location.hash = encodeURIComponent(n.id);
        if (hasKids && n.li.classList.contains("closed")) toggle(n);
      });
      ul.appendChild(li);
    });
  }

  function toggle(n, open) {
    var closed = open === undefined ? !n.li.classList.contains("closed") : !open;
    n.li.classList.toggle("closed", closed);
    var tw = n.row.querySelector(".twisty");
    if (n.children.length) tw.textContent = closed ? "▸" : "▾";
  }

  // Shows a node when it or a descendant matches; opens the groups on the way to a match.
  function filter(list, q) {
    var any = false;
    list.forEach(function (n) {
      var self = !q || n.search.indexOf(q) >= 0;
      var kids = n.children.length ? filter(n.children, self ? "" : q) : false;
      var visible = self || kids;
      n.li.classList.toggle("hidden", !visible);
      if (q && kids) toggle(n, true);
      any = any || visible;
    });
    return any;
  }

  function show(id) {
    var n = nodes[id] || nodes.events;
    document.getElementById("content").innerHTML = n.render();
    document.getElementById("content").scrollTop = 0;
    if (activeRow) activeRow.classList.remove("active");
    activeRow = n.row;
    activeRow.classList.add("active");
    for (var p = n.parent; p; p = p.parent) if (p.li.classList.contains("closed")) toggle(p, true);
    document.title = n.label + " - WarcraftXL API";
  }

  function route() {
    var id = decodeURIComponent(location.hash.slice(1));
    show(id || "events");
  }

  function start() {
    var app = document.getElementById("app");
    if (!api) {
      app.innerHTML = '<main class="main ' + TW.main + '">' + plain("data.js is missing") +
        '<p class="para ' + TW.para + '">Run scripts/docgen/generate.py.</p></main>';
      return;
    }
    app.innerHTML = '<nav class="side ' + TW.side + '"><div class="side-head ' + TW.head + '">' +
      '<h1 class="brand ' + TW.brand + '">WarcraftXL API</h1>' +
      '<input id="filter" class="filter ' + TW.filter + '" type="search" ' +
      'placeholder="Filter by name or text" autocomplete="off"></div>' +
      '<div class="tree ' + TW.tree + '"><ul id="tree" class="' + TW.treeRoot + '"></ul></div></nav>' +
      '<main class="main ' + TW.main + '" id="content"></main>';
    var roots = build();
    renderTree(roots, document.getElementById("tree"));
    var input = document.getElementById("filter");
    input.addEventListener("input", function () { filter(roots, input.value.trim().toLowerCase()); });
    window.addEventListener("hashchange", route);
    route();
  }

  start();
})();
