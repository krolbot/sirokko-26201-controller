import { commandAllowed, commandReason, errorText } from "./presentation.js";
import {
  commandMatchesSnapshot,
  hasFault,
  isHexString,
  isActiveSession,
  makeCommandId,
  modelRevisionPayload,
} from "./protocol.js";

const CONFIRMATION_TIMEOUT_MS = 5_000;
const ACTIONS = new Set([
  "START",
  "RESET_FAULT",
  "SETTINGS",
  "CALIBRATE",
  "TIMER",
  "MODEL_REVISION",
  "PROFILE",
  "DEBUG_ENTER",
  "DEBUG_SET",
]);

function initialView() {
  return {
    auth: "anonymous",
    link: "offline",
    snapshot: null,
    pending: null,
    requestInFlight: null,
    message: null,
    ota: { phase: "idle" },
  };
}

function statusMessage(kind, text) {
  return { kind, text };
}

/**
 * State coordinator for the browser UI. It never assumes that HTTP acceptance
 * changed the heater. A command remains pending until a newer state snapshot
 * matches its requested result, or becomes explicitly unknown after five seconds.
 */
export class ControllerSession {
  constructor({
    api,
    onChange = () => {},
    now = () => Date.now(),
    commandId = makeCommandId,
  } = {}) {
    if (!api) throw new TypeError("API client is required");
    this.api = api;
    this.onChange = onChange;
    this.now = now;
    this.commandId = commandId;
    this._view = initialView();
    this._authEpoch = 0;
    this._readEpoch = 0;
    this._commandEpoch = 0;
    this._pollPromise = null;
    this._refreshRequested = false;
    this._lastSnapshotAt = null;
  }

  get view() {
    return this._view;
  }

  _set(patch) {
    this._view = { ...this._view, ...patch };
    this.onChange(this._view);
  }

  dismissMessage() {
    this._set({ message: null });
  }

  _isCurrent(authEpoch, commandEpoch) {
    return (
      this._authEpoch === authEpoch &&
      this._commandEpoch === commandEpoch &&
      this._view.auth === "authenticated"
    );
  }

  _clearForAuthLoss() {
    this._authEpoch += 1;
    this._readEpoch += 1;
    this._commandEpoch += 1;
    this._refreshRequested = false;
    this._set({
      auth: "anonymous",
      link: "offline",
      pending: null,
      requestInFlight: null,
      snapshot: null,
      message: statusMessage("error", "Сеанс истёк. Войдите снова."),
      ota: this._view.ota, // OTA observation must survive the required login after reboot.
    });
  }

  _markLinkLost(error) {
    const interrupted = this._view.pending || this._view.requestInFlight;
    this._readEpoch += 1;
    this._commandEpoch += 1;
    this._refreshRequested = false;
    this._set({
      link: "offline",
      pending: null,
      requestInFlight: null,
      message: interrupted
        ? statusMessage("error", errorText(error, "Действие не подтверждено"))
        : null,
    });
  }

  _handleError(error, fallback) {
    if (error?.kind === "auth" || error?.status === 401) {
      this._clearForAuthLoss();
      return;
    }
    if (error?.kind === "network" || error?.kind === "timeout") {
      this._markLinkLost(error);
      return;
    }
    this._set({
      requestInFlight: null,
      message: statusMessage(
        "error",
        errorText(error, fallback, this._view.snapshot),
      ),
    });
  }

  async restore() {
    const authEpoch = ++this._authEpoch;
    this._commandEpoch += 1;
    this._set({
      auth: "authenticated",
      link: "connecting",
      pending: null,
      requestInFlight: null,
      message: null,
    });
    await this.poll();
    return authEpoch === this._authEpoch && this._view.auth === "authenticated";
  }

  async login(password) {
    const authEpoch = ++this._authEpoch;
    this._commandEpoch += 1;
    this._set({
      requestInFlight: "LOGIN",
      message: null,
      pending: null,
      link: "connecting",
    });
    try {
      await this.api.login(password);
      if (authEpoch !== this._authEpoch) return false;
      this._set({
        auth: "authenticated",
        link: "connecting",
        requestInFlight: null,
        message: null,
      });
      await this.poll();
      return this._view.auth === "authenticated";
    } catch (error) {
      if (authEpoch !== this._authEpoch) return false;
      if (error?.kind === "auth" || error?.status === 401) {
        this._set({
          auth: "anonymous",
          link: "offline",
          requestInFlight: null,
          message: statusMessage("error", "Пароль не принят"),
        });
      } else {
        this._set({
          auth: "anonymous",
          link: "offline",
          requestInFlight: null,
          message: statusMessage("error", errorText(error, "Не удалось войти")),
        });
      }
      return false;
    }
  }

