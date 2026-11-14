// Keeps a copy of the app's own files so the home-screen app opens even with a poor signal.
// Network first: a fresh copy always wins. The API and the sign-in pages are never cached.
const CACHE = "splitflap-v1";
const SHELL = ["./", "style.css", "app.js", "manifest.webmanifest", "icon.svg", "icon-192.png"];

self.addEventListener("install", (ev) => {
  ev.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting()));
});
self.addEventListener("activate", (ev) => {
  ev.waitUntil(caches.keys().then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)))).then(() => self.clients.claim()));
});
self.addEventListener("fetch", (ev) => {
  const url = new URL(ev.request.url);
  if (ev.request.method !== "GET" || url.origin !== location.origin || /^\/(api\/|login|logout|healthz)/.test(url.pathname)) return;
  ev.respondWith(
    fetch(ev.request).then((res) => {
      // Only plain successes are kept: a redirect to the sign-in page must not replace the app.
      if (res.ok && res.type === "basic" && !res.redirected) {
        const copy = res.clone();
        caches.open(CACHE).then((c) => c.put(ev.request, copy));
      }
      return res;
    }).catch(() => caches.match(ev.request, { ignoreSearch: true }).then((hit) => hit || Response.error())),
  );
});
