"use strict";

const PLATFORMS = [
  ["tiktok", "TikTok"],
  ["instagram", "Instagram"],
  ["facebook", "Facebook"],
];
const $ = (sel) => document.querySelector(sel);

// ---- admin key: kept only in this browser ----
const keyInput = $("#admin-key");
try { keyInput.value = localStorage.getItem("adminKey") || ""; } catch (e) { /* storage blocked */ }
keyInput.addEventListener("change", () => {
  try { localStorage.setItem("adminKey", keyInput.value); } catch (e) { /* ignore */ }
});

function toast(msg, isError) {
  const t = $("#toast");
  t.textContent = msg;
  t.className = isError ? "error" : "";
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { t.textContent = ""; }, 6000);
}

async function api(path, body) {
  const opts = body === undefined ? {} : {
    method: "POST",
    headers: { "Content-Type": "application/json", "X-Admin-Key": keyInput.value },
    body: JSON.stringify(body),
  };
  const res = await fetch(path, opts);
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
  return data;
}

// ---- tabs ----
document.querySelectorAll("nav button").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll("nav button").forEach((b) => b.setAttribute("aria-selected", b === btn));
    document.querySelectorAll("main section").forEach((s) => { s.hidden = s.id !== `tab-${btn.dataset.tab}`; });
    if (btn.dataset.tab === "settings") loadConfig();
  });
});

// ---- status ----
const fmtTime = (epoch) => (epoch ? new Date(epoch * 1000).toLocaleString() : "—");
const fmtCount = (n) => (n === null || n === undefined ? "—" : Number(n).toLocaleString());

function renderFrame(frame) {
  const el = $("#frame");
  el.replaceChildren(...[...(frame || "")].map((ch) => {
    const s = document.createElement("span");
    s.textContent = ch;
    return s;
  }));
  el.setAttribute("aria-label", `Current display: ${frame.trim() || "blank"}`);
}

function cell(text, cls) {
  const td = document.createElement("td");
  td.textContent = text;
  if (cls) td.className = cls;
  return td;
}

async function refreshStatus() {
  try {
    const s = await api("/api/status");
    renderFrame(s.display.frame);
    const rows = PLATFORMS.map(([id, name]) => {
      const p = s.platforms[id];
      const tr = document.createElement("tr");
      const status = !p.enabled ? "disabled" : p.source === "manual" ? "manual" : p.status;
      const detail = p.enabled && p.error ? ` — ${p.error}` : "";
      const expiry = p.token.expires_at ? fmtTime(p.token.expires_at) : (p.token.access ? "no expiry" : "no token");
      tr.append(
        cell(name),
        cell(fmtCount(p.count), "num"),
        cell(fmtTime(p.last_ok)),
        cell(status + detail, `status-${p.status}`),
        cell(expiry + (p.token.needs_reauth ? " (re-auth needed)" : "")),
      );
      return tr;
    });
    $("#platform-rows").replaceChildren(...rows);
    const up = Math.floor(s.uptime_s / 3600) + " h " + Math.floor((s.uptime_s % 3600) / 60) + " min";
    $("#device-info").textContent =
      `Wi-Fi ${s.wifi.ssid} (${s.wifi.rssi} dBm, ${s.wifi.ip}) · up ${up} · last reset: ${s.reset_reason} · ` +
      `free heap ${Math.round(s.free_heap / 1024)} KB` + (s.display.quiet ? " · quiet hours" : "") +
      (s.time ? "" : " · clock not synced yet");
    const found = s.bus.present.filter(Boolean).length;
    const missing = s.bus.present.map((ok, i) => (ok ? null : `0x${(16 + i).toString(16)}`)).filter(Boolean);
    $("#bus-info").textContent = `Units answering: ${found}/${s.bus.units}` +
      (missing.length ? ` — missing ${missing.join(", ")}` : "") + (s.bus.error ? ` — last bus error: ${s.bus.error}` : "");
  } catch (e) {
    $("#device-info").textContent = `Can't reach the device: ${e.message}`;
  }
}