  async poll() {
    if (
      this._view.auth !== "authenticated" ||
      this._view.requestInFlight === "OTA"
    )
      return null;
    if (this._pollPromise) return this._pollPromise;
    const authEpoch = this._authEpoch;
    const readEpoch = this._readEpoch;
    const poll = (async () => {
      try {
        const snapshot = await this.api.state();
        if (
          authEpoch !== this._authEpoch ||
          readEpoch !== this._readEpoch ||
          this._view.auth !== "authenticated"
        )
          return null;
        this._applySnapshot(snapshot);
        return snapshot;
      } catch (error) {
        if (authEpoch !== this._authEpoch || readEpoch !== this._readEpoch)
          return null;
        if (error?.kind === "auth" || error?.status === 401)
          this._clearForAuthLoss();
        else this._markLinkLost(error);
        return null;
      } finally {
        if (this._pollPromise === poll) {
          this._pollPromise = null;
          if (this._refreshRequested && this._view.auth === "authenticated") {
            this._refreshRequested = false;
            void this.poll();
          }
        }
      }
    })();
    this._pollPromise = poll;
    return poll;
  }

  requestRefresh() {
    if (this._view.auth !== "authenticated") return Promise.resolve(null);
    if (this._pollPromise) {
      this._refreshRequested = true;
      return this._pollPromise;
    }
    return this.poll();
  }

  _applySnapshot(snapshot) {
    if (
      !snapshot ||
      !isHexString(snapshot.boot_id) ||
      !isHexString(snapshot.state_version) ||
      !isHexString(snapshot.run_id) ||
      !Number.isSafeInteger(snapshot.uptime_ms) ||
      !snapshot.state?.code ||
      !snapshot.settings ||
      !snapshot.sensors
    ) {
      this._markLinkLost({ message: "Некорректный ответ контроллера" });
      return;
    }
    this._lastSnapshotAt = this.now();
    const previous = this._view.snapshot;
    const bootChanged = Boolean(
      previous &&
      isHexString(previous.boot_id) &&
      snapshot.boot_id !== previous.boot_id,
    );
    const fault = hasFault(snapshot);
    let pending = this._view.pending;
    let requestInFlight = this._view.requestInFlight;
    let message = this._view.message;
    let ota = this._view.ota;

    if (bootChanged) {
      this._commandEpoch += 1;
      pending = null;
      requestInFlight = null;
      message = statusMessage("warning", "Контроллер перезапущен.");
    } else if (
      fault &&
      (!hasFault(previous) || snapshot.faults !== previous.faults)
    ) {
      this._commandEpoch += 1;
      message =
        pending || requestInFlight
          ? statusMessage("warning", "Действие прервано из-за ошибки отопителя")
          : null;
      pending = null;
      requestInFlight = null;
    } else if (
      isActiveSession(previous) &&
      ["STOP_PURGE", "IDLE", "UNCONFIGURED"].includes(snapshot.state.code) &&
      (pending || requestInFlight) &&
      pending?.action !== "STOP" &&
      requestInFlight !== "STOP"
    ) {
      this._commandEpoch += 1;
      pending = null;
      requestInFlight = null;
      message = statusMessage(
        "warning",
        "Действие прервано остановкой отопителя",
      );
    } else if (pending && commandMatchesSnapshot(pending, snapshot)) {
      message =
        pending.action === "DEBUG_SET" ? null : statusMessage("ok", "Готово.");
      pending = null;
    }

    if (
      [
        "activation_queued",
        "checking_startup",
        "confirmation_timeout",
      ].includes(ota?.phase) &&
      isHexString(snapshot.boot_id) &&
      snapshot.boot_id !== ota.beforeBootId &&
      snapshot.version
    ) {
      ota = {
        ...ota,
        phase:
          ota.expectedVersion && snapshot.version === ota.expectedVersion
            ? snapshot.hardware?.startup_ready === true
              ? "running_expected_version"
              : "checking_startup"
            : "different_version",
        observedBootId: snapshot.boot_id,
        observedVersion: snapshot.version,
      };
      message =
        ota.phase === "checking_startup"
          ? statusMessage("pending", "Проверка запуска…")
          : statusMessage(
              ota.phase === "running_expected_version" ? "ok" : "warning",
              ota.phase === "running_expected_version"
                ? `Версия ${snapshot.version}.`
                : `Новая загрузка сообщает ${snapshot.version}, ожидалась ${ota.expectedVersion || "неизвестная версия"}. Обновление не подтверждено; возможен откат.`,
            );
    }

    // A version observation is never proof of a healthy controller. Fault wins over OTA feedback.
    this._set({
      snapshot,
      link: "online",
      pending,
      requestInFlight,
      message,
      ota,
    });
  }

