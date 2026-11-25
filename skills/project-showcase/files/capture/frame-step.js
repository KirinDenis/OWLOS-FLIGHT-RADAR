/* frame-step.js: film a page's animation frame by frame, also in a browser pane the agent cannot see (hidden or in
 * the background), where requestAnimationFrame runs about twice a second and CSS animations stand frozen.
 *
 * Paste it into the page (an agent's javascript tool, or the console) BEFORE the animated part is first seen: a WebGL
 * canvas must be made with preserveDrawingBuffer, or reading it back gives a blank picture.
 *
 *   await window.frameStep.ready();                // after the page made its canvases
 *   frameStep.step(16);                            // one frame, 16 ms of the page's time
 *   await frameStep.grab('.win.history', 'h-0001.png', 'http://localhost:8798/');
 *
 * Time is stepped by hand and never falls behind real time: __T = max(__T + ms, performance.now()). A page that
 * keeps its own clock from performance.now() and one that uses the rAF stamp then agree, and the page's own
 * "dt = (now - last)" never comes out negative. (It once did: a frame stamped before the loop woke gave a negative
 * dt and the 3D view went NaN. The view now clamps dt at 0 too.)
 *
 * The picture is the canvases drawn where they lie, then a foreignObject clone of the element's HTML over them with
 * the page's own CSS, the canvases hidden in the clone. It is posted to a local sink (files/capture/sink.ps1) that
 * saves it under the name given. Measured: about 0.4 s a frame for a 1700x900 window with a WebGL canvas. */
(() => {
  const orig = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (type, attrs) {
    if (type === 'webgl2' || type === 'webgl') {
      attrs = Object.assign({}, attrs, { preserveDrawingBuffer: true });
    }
    return orig.call(this, type, attrs);
  };

  let queue = [];
  let T = performance.now();
  window.requestAnimationFrame = (cb) => {
    queue.push(cb);
    return queue.length;
  };
  window.cancelAnimationFrame = () => {};

  function step(ms) {
    T = Math.max(T + ms, performance.now());
    const q = queue;
    queue = [];
    for (const cb of q) {
      try {
        cb(T);
      } catch (e) {
        console.error(e);
      }
    }
  }

  const css = () => [...document.querySelectorAll('style')].map((s) => s.textContent).join('\n').replace(/</g, '&lt;');

  async function grab(selector, name, sink, background = '#03081a') {
    for (const a of document.getAnimations()) {       // the hidden pane's animation timeline is frozen: finish them
      try {
        a.finish();
      } catch (e) {
        // an animation that cannot finish (infinite) stays as it is
      }
    }
    const el = document.querySelector(selector);
    const r = el.getBoundingClientRect();
    const W = Math.round(r.width);
    const H = Math.round(r.height);
    const c = document.createElement('canvas');
    c.width = W;
    c.height = H;
    const g = c.getContext('2d');
    g.fillStyle = background;
    g.fillRect(0, 0, W, H);
    for (const cv of el.querySelectorAll('canvas')) {
      const q = cv.getBoundingClientRect();
      if (q.width && cv.offsetParent) {
        g.drawImage(cv, q.left - r.left, q.top - r.top, q.width, q.height);
      }
    }
    const body = new XMLSerializer().serializeToString(el.cloneNode(true));
    // the clone stands at 0,0 at the element's size; no animations, no blur behind glass (foreignObject cannot)
    const fix = `*{animation:none !important;transition:none !important}
      ${selector} { position: static !important; transform: none !important; left: auto !important; top: auto !important;
        width: ${W}px !important; height: ${H}px !important; }
      *::after { -webkit-backdrop-filter: none !important; backdrop-filter: none !important; }
      canvas { visibility: hidden; }`;
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}"><foreignObject width="100%" height="100%">`
      + `<div xmlns="http://www.w3.org/1999/xhtml" style="width:${W}px;height:${H}px">`
      + `<style>${css()} ${fix}</style>${body}</div></foreignObject></svg>`;
    const img = new Image();
    img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
    await img.decode();
    g.drawImage(img, 0, 0);
    const blob = await new Promise((res) => c.toBlob(res, 'image/png'));
    await fetch(sink + name, { method: 'POST', body: blob, mode: 'no-cors' });
    return [W, H];
  }

  // a whole run in the background: the agent's tool call may time out, the loop goes on and the files keep coming
  function film(selector, prefix, frames, msPerFrame, sink) {
    return (async () => {
      for (let i = 0; i < frames; i++) {
        step(msPerFrame);
        await grab(selector, prefix + String(i).padStart(4, '0') + '.png', sink);
      }
      return 'done';
    })();
  }

  window.frameStep = { step, grab, film, ready: () => new Promise((r) => setTimeout(r, 1200)) };
})();
