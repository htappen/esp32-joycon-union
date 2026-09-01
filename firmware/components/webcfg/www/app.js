"use strict";
// Joy-Con Bridge config portal — vanilla JS, no external deps (FR-19).

const $ = (s) => document.querySelector(s);
const api = (path, opts) => fetch(path, opts).then((r) => r.json());

// pad_button bit values must match pad_state.h
const PAD = {
  0: "unmapped", 1: "A", 2: "B", 4: "X", 8: "Y", 16: "LB", 32: "RB",
  64: "View", 128: "Menu", 256: "Guide", 512: "L3", 1024: "R3", 32768: "Share",
};
const MAP_SELECTS = ["map_capture", "map_left_sl", "map_left_sr", "map_right_sl", "map_right_sr"];

function fillSelects() {
  for (const id of MAP_SELECTS) {
    const sel = $("#" + id);
    for (const [val, label] of Object.entries(PAD)) {
      const o = document.createElement("option");
      o.value = val; o.textContent = label;
      sel.appendChild(o);
    }
  }
}

function setPill(id, on, text) {
  const el = $("#" + id);
  el.classList.toggle("on", on);
  el.classList.toggle("off", !on);
  el.querySelector(".st").textContent = text;
}

function setPairingStatus(on) {
  const status = $("#pairing-status");
  status.classList.toggle("on", on);
  status.classList.toggle("off", !on);
  status.textContent = on ? "Pairing mode: ON" : "Pairing mode: OFF";
  $("#pairing-help").textContent = on
    ? "Pairing is active. Put the new controller into Bluetooth pairing mode."
    : "Pairing is off. Start pairing before connecting a new controller.";
  $("#pair-start").disabled = on;
  $("#pair-stop").disabled = !on;
}

async function refreshState() {
  try {
    const s = await api("/api/state");
    setPill("jc-L", s.left.connected,
      s.left.connected ? `${s.left.addr} · ${s.left.battery}%` : "not connected");
    setPill("jc-R", s.right.connected,
      s.right.connected ? `${s.right.addr} · ${s.right.battery}%` : "not connected");
    setPill("host", s.host.connected,
      s.host.connected ? "connected" : (s.host.remembered ? "remembered" : "advertising"));
    setPairingStatus(s.pairing);
    $("#degraded").hidden = !s.degraded;
  } catch (e) { /* Config Mode may briefly drop BT; ignore */ }
}

async function loadMapping() {
  const m = await api("/api/mapping");
  $("#ab").checked = m.ab_xbox_position;
  $("#trigfull").checked = m.triggers_full_scale;
  $("#dz").value = m.deadzone;
  $("#dzv").textContent = m.deadzone;
  for (const id of MAP_SELECTS) $("#" + id).value = String(m[id] ?? 0);
}

async function saveMapping() {
  const body = {
    ab_xbox_position: $("#ab").checked,
    triggers_full_scale: $("#trigfull").checked,
    deadzone: Number($("#dz").value),
  };
  for (const id of MAP_SELECTS) body[id] = Number($("#" + id).value);
  await fetch("/api/mapping", {
    method: "PUT", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const tag = $("#saved"); tag.hidden = false; setTimeout(() => (tag.hidden = true), 1500);
}

function connectWs() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (ev) => {
    const d = JSON.parse(ev.data);
    $("#live").textContent =
      `LX ${d.lx.toString().padStart(6)}  LY ${d.ly.toString().padStart(6)}` +
      `   RX ${d.rx.toString().padStart(6)}  RY ${d.ry.toString().padStart(6)}\n` +
      `LT ${d.lt.toString().padStart(4)}   RT ${d.rt.toString().padStart(4)}\n` +
      `buttons ${d.buttons.toString(2).padStart(16, "0")}\n` +
      `left ${d.degraded_left ? "DEGRADED" : "ok"}   right ${d.degraded_right ? "DEGRADED" : "ok"}`;
    if (d.state) refreshPillsFrom(d);
  };
  ws.onclose = () => setTimeout(connectWs, 1500);
}

function refreshPillsFrom(d) {
  if (!d.state) return;
  setPill("jc-L", d.state.left, d.state.left ? "connected" : "not connected");
  setPill("jc-R", d.state.right, d.state.right ? "connected" : "not connected");
  setPill("host", d.state.host, d.state.host ? "connected" : "advertising");
}

function wire() {
  $("#pair-start").onclick = () => fetch("/api/pairing/start", { method: "POST" }).then(refreshState);
  $("#pair-stop").onclick  = () => fetch("/api/pairing/stop",  { method: "POST" }).then(refreshState);
  $("#mode-play").onclick = () => fetch("/api/mode/play", { method: "POST" });
  document.querySelectorAll("[data-forget]").forEach((b) => {
    b.onclick = () => {
      const side = b.dataset.forget === "1" ? "R" : "L";
      fetch(`/api/joycon/${side}/forget`, { method: "POST" }).then(refreshState);
    };
  });
  $("[data-hostforget]").onclick = () =>
    fetch("/api/host/forget", { method: "POST" }).then(refreshState);
  $("#dz").oninput = () => ($("#dzv").textContent = $("#dz").value);
  $("#save").onclick = saveMapping;
  $("#reboot").onclick = () => confirm("Reboot the bridge?") &&
    fetch("/api/reboot", { method: "POST" });
  $("#factory").onclick = () => confirm("Erase all pairings and settings?") &&
    fetch("/api/factory-reset", { method: "POST" });
}

async function main() {
  fillSelects();
  wire();
  try {
    const v = await api("/api/version");
    $("#fw").textContent = `fw ${v.version}`;
  } catch (e) {}
  await loadMapping();
  await refreshState();
  setInterval(refreshState, 2000);
  connectWs();
}
main();
