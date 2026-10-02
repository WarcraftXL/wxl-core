// Builds the navigation tree and the pages from WXL_API (data.js); routes on location.hash.
(function () {
  "use strict";

  var api = window.WXL_API || (typeof WXL_API !== "undefined" ? WXL_API : null);
  var nodes = {};          // id -> node
  var activeRow = null;

  function esc(s) {
    return String(s == null ? "" : s)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }

  // Prose: paragraphs on blank lines, @p name and `name` as inline code.
  function prose(text) {
    if (!text) return '<p class="empty">No description.</p>';
    return text.split(/\n\s*\n/).map(function (p) {
      var h = esc(p).replace(/@p\s+([\w:.]+)/g, "<code>$1</code>").replace(/`([^`]+)`/g, "<code>$1</code>");
      return "<p>" + h + "</p>";
    }).join("");
  }

  function firstSentence(text) {
    if (!text) return "";
    var t = text.split(/\n\s*\n/)[0];
    var m = t.match(/^(.+?[.;:])(\s|$)/);
    t = m ? m[1] : t;
    return t.length > 160 ? t.slice(0, 157) + "..." : t;
  }

  function code(s) { return '<pre class="sig"><code>' + esc(s) + "</code></pre>"; }

  function paramTable(list, withName) {
    var rows = list.map(function (p) {
      return "<tr><td class=\"t\"><code>" + esc(p.type || "") + "</code></td>" +
        (withName ? "<td class=\"n\"><code>" + esc(p.name || "") + "</code></td>" : "") +
        "<td>" + esc(p.meaning || "") + "</td></tr>";
    }).join("");
    return "<table><thead><tr><th>Type</th>" + (withName ? "<th>Name</th>" : "") +
      "<th>Meaning</th></tr></thead><tbody>" + rows + "</tbody></table>";
  }

  function paramsBlock(params, returns, title) {
    var h = "";
    if (params && params.length) h += "<h3>" + (title || "Parameters") + "</h3>" + paramTable(params, true);
    if (returns && returns.length) {
      var named = returns.some(function (r) { return r.name; });
      h += "<h3>Returns</h3>" + paramTable(returns, named);
    }
    return h;
  }

  function link(node) { return '<a href="#' + encodeURIComponent(node.id) + '">' + esc(node.label) + "</a>"; }

  function overview(node) {
    if (!node.children.length) return '<p class="empty">Nothing documented here.</p>';
    var rows = node.children.map(function (c) {
      return "<tr><td>" + link(c) + "</td><td>" + (c.kind ? '<span class="badge">' + esc(c.kind) + "</span>" : "") +
        esc(firstSentence(c.desc)) + "</td></tr>";
    }).join("");
    return '<table class="overview"><tbody>' + rows + "</tbody></table>";
  }

  function where(file, line) {
    return file ? '<div class="meta">' + esc(file) + (line ? ":" + line : "") + "</div>" : "";
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
    var h = '<h2><span class="badge">' + esc(it.kind) + "</span>" + esc(it.qname || it.name) + "</h2>" +
      where(it.file, it.line) + '<div class="desc">' + prose(it.description) + "</div>";
    if (it.signature) h += "<h3>Declaration</h3>" + code(it.signature);
    h += paramsBlock(it.params, it.returns);
    if (node.children.length) h += "<h3>Members</h3>" + overview(node);
    return h;
  }

  function build() {
    var roots = [];

    // Events
    var ev = add(null, { id: "events", label: "Events", group: true,
      desc: "The event catalogue (include/wxl/events/Events.def): one row per event; the id is the ABI.",
      render: function () {
        return "<h2>Events</h2>" + '<div class="desc">' + prose(ev.desc) + "</div><h3>All events</h3>" + overview(ev);
      } });
    roots.push(ev);
    api.events.forEach(function (e) {
      add(ev, { id: "event/" + e.name, label: e.name, kind: "#" + e.id, desc: e.description,
        render: function () {
          var h = '<h2><span class="badge">event ' + e.id + "</span>" + esc(e.name) + "</h2>" + where(e.file, e.line) +
            '<div class="desc">' + prose(e.description) + "</div>";
          h += "<h3>Table row</h3>" + code(e.call);
          h += "<h3>Args: <code>" + esc(e.args) + "</code></h3>";
          if (e.argsDescription) h += '<div class="desc">' + prose(e.argsDescription) + "</div>";
          if (e.argsSignature) h += code(e.argsSignature);
          if (e.params.length) h += paramsBlock(e.params, null, "Fields");
          var hooks = [];
          api.scripts.forEach(function (s) {
            s.hooks.forEach(function (k) { if (k.name === e.name) hooks.push(nodes["hook/" + s.name + "/" + k.name]); });
          });
          if (hooks.length) h += "<h3>Script hook</h3><p>" + hooks.map(function (k) {
            return link(k) + " in " + link(k.parent);
          }).join(", ") + "</p>";
          return h;
        } });
    });
    if (api.eventTypes && api.eventTypes.length) {
      var et = add(ev, { id: "events/types", label: "Event bus types", group: true,
        desc: "The enum, handler and helpers of include/wxl/Events.hpp.",
        render: function () { return "<h2>Event bus types</h2>" + '<div class="desc">' + prose(et.desc) + "</div>" + overview(et); } });
      api.eventTypes.forEach(function (it) { itemNode(et, it, "events/types"); });
    }

    // Script hooks
    var sh = add(null, { id: "hooks", label: "Script Hooks", group: true,
      desc: "Derive a script type, override the hooks you need, register it with ScriptMgr::Add.",
      render: function () { return "<h2>Script Hooks</h2>" + '<div class="desc">' + prose(sh.desc) + "</div>" + overview(sh); } });
    roots.push(sh);
    api.scripts.forEach(function (s) {
      var sn = add(sh, { id: "hooks/" + s.name, label: s.name, group: true, desc: s.banner || s.description,
        render: function () {
          var h = '<h2><span class="badge">script type</span>' + esc(s.name) + "</h2>" + where(s.file) +
            '<div class="desc">' + prose(s.banner) + (s.description && s.description !== s.banner ? prose(s.description) : "") + "</div>";
          if (s.signature) h += "<h3>Declaration</h3>" + code(s.signature);
          return h + "<h3>Hooks</h3>" + overview(sn);
        } });
      s.hooks.forEach(function (k) {
        add(sn, { id: "hook/" + s.name + "/" + k.name, label: k.name, kind: "hook", desc: k.description,
          render: function () {
            var h = '<h2><span class="badge">hook</span>' + esc(s.name + "::" + k.name) + "</h2>" + where(k.file, k.line) +
              '<div class="desc">' + prose(k.description) + "</div>";
            h += "<h3>Override</h3>" + code("void " + k.name + k.params_tuple + " override");
            h += "<h3>Table row</h3>" + code(k.call);
            h += paramsBlock(k.params, k.returns);
            if (nodes["event/" + k.name]) h += "<h3>Event</h3><p>" + link(nodes["event/" + k.name]) + "</p>";
            return h;
          } });
      });
    });

    // Script framework
    var fw = add(null, { id: "framework", label: "Script Framework", group: true, desc: api.framework.description,
      render: function () { return "<h2>Script Framework</h2>" + where("include/wxl/Script.hpp") +
        '<div class="desc">' + prose(fw.desc) + "</div>" + overview(fw); } });
    roots.push(fw);
    api.framework.items.forEach(function (it) { itemNode(fw, it, "framework"); });

    // Objects
    var ob = add(null, { id: "objects", label: "Objects", group: true,
      desc: "Handles over resident client objects: Object, Unit : Object, Player : Unit.",
      render: function () { return "<h2>Objects</h2>" + '<div class="desc">' + prose(ob.desc) + "</div>" + overview(ob); } });
    roots.push(ob);
    api.objects.forEach(function (it) { itemNode(ob, it, "objects"); });

    // Bindings
    var bd = add(null, { id: "bindings", label: "Bindings", group: true,
      desc: "Typed engine bindings, one header per area under include/wxl/game/.",
      render: function () { return "<h2>Bindings</h2>" + '<div class="desc">' + prose(bd.desc) + "</div>" + overview(bd); } });
    roots.push(bd);
    api.bindings.forEach(function (b) {
      var bn = add(bd, { id: "bindings/" + b.name, label: b.label, group: true, desc: b.description,
        render: function () {
          return "<h2>" + esc(b.name) + "</h2>" + '<div class="meta">' + esc(b.file) +
            (b.namespace ? " &middot; <code>" + esc(b.namespace) + "</code>" : "") + "</div>" +
            '<div class="desc">' + prose(b.description) + "</div><h3>Contents</h3>" + overview(bn);
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
      row.className = "row" + (n.group ? " group" : "");
      row.innerHTML = '<span class="twisty">' + (hasKids ? "▾" : "") + '</span><span class="label">' +
        esc(n.label) + "</span>" + (n.kind && !n.group ? '<span class="kind">' + esc(n.kind) + "</span>" : "");
      row.title = n.label;
      li.appendChild(row);
      n.li = li;
      n.row = row;
      // A group matches on its label only, so a word in its intro does not reveal every child.
      n.search = (n.group ? n.label : n.label + " " + (n.item && n.item.qname ? n.item.qname : "") + " " + (n.desc || "")).toLowerCase();
      if (hasKids) {
        var sub = document.createElement("ul");
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
      app.innerHTML = '<div class="main"><h2>data.js is missing</h2><p>Run scripts/docgen/generate.py.</p></div>';
      return;
    }
    app.innerHTML = '<nav class="side"><div class="side-head"><h1>WarcraftXL API</h1>' +
      '<input id="filter" type="search" placeholder="Filter by name or text" autocomplete="off"></div>' +
      '<div class="tree"><ul id="tree"></ul></div></nav><main class="main" id="content"></main>';
    var roots = build();
    renderTree(roots, document.getElementById("tree"));
    var input = document.getElementById("filter");
    input.addEventListener("input", function () { filter(roots, input.value.trim().toLowerCase()); });
    window.addEventListener("hashchange", route);
    route();
  }

  start();
})();
