(function () {
  'use strict';
  const nodes = Array.from(document.querySelectorAll('.enhanced-math'));
  let index = 0;
  window.enhancedMathState = {total: nodes.length, complete: 0, errors: 0};
  function batch() {
    const start = performance.now();
    do {
      const node = nodes[index++];
      const raw = node.textContent;
      const display = node.dataset.display === '1';
      const count = display ? 2 : 1;
      try {
        if (typeof katex === 'undefined') throw new Error('Offline math component is unavailable.');
        katex.render(raw.slice(count, -count), node, {
          displayMode: display, output: 'htmlAndMathml', trust: false,
          throwOnError: true, maxExpand: 1000, maxSize: 20, strict: 'warn', macros: {}
        });
        node.dataset.rendered = '1';
      } catch (error) {
        node.textContent = raw;
        node.classList.add('enhanced-math-error');
        node.title = 'Equation could not be rendered: ' + String(error.message || error);
        node.setAttribute('aria-label', node.title + '. ' + raw);
        window.enhancedMathState.errors++;
      }
      window.enhancedMathState.complete++;
    } while (index < nodes.length && performance.now() - start < 8);
    if (index < nodes.length) setTimeout(batch, 0);
    else document.documentElement.dataset.mathReady = '1';
  }
  if (nodes.length) batch();
  else document.documentElement.dataset.mathReady = '1';
})();
