const channels = ["exhaust", "air", "room"];
const columns = [
  "time_utc",
  "event",
  "note",
  "firmware",
  "boot_id",
  "uptime_ms",
  "state",
  "faults",
  "profile_fingerprint",
  "fan_command",
  "glow_command",
  "fuel_command",
  "motor_pwm_code",
  "motor_command_percent",
  ...channels.flatMap((name) => [
    `${name}_c`,
    `${name}_sample_ms`,
    `${name}_status`,
    `${name}_valid`,
    `${name}_fresh`,
  ]),
];

function csvCell(value) {
  let text = String(value ?? "");
  // Quoting alone does not prevent spreadsheet formulas in operator notes.
  if (typeof value === "string" && /^\s*[=+\-@]/.test(text)) text = "'" + text;
  return '"' + text.replaceAll('"', '""') + '"';
}

export class Measurements {
  constructor(limit = 21600) {
    this.limit = limit;
    this.rows = [];
    this.active = false;
    this.exportedRows = 0;
    this.samples = 0;
    this.reason = "";
  }

  get unsaved() {
    return this.rows.length > this.exportedRows;
  }

  status() {
    return {
      active: this.active,
      samples: this.samples,
      unsaved: this.unsaved,
      online: this.online,
      reason: this.reason,
      elapsedMs: this.samples ? this.lastUptime - this.firstUptime : 0,
    };
  }

  start(snapshot, online, now = Date.now()) {
    if (this.active || this.unsaved || !online || !this.valid(snapshot))
      return false;
    this.rows = [];
    this.exportedRows = this.samples = 0;
    this.reason = "";
    this.active = this.online = true;
    this.bootId = snapshot.boot_id;
    this.firstUptime = snapshot.uptime_ms;
    this.lastUptime = null;
    this.observe(snapshot, true, now);
    return true;
  }

  valid(snapshot) {
    return (
      typeof snapshot?.boot_id === "string" &&
      snapshot.boot_id.length > 0 &&
      Number.isSafeInteger(snapshot.uptime_ms) &&
      snapshot.uptime_ms >= 0
    );
  }

  append(row) {
    if (!this.active) return;
    this.rows.push(row);
    if (this.rows.length >= this.limit) {
      this.active = false;
      this.reason = "limit";
    }
  }

  event(kind, note, now) {
    this.append([new Date(now).toISOString(), kind, note, "", this.bootId]);
  }

  observe(snapshot, online, now = Date.now()) {
    if (!this.active) return;
    online = online && this.valid(snapshot);
    if (!online) {
      if (this.online) this.event("disconnected", "", now);
      this.online = false;
      return;
    }
    if (
      snapshot.boot_id !== this.bootId ||
      (this.lastUptime !== null && snapshot.uptime_ms < this.lastUptime)
    ) {
      this.stop("restart", now);
      return;
    }
    if (!this.online) this.event("reconnected", "", now);
    else if (
      this.lastUptime !== null &&
      snapshot.uptime_ms - this.lastUptime > 2500
    )
      this.event("sampling_gap", "", now);
    this.online = true;
    if (!this.active || snapshot.uptime_ms === this.lastUptime) return;
    const outputs = snapshot.outputs || {};
    const readings = channels.flatMap((name) => {
      const s = snapshot.sensors?.[name] || {};
      const usable =
        s.valid && s.fresh && s.status === 0 && Number.isFinite(s.milli_c);
      return [
        usable ? s.milli_c / 1000 : null,
        s.sample_ms,
        s.status,
        s.valid,
        s.fresh,
      ];
    });
    this.append([
      new Date(now).toISOString(),
      "sample",
      "",
      snapshot.version,
      this.bootId,
      snapshot.uptime_ms,
      snapshot.state?.code,
      snapshot.faults,
      snapshot.envelope?.fingerprint,
      outputs.fan_request,
      outputs.glow,
      outputs.fuel,
      outputs.speed_code,
      outputs.command_percent,
      ...readings,
    ]);
    this.lastUptime = snapshot.uptime_ms;
    this.samples++;
  }

  note(text, now = Date.now()) {
    const value = text.trim().slice(0, 160);
    if (!this.active || !value) return false;
    this.event("note", value, now);
    return true;
  }

  stop(reason = "stopped", now = Date.now()) {
    if (!this.active) return;
    this.event(reason, "", now);
    this.active = false;
    if (this.reason !== "limit") this.reason = reason;
  }

  csv() {
    return (
      "\uFEFF" +
      [columns, ...this.rows]
        .map((row) => columns.map((_, i) => csvCell(row[i])).join(";"))
        .join("\r\n") +
      "\r\n"
    );
  }

  markExported() {
    this.exportedRows = this.rows.length;
  }
}
