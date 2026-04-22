// Auto-expand the navtree entry for the current page so its sub-sections are
// immediately visible. Doxygen's built-in navtree.js only expands the path
// TO the selected node on certain landing pages (index / pages / search),
// so for other pages (e.g. user-guide.html) we trigger the expand toggle
// ourselves once the selection has been resolved.
(function () {
  function tryExpand(attempts) {
    var selected = document.getElementById("selected");
    if (!selected) {
      if (attempts > 0) {
        setTimeout(function () { tryExpand(attempts - 1); }, 100);
      }
      return;
    }
    var li = selected.closest("li");
    if (!li) return;
    // If doxygen has already populated the child list (i.e. it decided to
    // auto-expand this node, which it does on mainpage / pages / search),
    // leave it alone — clicking the toggle would collapse it.
    if (li.querySelector(":scope > ul.children_ul")) return;
    // First <a> child of .item is the expand toggle (only present when the
    // node has children). The label link lives inside a nested <span>.
    var toggle = li.querySelector(":scope > .item > a");
    if (toggle) toggle.click();
  }
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", function () {
      tryExpand(50);
    });
  } else {
    tryExpand(50);
  }
})();
