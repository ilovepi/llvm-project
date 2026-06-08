document.addEventListener("DOMContentLoaded", function() {
  const resizer = document.getElementById('resizer');
  const sidebar = document.querySelector('.sidebar');

  let isResizing = false;
  resizer.addEventListener('mousedown', (e) => { isResizing = true; });

  document.addEventListener('mousemove', (e) => {
    if (!isResizing)
      return;
    const newWidth = e.clientX;
    if (newWidth > 100 && newWidth < window.innerWidth - 100) {
      sidebar.style.width = `${newWidth}px`;
    }
  });

  document.addEventListener('mouseup', () => { isResizing = false; });

  // Links can't be baked into the prototype HTML. hljs rewrites every `pre
  // code` block on load and would drop any anchor in it, so highlight first and
  // then re-apply links over the highlighted text from the window.ClangDocXRef
  // map.
  const xref = window.ClangDocXRef || {};
  // Longest names first so e.g. "const Foo &" wins over "Foo".
  const xrefKeys =
      Object.keys(xref).filter(Boolean).sort((a, b) => b.length - a.length);
  const isIdentChar = (c) => /[A-Za-z0-9_]/.test(c);

  // Wrap each documented type in this signature with a link. Walk the block's
  // text nodes once, find the non-overlapping matches, and wrap them from last
  // to first so the earlier offsets stay valid. A type's text can span several
  // highlight spans, so each match is extracted and re-inserted wrapped.
  function linkifyTypes(codeEl) {
    const text = codeEl.textContent;
    const present = xrefKeys.filter((k) => text.includes(k));
    if (!present.length)
      return;

    const nodes = [];
    let full = "";
    const walker = document.createTreeWalker(codeEl, NodeFilter.SHOW_TEXT);
    while (walker.nextNode()) {
      nodes.push({node : walker.currentNode, start : full.length});
      full += walker.currentNode.nodeValue;
    }

    const matches = [];
    for (const key of present) {
      for (let i = full.indexOf(key); i !== -1;
           i = full.indexOf(key, i + key.length)) {
        const before = i > 0 ? full[i - 1] : " ";
        const after = i + key.length < full.length ? full[i + key.length] : " ";
        if (!isIdentChar(before) && !isIdentChar(after))
          matches.push({start : i, end : i + key.length, href : xref[key]});
      }
    }
    if (!matches.length)
      return;

    // Keep the earliest, longest non-overlapping matches.
    matches.sort((a, b) => a.start - b.start || b.end - a.end);
    const chosen = [];
    let lastEnd = -1;
    for (const m of matches)
      if (m.start >= lastEnd) {
        chosen.push(m);
        lastEnd = m.end;
      }

    const locate = (pos) => {
      for (const n of nodes)
        if (pos <= n.start + n.node.nodeValue.length)
          return {node : n.node, offset : pos - n.start};
      return null;
    };
    for (let k = chosen.length - 1; k >= 0; k--) {
      const s = locate(chosen[k].start), e = locate(chosen[k].end);
      if (!s || !e)
        continue;
      const range = document.createRange();
      range.setStart(s.node, s.offset);
      range.setEnd(e.node, e.offset);
      const anchor = document.createElement("a");
      anchor.setAttribute("href", chosen[k].href);
      anchor.className = "type-link";
      anchor.appendChild(range.extractContents());
      range.insertNode(anchor);
    }
  }

  document.querySelectorAll('pre code').forEach((el) => {
    // One block must never break highlighting or navigation on the rest of the
    // page, so isolate any failure here.
    try {
      hljs.highlightElement(el);
      el.classList.remove("hljs");
      linkifyTypes(el);
    } catch (e) { /* ignore */
    }
  });

  document.querySelectorAll('.sidebar-item-container').forEach(item => {
    item.addEventListener('click', function() {
      const anchor = item.getElementsByTagName("a");
      window.location.hash = anchor[0].getAttribute('href');
    });
  });
})