$("#rehome").addEventListener("click", async () => {
  try { await api("/api/rehome", {}); toast("Re-homing all units"); } catch (e) { toast(e.message, true); }
});

// ---- settings ----
const form = $("#settings-form");

function buildPlatformSettings() {
  $("#platform-settings").replaceChildren(...PLATFORMS.map(([id, name]) => {
    const row = document.createElement("div");
    row.className = "platform-row";
    row.innerHTML = `
      <label class="check"><input type="checkbox" name="${id}.enabled"> ${name}</label>
      <label>Source <select name="${id}.source"><option value="api">API</option><option value="manual">Manual</option></select></label>
      <label>Manual count <input type="number" min="0" name="${id}.manual_count"></label>`;
    return row;
  }));
}
buildPlatformSettings();

async function loadConfig() {
  try {
    const c = await api("/api/config");
    for (const [id] of PLATFORMS) {
      const p = c.platforms[id];
      form.elements[`${id}.enabled`].checked = p.enabled;
      form.elements[`${id}.source`].value = p.source;
      form.elements[`${id}.manual_count`].value = p.manual_count;
    }
    for (const name of ["mode", "single", "text", "text_align", "units", "label_width", "speed", "poll_minutes",
      "cycle_seconds", "quiet_start", "quiet_end", "timezone", "milestone_step"]) {
      form.elements[name].value = c[name];
    }
    form.elements.celebrate.checked = c.celebrate;
    if (!c.admin_key_set) toast("No admin key set on the device: hold BOOT while powering on to set one.", true);
  } catch (e) {
    toast(e.message, true);
  }
}

form.addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const f = form.elements;
  const body = { platforms: {} };
  for (const [id] of PLATFORMS) {
    body.platforms[id] = {
      enabled: f[`${id}.enabled`].checked,
      source: f[`${id}.source`].value,
      manual_count: Number(f[`${id}.manual_count`].value || 0),
    };
  }
  for (const name of ["mode", "single", "text", "text_align", "quiet_start", "quiet_end", "timezone"]) body[name] = f[name].value;
  for (const name of ["units", "label_width", "speed", "poll_minutes", "cycle_seconds", "milestone_step"]) body[name] = Number(f[name].value);
  body.celebrate = f.celebrate.checked;
  try {
    await api("/api/config", body);
    toast("Settings saved");
    refreshStatus();
  } catch (e) {
    toast(e.message, true);
  }
});

// ---- tokens ----
const tokenForm = $("#token-form");
function syncTokenFields() {
  const platform = tokenForm.elements.platform.value;
  tokenForm.querySelectorAll("[data-for]").forEach((el) => { el.hidden = el.dataset.for !== platform; });
}
tokenForm.elements.platform.addEventListener("change", syncTokenFields);
syncTokenFields();

tokenForm.addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const f = tokenForm.elements;
  const body = { platform: f.platform.value };
  for (const name of ["access_token", "page_id", "refresh_token", "client_key", "client_secret"]) {
    const field = f[name];
    if (field.value && !field.closest("label").hidden) body[name] = field.value.trim();
  }
  try {
    await api("/api/tokens", body);
    toast("Token saved; fetching now");
    tokenForm.reset();
    syncTokenFields();
    setTimeout(refreshStatus, 4000);
  } catch (e) {
    toast(e.message, true);
  }
});

$("#clear-token").addEventListener("click", async () => {
  const platform = tokenForm.elements.platform.value;
  if (!confirm(`Delete the stored ${platform} tokens from the device?`)) return;
  try { await api("/api/tokens", { platform, clear: true }); toast("Tokens cleared"); refreshStatus(); }
  catch (e) { toast(e.message, true); }
});

refreshStatus();
setInterval(refreshStatus, 10000);
