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
    case sirokko::State::TRIAL_READY:
        return "Первый запуск: удерживайте KEY0";
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
    case sirokko::Block::RECOVERY_PURGE:
        return "Дождитесь охлаждения отопителя";
    case sirokko::Block::SENSOR_INVALID:
        return "Нет достоверных показаний нужных для запуска датчиков температуры";
    case sirokko::Block::SENSOR_STALE:
        return "Данные датчиков устарели";
    case sirokko::Block::INITIAL_THERMAL_STATE:
        return "Выхлоп ещё не остыл до температуры завершения охлаждения";
    case sirokko::Block::FAULT_LATCHED:
        return "Запуск заблокирован до сброса аварии";
    case sirokko::Block::BUSY:
        return "Команда недоступна на текущем этапе работы";
    case sirokko::Block::MAINTENANCE:
        return "Обновление прошивки";
    case sirokko::Block::LOCAL_CONTROLS_UNAVAILABLE:
        return "Не удалось запустить драйвер экрана или кнопки";
    case sirokko::Block::BUTTON_HELD:
        return "Отпустите KEY0 перед подготовкой первого запуска";
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

const char *russian_event(const char *kind) {
    if (!kind)
        return "Неизвестное событие";
    if (std::strcmp(kind, "PROFILE_SAVED") == 0)
        return "Параметры отопителя сохранены";
    if (std::strcmp(kind, "TRIAL_ARMED") == 0)
        return "Первый запуск подготовлен";
    if (std::strcmp(kind, "TRIAL_START") == 0)
        return "Первый запуск начат удержанием KEY0";
    if (std::strcmp(kind, "TRIAL_CONFIRMED") == 0)
        return "Горение подтверждено пользователем";
    if (std::strcmp(kind, "TRIAL_PWM_CHANGED") == 0)
        return "ШИМ двигателя изменён при первом запуске";
    if (std::strcmp(kind, "TRIAL_EXPIRED") == 0)
        return "Первый запуск отменён: KEY0 не нажата";
    if (std::strcmp(kind, "TRIAL_UNCONFIRMED") == 0)
        return "Первый запуск остановлен: горение не подтверждено";
    if (std::strcmp(kind, "TRIAL_FINISHED") == 0)
        return "Первый запуск завершён по времени";
    if (std::strcmp(kind, "START") == 0)
        return "Запуск отопителя";
    if (std::strcmp(kind, "STOP") == 0)
        return "Остановка отопителя";
    if (std::strcmp(kind, "FAULT") == 0)
        return "Авария";
    if (std::strcmp(kind, "COMBUSTION_SUSPECT") == 0)
        return "Проверка падения температуры выхлопа";
    if (std::strcmp(kind, "TIMER_EXPIRED") == 0)
        return "Время таймера истекло";
    if (std::strcmp(kind, "TIMER_ARMED") == 0)
        return "Таймер отключения установлен";
    if (std::strcmp(kind, "TIMER_CANCELLED") == 0)
        return "Таймер отключения отменён";
    return "Неизвестное событие";
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
        {sirokko::FAULT_IGNITION_TIMEOUT, "За время розжига выхлоп не прогрелся до заданной температуры"},
        {sirokko::FAULT_FLAME_LOST, "Выхлоп остыл ниже минимальной рабочей температуры"},
        {sirokko::FAULT_COMBUSTION_DROP, "Температура выхлопа устойчиво падает при неизменном ШИМ двигателя"},
        {sirokko::FAULT_CONTROL_WATCHDOG, "Контроллер не выполнил цикл управления вовремя"},
        {sirokko::FAULT_STORAGE, "Ошибка чтения или записи настроек"},
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
    json->raw(",\"trial\":{\"active\":");
    json->boolean(control.trial_active);
    json->raw(",\"remaining_ms\":");
    json->unsigned_integer(control.trial_remaining_ms);
    json->raw(",\"elapsed_ms\":");
    json->unsigned_integer(control.trial_elapsed_ms);
    json->character('}');
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
    json->boolean(view.envelope.provisioned || control.trial_active);
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
    json->raw(",\"ignition_glow_ms\":");
    json->unsigned_integer(sirokko::kIgnitionGlowMs);
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
