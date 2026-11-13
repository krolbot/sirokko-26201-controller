#include "web_state.hpp"
#include "envelope_fields.hpp"
#include "command_policy.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
namespace {
const char *russian_state(sirokko::State state) {
    switch (state) {
    case sirokko::State::UNCONFIGURED:
        return "Требуется настройка";
    case sirokko::State::RECOVERY_PURGE:
        return "Охлаждение незавершённого цикла";
    case sirokko::State::IDLE:
        return "Отопитель выключен";
    case sirokko::State::PREHEAT:
        return "Накал свечи";
    case sirokko::State::IGNITION:
        return "Розжиг";
    case sirokko::State::RUN:
        return "Нагрев";
    case sirokko::State::STOP_PURGE:
        return "Охлаждение";
    case sirokko::State::FAULT_PURGE:
        return "Охлаждение после аварии";
    case sirokko::State::FAULT:
        return "Авария";
    case sirokko::State::MAINTENANCE:
        return "Обновление прошивки";
    }
    return "Неизвестное состояние";
}

const char *russian_block(sirokko::Block block) {
    switch (block) {
    case sirokko::Block::INITIALIZING:
        return "Контроллер запускается";
    case sirokko::Block::STORAGE_UNAVAILABLE:
        return "Ошибка чтения или записи настроек";
    case sirokko::Block::NONE:
        return "Нет";
    case sirokko::Block::ENVELOPE_MISSING:
        return "Не все параметры отопителя заполнены";
    case sirokko::Block::ENVELOPE_INVALID:
        return "Параметры отопителя не позволяют выполнить эту команду";
    case sirokko::Block::CALIBRATION_TOO_SLOW:
        return "Темп изменения ШИМ слишком мал для калибровки";
    case sirokko::Block::RECOVERY_PURGE:
        return "Дождитесь охлаждения отопителя";
    case sirokko::Block::SENSOR_INVALID:
        return "Нет достоверных показаний нужных для запуска датчиков температуры";
    case sirokko::Block::SENSOR_STALE:
        return "Данные датчиков устарели";
    case sirokko::Block::INITIAL_THERMAL_STATE:
        return "Выхлоп ещё не остыл до порога запуска";
    case sirokko::Block::FAULT_LATCHED:
        return "Запуск заблокирован до сброса аварии";
    case sirokko::Block::BUSY:
        return "Команда недоступна на текущем этапе работы";
    case sirokko::Block::MAINTENANCE:
        return "Обновление прошивки";
    case sirokko::Block::MODEL_UNAVAILABLE:
        return "Нужна калибровка";
    case sirokko::Block::LOCAL_CONTROLS_UNAVAILABLE:
        return "Не удалось запустить драйвер экрана или кнопки";
    case sirokko::Block::STALE_COMMAND:
        return "Состояние отопителя изменилось после отправки команды";
    }
    return "Контроллер не сообщил причину блокировки";
}

void append_sensor(JsonBuilder *json, const sirokko::SensorSample &sample, bool valid, bool fresh) {
    json->raw("{\"milli_c\":");
    json->integer(sample.milli_c);
    json->raw(",\"sample_ms\":");
    json->unsigned_integer(sample.sample_ms);
    json->raw(",\"status\":");
    json->unsigned_integer(static_cast<uint32_t>(sample.status));
    json->raw(",\"valid\":");
    json->boolean(valid);
    json->raw(",\"fresh\":");
    json->boolean(fresh);
    json->character('}');
}

struct CommissioningText {
    const char *code;
    const char *ru;
};
CommissioningText commissioning_phase_text(sirokko::CommissioningPhase value) {
    switch (value) {
    case sirokko::CommissioningPhase::IDLE:
        return {"IDLE", "Не запущена"};
    case sirokko::CommissioningPhase::STARTING:
        return {"STARTING", "Запуск отопителя для калибровки"};
    case sirokko::CommissioningPhase::WAIT_REFERENCE_COMMAND:
        return {"WAIT_REFERENCE_COMMAND", "Увеличение ШИМ до значения розжига"};
    case sirokko::CommissioningPhase::SETTLE_REFERENCE:
        return {"SETTLE_REFERENCE", "Ожидание стабильных температур при ШИМ розжига"};
    case sirokko::CommissioningPhase::WAIT_DESCENT_COMMAND:
        return {"WAIT_DESCENT_COMMAND", "Снижение ШИМ двигателя"};
    case sirokko::CommissioningPhase::SETTLE_POINT:
        return {"SETTLE_POINT", "Ожидание стабильных температур при сниженном ШИМ"};
    case sirokko::CommissioningPhase::WAIT_RETURN_COMMAND:
        return {"WAIT_RETURN_COMMAND", "Возврат к ШИМ розжига"};
    case sirokko::CommissioningPhase::SETTLE_RETURN_REFERENCE:
        return {"SETTLE_RETURN_REFERENCE", "Сравнение температур при возврате к ШИМ розжига"};
    case sirokko::CommissioningPhase::WAIT_REPEAT_COMMAND:
        return {"WAIT_REPEAT_COMMAND", "Повторная установка сниженного ШИМ"};
    case sirokko::CommissioningPhase::SETTLE_REPEAT_POINT:
        return {"SETTLE_REPEAT_POINT", "Сравнение температур при повторе сниженного ШИМ"};
    case sirokko::CommissioningPhase::WAIT_POST_REPEAT_REFERENCE_COMMAND:
        return {"WAIT_POST_REPEAT_REFERENCE_COMMAND", "Повторный возврат к ШИМ розжига"};
    case sirokko::CommissioningPhase::SETTLE_POST_REPEAT_REFERENCE:
        return {"SETTLE_POST_REPEAT_REFERENCE", "Повторное сравнение температур при ШИМ розжига"};
    case sirokko::CommissioningPhase::COMPLETE_PENDING_STOP:
        return {"COMPLETE_PENDING_STOP", "Измерения завершены, останов и охлаждение"};
    case sirokko::CommissioningPhase::FAILED:
        return {"FAILED", "Калибровка прервана из-за ошибки"};
    case sirokko::CommissioningPhase::CANCELLED:
        return {"CANCELLED", "Отменена"};
    case sirokko::CommissioningPhase::PERSIST_STAGING:
        return {"PERSIST_STAGING", "Подготовка к сохранению калибровки"};
    case sirokko::CommissioningPhase::PERSIST_COMMITTING:
        return {"PERSIST_COMMITTING", "Запись и проверка результата калибровки"};
    case sirokko::CommissioningPhase::SAVED:
        return {"SAVED", "Результат калибровки сохранён"};
    case sirokko::CommissioningPhase::PERSIST_FAILED:
        return {"PERSIST_FAILED", "Не удалось сохранить калибровку"};
    }
    return {"UNKNOWN", "Неизвестное состояние"};
}
const char *russian_event(const char *kind) {
    if (!kind)
        return "Неизвестное событие";
    if (std::strcmp(kind, "PROFILE_SAVED") == 0)
        return "Параметры отопителя сохранены";
    if (std::strcmp(kind, "START") == 0)
        return "Запуск отопителя";
    if (std::strcmp(kind, "STOP") == 0)
        return "Остановка отопителя";
    if (std::strcmp(kind, "FAULT") == 0)
        return "Авария";
    if (std::strcmp(kind, "COMBUSTION_SUSPECT") == 0)
        return "Проверка падения температуры выхлопа";
    if (std::strcmp(kind, "TIMER_EXPIRED") == 0)
        return "Истёк таймер останова";
    if (std::strcmp(kind, "TIMER_ARMED") == 0)
        return "Таймер останова установлен";
    if (std::strcmp(kind, "TIMER_CANCELLED") == 0)
        return "Таймер останова отменён";
    if (std::strstr(kind, "KEY0") || std::strstr(kind, "OPERATOR"))
        return "Калибровка остановлена кнопкой на плате";
    const struct {
        const char *code;
        const char *text;
    } events[] = {
        {"CALIBRATION_START", "Калибровка начата"},
        {"CALIBRATION_CANCELLED", "Калибровка отменена"},
        {"CALIBRATION_PENDING_COOLDOWN", "Измерения завершены, охлаждение"},
        {"CALIBRATION_REJECTED", "Результат калибровки отклонён"},
        {"CALIBRATION_SAVED", "Результат калибровки сохранён"},
        {"CALIBRATION_STORAGE_UNCONFIRMED", "Сохранение калибровки не подтверждено"},
        {"CALIBRATION_STORAGE_FAILED", "Не удалось сохранить калибровку"},
    };
    for (const auto &event : events)
        if (std::strcmp(kind, event.code) == 0)
            return event.text;
    for (int value = 0; value <= static_cast<int>(sirokko::CommissioningPhase::PERSIST_FAILED); ++value) {
        const auto phase = commissioning_phase_text(static_cast<sirokko::CommissioningPhase>(value));
        if (std::strcmp(kind, phase.code) == 0)
            return phase.ru;
    }
    return "Неизвестное событие";
}

CommissioningText commissioning_reason_text(sirokko::CommissioningReason value) {
    switch (value) {
    case sirokko::CommissioningReason::NONE:
        return {"NONE", "Нет"};
    case sirokko::CommissioningReason::COMPLETE:
        return {"COMPLETE", "Измерения завершены"};
    case sirokko::CommissioningReason::INVALID_CONFIG:
        return {"INVALID_CONFIG", "Параметры не позволяют выполнить калибровку"};
    case sirokko::CommissioningReason::BACKDATED_TIME:
        return {"BACKDATED_TIME", "Время контроллера изменилось в обратную сторону"};
    case sirokko::CommissioningReason::OVERALL_TIMEOUT:
        return {"OVERALL_TIMEOUT", "Истекло время калибровки"};
    case sirokko::CommissioningReason::RUN_CONTEXT_LOST:
        return {"RUN_CONTEXT_LOST", "Отопитель вышел из режима нагрева во время калибровки"};
    case sirokko::CommissioningReason::COMMAND_OUT_OF_RANGE:
        return {"COMMAND_OUT_OF_RANGE", "ШИМ двигателя вышел за границы калибровки"};
    case sirokko::CommissioningReason::COMMAND_SLEW_VIOLATION:
        return {"COMMAND_SLEW_VIOLATION", "ШИМ двигателя меняется быстрее установленного ограничения"};
    case sirokko::CommissioningReason::COMMAND_NOT_REACHED:
        return {"COMMAND_NOT_REACHED", "ШИМ двигателя не достиг заданного значения за отведённое время"};
    case sirokko::CommissioningReason::SENSOR_INVALID:
        return {"SENSOR_INVALID", "Недостоверные показания датчика"};
    case sirokko::CommissioningReason::STALE_SAMPLE:
        return {"STALE_SAMPLE", "Устаревшие показания"};
    case sirokko::CommissioningReason::SAMPLE_TIMESTAMP_REGRESSION:
        return {"SAMPLE_TIMESTAMP_REGRESSION", "Время нового измерения раньше предыдущего"};
    case sirokko::CommissioningReason::SAMPLE_SKEW:
        return {"SAMPLE_SKEW", "Термопары измерены с недопустимым интервалом между ними"};
    case sirokko::CommissioningReason::REPEATED_SAMPLE:
        return {"REPEATED_SAMPLE", "Повторно получено прежнее измерение термопары"};
    case sirokko::CommissioningReason::SAMPLE_GAP:
        return {"SAMPLE_GAP", "Превышен интервал между измерениями термопар"};
    case sirokko::CommissioningReason::REFERENCE_TIMEOUT:
        return {"REFERENCE_TIMEOUT", "Температуры не стабилизировались при ШИМ розжига"};
    case sirokko::CommissioningReason::POINT_TIMEOUT:
        return {"POINT_TIMEOUT", "Температуры не стабилизировались при сниженном ШИМ"};
    case sirokko::CommissioningReason::REFERENCE_NOT_REPRODUCIBLE:
        return {"REFERENCE_NOT_REPRODUCIBLE", "Температуры не повторились после возврата к ШИМ розжига"};
    case sirokko::CommissioningReason::NO_COMMAND_RESPONSE:
        return {"NO_COMMAND_RESPONSE", "Температуры не изменились достаточно после изменения ШИМ"};
    case sirokko::CommissioningReason::POINT_NOT_REPRODUCIBLE:
        return {"POINT_NOT_REPRODUCIBLE", "Температуры не повторились при том же сниженном ШИМ"};
    case sirokko::CommissioningReason::DYNAMIC_NOT_REPRODUCIBLE:
        return {"DYNAMIC_NOT_REPRODUCIBLE",
                "Изменение температур во времени не повторилось при том же изменении ШИМ"};
    case sirokko::CommissioningReason::INSUFFICIENT_MARGIN:
        return {"INSUFFICIENT_MARGIN", "Не удалось определить рабочий диапазон ШИМ"};
    case sirokko::CommissioningReason::OPERATOR_NEGATIVE:
        return {"OPERATOR_NEGATIVE", "Калибровка прервана кнопкой на плате"};
    case sirokko::CommissioningReason::EXTERNAL_CANCEL:
        return {"EXTERNAL_CANCEL", "Калибровка отменена командой остановки"};
    case sirokko::CommissioningReason::STORAGE_UNCONFIRMED:
        return {"STORAGE_UNCONFIRMED", "Контроллер не подтвердил сохранение калибровки"};
    }
    return {"UNKNOWN", "Неизвестное состояние"};
}
void append_model(JsonBuilder *json, const sirokko::LearnedOperatingModel &model) {
    json->raw("{\"valid\":");
    json->boolean(model.valid);
    json->raw(",\"minimum_speed_code\":");
    json->unsigned_integer(static_cast<uint32_t>(model.minimum_speed_code));
    json->raw(",\"maximum_speed_code\":");
    json->unsigned_integer(static_cast<uint32_t>(model.maximum_speed_code));
    json->raw(",\"slew_up_per_s\":");
    json->real(model.slew_up_per_s);
    json->raw(",\"slew_down_per_s\":");
    json->real(model.slew_down_per_s);
    json->raw(",\"fingerprint\":\"");
    json->hexadecimal(model.fingerprint);
    json->raw("\"");
    json->raw(",\"provenance\":{\"safety_envelope_fingerprint\":\"");
    json->hexadecimal(model.provenance.safety_envelope_fingerprint);
    json->raw("\",\"commissioning_started_ms\":");
    json->unsigned_integer(model.provenance.commissioning_started_ms);
    json->raw(",\"commissioning_completed_ms\":");
    json->unsigned_integer(model.provenance.commissioning_completed_ms);
    json->raw(",\"revision\":");
    json->unsigned_integer(model.provenance.revision);
    json->raw("}");
    json->raw(",\"measurement\":{\"elapsed_ms\":");
    json->unsigned_integer(model.measurements.elapsed_ms);
    json->raw(",\"tested_point_count\":");
    json->unsigned_integer(model.measurements.tested_point_count);
    json->raw(",\"observed_slew_up_codes_per_s\":");
    json->real(model.measurements.observed_slew_up_codes_per_s);
    json->raw(",\"observed_slew_down_codes_per_s\":");
    json->real(model.measurements.observed_slew_down_codes_per_s);
    json->raw("}}");
}

} // namespace
bool build_state_json(const RuntimeView &view, const WebStateInfo &info, JsonBuilder *json) {
    const sirokko::Snapshot &control = view.control;
    json->raw("{\"version\":");
    json->quoted(info.version);
    json->raw(",\"uptime_ms\":");
    json->unsigned_integer(view.now_ms);
    json->raw(",\"state_version\":\"");
    json->hexadecimal(control.state_version);
    json->raw("\"");
    json->raw(",\"boot_id\":\"");
    json->hexadecimal(control.boot_id);
    json->raw("\"");
    json->raw(",\"run_id\":\"");
    json->hexadecimal(control.run_id);
    json->raw("\"");
    json->raw(",\"state\":{\"code\":");
    json->quoted(sirokko::state_name(control.state));
    json->raw(",\"ru\":");
    json->quoted(russian_state(control.state));
    json->raw("}");
    json->raw(",\"start_block\":{\"code\":");
    json->quoted(sirokko::block_name(control.start_block));
    json->raw(",\"ru\":");
    json->quoted(russian_block(control.start_block));
    json->raw("}");
    json->raw(",\"faults\":");
    json->unsigned_integer(control.faults);
    json->raw(",\"fault_messages\":[");
    const struct {
        uint32_t mask;
        const char *text;
    } descriptions[] = {
        {sirokko::FAULT_EXHAUST, "Нет достоверных показаний выхлопа"},
        {sirokko::FAULT_AIR, "Нет достоверных показаний нагретого воздуха"},
        {sirokko::FAULT_ROOM, "Нет достоверных показаний температуры помещения"},
        {sirokko::FAULT_OVER_TEMPERATURE, "Достигнут предел температуры выхлопа; топливо отключено"},
        {sirokko::FAULT_IGNITION_TIMEOUT, "Горение не подтверждено при розжиге"},
        {sirokko::FAULT_FLAME_LOST, "Выхлоп остыл ниже порога отключения топлива"},
        {sirokko::FAULT_COMBUSTION_DROP, "Температура выхлопа устойчиво падает при неизменном ШИМ двигателя"},
        {sirokko::FAULT_CONTROL_WATCHDOG, "Контроллер не выполнил цикл управления вовремя"},
        {sirokko::FAULT_COMMISSIONING, "Ошибка калибровки"},
        {sirokko::FAULT_STORAGE, "Ошибка чтения или записи настроек или калибровки"},
        {sirokko::FAULT_LOCAL_CONTROLS, "Не запущен драйвер экрана или кнопки"},
    };
    bool first_fault = true;
    for (const auto &description : descriptions) {
        if (!(control.faults & description.mask))
            continue;
        if (!first_fault)
            json->character(',');
        first_fault = false;
        json->quoted(description.text);
    }
    json->character(']');
    const auto &monitor = control.combustion;
    const char *phases[] = {"OFF", "LEARNING", "WATCHING", "SUSPECT", "LOST"};
    json->raw(",\"combustion\":{\"phase\":");
    json->quoted(phases[static_cast<unsigned>(monitor.phase)]);
    json->raw(",\"samples\":");
    json->unsigned_integer(monitor.samples);
    json->raw(",\"reference_exhaust_milli_c\":");
    json->integer(monitor.reference_exhaust_milli_c);
    json->raw(",\"reference_air_milli_c\":");
    json->integer(monitor.reference_air_milli_c);
    json->raw(",\"drop_limit_milli_c\":");
    json->integer(monitor.drop_limit_milli_c);
    json->raw(",\"exhaust_slope_milli_c_per_s\":");
    json->integer(monitor.exhaust_slope_milli_c_per_s);
    json->raw(",\"air_slope_milli_c_per_s\":");
    json->integer(monitor.air_slope_milli_c_per_s);
    json->raw(",\"suspect_ms\":");
    json->unsigned_integer(monitor.suspect_ms);
    json->character('}');

    json->raw(",\"room_excess\":");
    json->boolean(control.room_excess);
    json->raw(",\"storage_ok\":");
    json->boolean(view.storage_ok);
    json->raw(",\"sensors\":{\"exhaust\":");
    append_sensor(json, view.sensors.exhaust, sirokko::thermocouple_valid(view.sensors.exhaust),
                  sirokko::thermocouple_fresh(view.sensors.exhaust, view.now_ms));
    json->raw(",\"air\":");
    append_sensor(json, view.sensors.air, sirokko::thermocouple_valid(view.sensors.air),
                  sirokko::thermocouple_fresh(view.sensors.air, view.now_ms));
    json->raw(",\"room\":");
    append_sensor(json, view.sensors.room, sirokko::room_temperature_valid(view.sensors.room),
                  sirokko::room_temperature_fresh(view.sensors.room, view.now_ms));
    json->raw("}");
    json->raw(",\"outputs\":{\"fan_request\":");
    json->boolean(control.outputs.fan_request);
    json->raw(",\"speed_code\":");
    json->unsigned_integer(static_cast<uint32_t>(control.outputs.speed_code));
    json->raw(",\"glow\":");
    json->boolean(control.outputs.glow);
    json->raw(",\"fuel\":");
    json->boolean(control.outputs.fuel);
    json->raw(",\"command_percent\":");
    json->real(control.command_percent);
    json->raw("}");
    json->raw(",\"hardware\":{\"outputs_enabled\":");
    json->boolean(view.envelope.provisioned);
    json->raw(",\"local_controls_initialized\":");
    json->boolean(view.local_controls_initialized);
    json->raw(",\"startup_ready\":");
    json->boolean(view.startup_ready);
    json->raw("}");
    json->raw(",\"commands\":{");
    bool first_command = true;
    for (const auto &entry : command_policy::commands) {
        if (!first_command)
            json->character(',');
        first_command = false;
        const auto blocked = command_policy::block(view, entry.action);
        json->quoted(entry.name);
        json->raw(":{\"allowed\":");
        json->boolean(blocked == sirokko::Block::NONE);
        json->raw(",\"code\":");
        json->quoted(sirokko::block_name(blocked));
        json->raw(",\"reason\":");
        json->quoted(blocked == sirokko::Block::NONE ? "" : russian_block(blocked));
        json->character('}');
    }
    json->character('}');
    json->raw(",\"wifi\":{\"password_required\":");
    json->boolean(info.password_required);
    json->raw(",\"next_boot_password_required\":");
    json->boolean(info.next_password_required);
    json->raw(",\"restart_required\":");
    json->boolean(info.wifi_restart_required);
    const auto wifi_block = command_policy::wifi_password_block(view);
    json->raw(",\"change_allowed\":");
    json->boolean(wifi_block == sirokko::Block::NONE);
    json->raw(",\"change_reason\":");
    json->quoted(wifi_block == sirokko::Block::NONE ? ""
                 : wifi_block == sirokko::Block::BUSY
                     ? "Смена пароля доступна после остановки и охлаждения отопителя"
                     : russian_block(wifi_block));
    json->raw("}");
    json->raw(",\"settings\":{\"room_setpoint_milli_c\":");
    json->integer(view.settings.room_setpoint_milli_c);
    json->raw(",\"duration_ms\":");
    json->unsigned_integer(view.settings.duration_ms);
    json->raw(",\"mode\":");
    json->quoted(view.settings.mode == sirokko::Mode::TEMPERATURE ? "TEMPERATURE" : "MANUAL");
    json->raw(",\"manual_percent\":");
    json->unsigned_integer(static_cast<uint32_t>(view.settings.manual_percent));
    json->raw(",\"slew_up_per_s\":");
    json->real(view.settings.slew_up_per_s);
    json->raw(",\"slew_down_per_s\":");
    json->real(view.settings.slew_down_per_s);
    json->raw("}");
    json->raw(",\"timer\":{\"active\":");
    json->boolean(control.duration_active);
    json->raw(",\"remaining_ms\":");
    json->unsigned_integer(control.duration_remaining_ms);
    const uint64_t deadline =
        control.duration_active &&
                control.duration_remaining_ms <= std::numeric_limits<uint64_t>::max() - view.now_ms
            ? view.now_ms + control.duration_remaining_ms
            : 0;
    json->raw(",\"deadline_ms\":");
    json->unsigned_integer(deadline);
    json->raw(",\"saved_duration_ms\":");
    json->unsigned_integer(view.settings.duration_ms);
    json->raw("}");
    json->raw(",\"purge\":{\"active\":");
    json->boolean(control.purge_active);
    json->raw(",\"minimum_remaining_ms\":");
    json->unsigned_integer(control.purge_minimum_remaining_ms);
    json->raw(",\"sensors_fresh\":");
    json->boolean(control.purge_sensors_fresh);
    json->raw(",\"temperature_ready\":");
    json->boolean(control.purge_temperature_ready);
    json->raw("}");
    json->raw(",\"commissioning\":{\"phase\":{\"code\":");
    json->quoted(commissioning_phase_text(control.commissioning.phase).code);
    json->raw(",\"ru\":");
    json->quoted(commissioning_phase_text(control.commissioning.phase).ru);
    json->raw("}");
    json->raw(",\"reason\":{\"code\":");
    json->quoted(commissioning_reason_text(control.commissioning.reason).code);
    json->raw(",\"ru\":");
    json->quoted(commissioning_reason_text(control.commissioning.reason).ru);
    json->raw("}");
    json->raw(",\"block\":{\"code\":");
    json->quoted(sirokko::block_name(control.commissioning.block));
    json->raw(",\"ru\":");
    json->quoted(russian_block(control.commissioning.block));
    json->raw("}");
    json->raw(",\"active\":");
    json->boolean(control.commissioning.active);
    json->raw(",\"target_speed_code\":");
    json->unsigned_integer(control.commissioning.target_speed_code);
    json->raw(",\"accepted_point_count\":");
    json->unsigned_integer(control.commissioning.accepted_point_count);
    json->raw(",\"elapsed_ms\":");
    json->unsigned_integer(control.commissioning.elapsed_ms);
    json->raw(",\"source_envelope_fingerprint\":\"");
    json->hexadecimal(control.commissioning.source_envelope_fingerprint);
    json->raw("\",\"installed_model_fingerprint\":\"");
    json->hexadecimal(control.commissioning.installed_model_fingerprint);
    json->raw("\",\"installed_model_revision\":");
    json->unsigned_integer(control.commissioning.installed_model_revision);
    json->raw("}");
    json->raw(",\"learned_model\":");
    append_model(json, control.learned_model);
    json->raw(",\"envelope\":{\"provisioned\":");
    json->boolean(control.envelope_provisioned);
    json->raw(",\"fingerprint\":\"");
    json->hexadecimal(sirokko::envelope_fingerprint(view.envelope));
    json->raw("\",\"parameters\":{");
    bool first_parameter = true;
    envelope_fields(view.envelope, [&](const char *name, auto value) {
        if (!first_parameter)
            json->character(',');
        first_parameter = false;
        json->quoted(name);
        json->character(':');
        if constexpr (std::is_signed_v<decltype(value)>)
            json->integer(value);
        else
            json->unsigned_integer(value);
    });
    json->raw("},\"limits\":{\"preheat_min_ms\":");
    json->unsigned_integer(sirokko::kPreheatMinimumMs);
    json->raw(",\"preheat_max_ms\":");
    json->unsigned_integer(sirokko::kPreheatMaximumMs);
    json->raw(",\"postpurge_min_ms\":");
    json->unsigned_integer(sirokko::kPostpurgeMinimumMs);
    json->raw(",\"temperature_confirmation_ms\":");
    json->unsigned_integer(sirokko::kTemperatureConfirmationMs);
    json->raw(",\"firing_max_ms\":");
    json->unsigned_integer(sirokko::kFiringStartupMaximumMs);
    json->raw(",\"timer_max_ms\":");
    json->unsigned_integer(sirokko::kMaximumTimerMs);
    json->raw(",\"temperature_max_milli_c\":");
    json->integer(sirokko::kProcessMeasurementCeilingMilliC);
    json->raw(",\"pwm_max\":");
    json->unsigned_integer(sirokko::kPwmMaximum);
    json->raw("}}");
    json->raw(",\"events\":[");
    const size_t event_count = std::min(view.event_count, view.events.size());
    for (size_t index = 0; index < event_count; ++index) {
        const sirokko::ControlEvent &event = view.events[index];
        if (index)
            json->character(',');
        json->raw("{\"at_ms\":");
        json->unsigned_integer(event.at_ms);
        json->raw(",\"kind\":");
        json->quoted(event.kind);
        json->raw(",\"kind_ru\":");
        json->quoted(russian_event(event.kind));
        json->raw(",\"state\":");
        json->quoted(sirokko::state_name(event.state));
        json->raw(",\"state_ru\":");
        json->quoted(russian_state(event.state));
        json->raw(",\"speed_code\":");
        json->unsigned_integer(static_cast<uint32_t>(event.speed_code));
        json->raw(",\"exhaust_milli_c\":");
        json->integer(event.exhaust_milli_c);
        json->raw(",\"air_milli_c\":");
        json->integer(event.air_milli_c);
        json->raw(",\"faults\":");
        json->unsigned_integer(event.faults);
        json->character('}');
    }
    json->raw("]}");
    return json->ok();
}
