// Talks to the C++ side of the plug-in. Inside the plug-in, JUCE gives us window.__JUCE__.backend;
// opened in an ordinary browser (for design work) everything still runs, with nothing on the other end.
'use strict';

const Bridge = (() => {
  const juce = window.__JUCE__;
  const inHost = !!(juce && juce.backend);
  const listeners = {};

  if (inHost) {
    juce.backend.addEventListener('cc', (msg) => {
      if (!msg || typeof msg.type !== 'string') return;
      (listeners[msg.type] || []).forEach((fn) => fn(msg));
    });
  }

  function send(type, data) {
    if (!inHost) return;
    juce.backend.emitEvent('cc', Object.assign({ type }, data || {}));
  }

  function on(type, fn) {
    (listeners[type] = listeners[type] || []).push(fn);
  }

  // Browser preview only: remember small things like the look between reloads.
  function localGet(key, fallback) {
    try { const v = window.localStorage.getItem('cc.' + key); return v === null ? fallback : v; } catch (e) { return fallback; }
  }
  function localSet(key, value) {
    try { window.localStorage.setItem('cc.' + key, value); } catch (e) { /* storage blocked: fine */ }
  }

  return { inHost, send, on, localGet, localSet };
})();