  tick() {
    if (
      this._view.link === "online" &&
      this._view.requestInFlight !== "OTA" &&
      this._lastSnapshotAt !== null &&
      this.now() - this._lastSnapshotAt > 5_000
    ) {
      this._markLinkLost({ message: "Нет новых данных" });
    }
    if (
      ["activation_queued", "checking_startup"].includes(
        this._view.ota.phase,
      ) &&
      this.now() >= this._view.ota.deadline
    ) {
      this._set({
        ota: { ...this._view.ota, phase: "confirmation_timeout" },
        message: statusMessage("warning", "Нет связи после обновления."),
      });
    }
    const pending = this._view.pending;
    if (pending && this.now() >= pending.deadline) {
      this._set({
        pending: null,
        message: statusMessage("warning", "Контроллер не подтвердил действие."),
      });
    }
  }

  async submit(action, fields = {}) {
    this.tick();
    if (!ACTIONS.has(action))
      throw new TypeError(`Неподдерживаемое публичное действие: ${action}`);
    if (this._view.auth !== "authenticated") {
      this._set({
        message: statusMessage("error", "Войдите для отправки команды."),
      });
      return { status: "AUTH_REQUIRED" };
    }
    if (this._view.link !== "online") {
      this._set({ message: statusMessage("error", "Нет связи.") });
      return { status: "OFFLINE" };
    }
    if (
      this._view.pending ||
      this._view.requestInFlight ||
      this._view.ota.phase === "activation_queued"
    )
      return { status: "BUSY" };
    const baseline = this._view.snapshot;
    if (!baseline || !isHexString(baseline.state_version))
      return { status: "NO_SNAPSHOT" };
    if (!commandAllowed(baseline, action)) {
      this._set({
        message: statusMessage("warning", commandReason(baseline, action)),
      });
      return { status: "BLOCKED" };
    }
    let commandFields = fields;
    if (action === "MODEL_REVISION") {
      commandFields = {
        revision: modelRevisionPayload(baseline.learned_model, fields.revision),
      };
    }
    const authEpoch = this._authEpoch;
    const commandEpoch = ++this._commandEpoch;
    const payload = { action, command_id: this.commandId(), ...commandFields };
    this._readEpoch += 1;
    this._set({
      requestInFlight: action,
      message: statusMessage("pending", "Отправляем…"),
    });
    try {
      const grant = await this.api.challenge(payload);
      if (!this._isCurrent(authEpoch, commandEpoch))
        return { status: "CANCELLED" };
      // Do not submit a command whose editable snapshot lost a race to the TFT
      // or another client between state read and challenge issuance.
      if (
        grant.state_version !== baseline.state_version ||
        grant.boot_id !== baseline.boot_id
      ) {
        this._set({
          requestInFlight: null,
          message: statusMessage(
            "warning",
            "Состояние изменилось. Проверьте текущие настройки",
          ),
        });
        void this.requestRefresh();
        return { status: "STALE_SNAPSHOT" };
      }
      const response = await this.api.command({ ...payload, grant });
      if (!this._isCurrent(authEpoch, commandEpoch))
        return { status: "CANCELLED" };
      if (response?.status !== "QUEUED") {
        this._set({
          requestInFlight: null,
          message: statusMessage(
            "error",
            errorText(
              response,
              "Контроллер отклонил действие",
              this._view.snapshot,
            ),
          ),
        });
        return { status: "REJECTED", response };
      }
      const pending = {
        action,
        payload,
        baseline,
        startedAt: this.now(),
        deadline: this.now() + CONFIRMATION_TIMEOUT_MS,
      };
      this._set({
        requestInFlight: null,
        pending,
        message: statusMessage(
          "pending",
          "Ожидается подтверждение контроллера",
        ),
      });
      void this.requestRefresh();
      return { status: "QUEUED" };
    } catch (error) {
      if (!this._isCurrent(authEpoch, commandEpoch))
        return { status: "CANCELLED" };
      this._handleError(error, "Команда не отправлена");
      return { status: "ERROR", error };
    }
  }

