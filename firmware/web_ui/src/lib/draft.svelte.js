import { deepEqual, cloneJson } from "./protocol.js";

// Page drafts survive navigation. A reboot or confirmed value replaces them.
export class Draft {
  value = $state(null);
  dirty = $state(false);
  conflict = $state(false);
  version = null;
  bootId = null;
  baseline = null;
  constructor(fields = null) {
    this.fields = fields;
  }
  select(value) {
    return this.fields
      ? Object.fromEntries(this.fields.map((key) => [key, value[key]]))
      : value;
  }

  sync(snapshot, incoming) {
    if (!snapshot || !incoming) return;
    incoming = this.select(incoming);
    if (
      this.bootId !== snapshot.boot_id ||
      !this.dirty ||
      deepEqual(this.value, incoming)
    ) {
      this.reset(snapshot, incoming);
    } else {
      this.conflict = !deepEqual(this.baseline, incoming);
      if (!this.conflict) this.version = snapshot.state_version;
    }
  }
  reset(snapshot, incoming) {
    incoming = this.select(incoming);
    if (!deepEqual(this.value, incoming)) this.value = cloneJson(incoming);
    this.version = snapshot.state_version;
    this.bootId = snapshot.boot_id;
    this.baseline = cloneJson(incoming);
    this.dirty = false;
    this.conflict = false;
  }
  set(field, value) {
    this.value = { ...this.value, [field]: value };
    this.dirty = true;
  }
  clear() {
    this.value = null;
    this.dirty = this.conflict = false;
    this.version = this.bootId = this.baseline = null;
  }
}