  async stop() {
    if (this._view.auth !== "authenticated") {
      this._set({ message: statusMessage("error", "Войдите для управления.") });
      return { status: "AUTH_REQUIRED" };
    }
    const baseline = this._view.snapshot;
    const authEpoch = this._authEpoch;
    const commandEpoch = ++this._commandEpoch;
    this._readEpoch += 1;
    this._set({
      pending: null,
      requestInFlight: "STOP",
      message: statusMessage("pending", "Останавливаем…"),
    });
    try {
      const response = await this.api.stop(this.commandId());
      if (!this._isCurrent(authEpoch, commandEpoch))
        return { status: "CANCELLED" };
      if (response?.status !== "STOP_REQUESTED") {
        this._set({
          requestInFlight: null,
          message: statusMessage(
            "error",
            errorText(
              response,
              "Остановка не подтверждена",
              this._view.snapshot,
            ),
          ),
        });
        return { status: "REJECTED", response };
      }
      const pending = baseline
        ? {
            action: "STOP",
            payload: { action: "STOP" },
            baseline,
            startedAt: this.now(),
            deadline: this.now() + CONFIRMATION_TIMEOUT_MS,
          }
        : null;
      this._set({
        requestInFlight: null,
        pending,
        message: statusMessage("pending", "Ожидается остановка."),
      });
      void this.requestRefresh();
      return { status: "STOP_REQUESTED" };
    } catch (error) {
      if (!this._isCurrent(authEpoch, commandEpoch))
        return { status: "CANCELLED" };
      this._handleError(error, "Остановка не подтверждена");
      return { status: "ERROR", error };
    }
  }

  async uploadOta(file) {
    this.tick();
    if (this._view.auth !== "authenticated" || this._view.link !== "online") {
      this._set({ message: statusMessage("error", "Нет связи.") });
      return { status: "OFFLINE" };
    }
    if (
      this._view.pending ||
      this._view.requestInFlight ||
      this._view.ota.phase === "activation_queued"
    )
      return { status: "BUSY" };
    const baseline = this._view.snapshot;
    if (!baseline || !isHexString(baseline.state_version))
      return { status: "NO_SNAPSHOT" };
    if (!commandAllowed(baseline, "OTA")) {
      this._set({
        message: statusMessage("warning", commandReason(baseline, "OTA")),
      });
      return { status: "BLOCKED" };
    }
    const authEpoch = this._authEpoch;
    const commandEpoch = ++this._commandEpoch;
    this._readEpoch += 1;
    this._set({
      requestInFlight: "OTA",
      message: statusMessage("pending", "Загружаем прошивку…"),
    });
    try {
      const prepared = await this.api.prepareOta(file, this.commandId());
      if (!this._isCurrent(authEpoch, commandEpoch))
        return { status: "CANCELLED" };
      if (
        prepared.grant?.state_version !== baseline.state_version ||
        prepared.grant?.boot_id !== baseline.boot_id
      ) {
        this._set({
          requestInFlight: null,
          message: statusMessage(
            "warning",
            "Состояние изменилось. Повторите обновление",
          ),
        });
        void this.requestRefresh();
        return { status: "STALE_SNAPSHOT" };
      }
      const response = await this.api.sendOta(prepared);
      if (!this._isCurrent(authEpoch, commandEpoch))
        return { status: "CANCELLED" };
      if (response?.status !== "VERIFIED_ACTIVATION_QUEUED") {
        this._set({
          requestInFlight: null,
          message: statusMessage(
            "error",
            errorText(
              response,
              "Обновление не подтверждено",
              this._view.snapshot,
            ),
          ),
        });
        return { status: "REJECTED", response };
      }
      this._set({
        requestInFlight: null,
        ota: {
          phase: "activation_queued",
          beforeBootId: baseline.boot_id,
          beforeVersion: baseline.version,
          expectedVersion: response.expected_version || null,
          deadline: this.now() + 120_000,
        },
        message: statusMessage("pending", "Ожидается перезапуск."),
      });
      void this.requestRefresh();
      return { status: "VERIFIED_ACTIVATION_QUEUED" };
    } catch (error) {
      if (!this._isCurrent(authEpoch, commandEpoch))
        return { status: "CANCELLED" };
      this._handleError(error, "Не удалось передать прошивку");
      return { status: "ERROR", error };
    }
  }
}
