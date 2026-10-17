/* SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT */
#include "app_remote.h"
#include "assets/ir_big.h"
#include "assets/ir_small.h"
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <nvs.h>
#include <array>
#include <charconv>

using namespace mooncake;
namespace {
constexpr uint32_t background = 0x171918;
constexpr uint32_t focus_color = 0xffb547;
constexpr uint32_t secondary_color = 0xbec3bb;
constexpr uint32_t divider_color = 0x414640;
constexpr const char* preferences_namespace = "ac_remote";
constexpr const char* preferences_key = "settings";
constexpr uint8_t preferences_schema = 1;
struct ProtocolOption { factory_ir::AcProtocol protocol; const char* label; };
constexpr std::array<ProtocolOption, 3> protocol_options{{
    {factory_ir::AcProtocol::Midea48, "方案A  MIDEA"},
    {factory_ir::AcProtocol::Coolix24, "方案B  COOLIX"},
    {factory_ir::AcProtocol::Bosch144, "方案C 新长码 BOSCH144"},
}};
struct ModeOption { factory_ir::AcMode value; const char* name; };
constexpr std::array<ModeOption, 5> mode_options{{
    {factory_ir::AcMode::Cool, "制冷"}, {factory_ir::AcMode::Dry, "除湿"},
    {factory_ir::AcMode::Auto, "自动"}, {factory_ir::AcMode::Heat, "制热"},
    {factory_ir::AcMode::Fan, "送风"},
}};
struct FanOption { factory_ir::AcFan value; const char* name; };
constexpr std::array<FanOption, 4> fan_options{{
    {factory_ir::AcFan::Auto, "自动"}, {factory_ir::AcFan::Low, "低风"},
    {factory_ir::AcFan::Medium, "中风"}, {factory_ir::AcFan::High, "高风"},
}};
struct ActionOption { factory_ir::CoolixAction action; const char* label; const char* completion; };
constexpr std::array<ActionOption, 6> action_options{{
    {factory_ir::CoolixAction::Swing, "上下摆风 / 切换", "摆风切换已发送，无回读"},
    {factory_ir::CoolixAction::SwingStep, "摆风点动 / 一次", "摆风点动已发送，无回读"},
    {factory_ir::CoolixAction::Sleep, "睡眠 / 切换", "睡眠切换已发送，无回读"},
    {factory_ir::CoolixAction::Turbo, "强力 / 切换", "强力切换已发送，无回读"},
    {factory_ir::CoolixAction::Led, "屏显 / 切换", "屏显切换已发送，无回读"},
    {factory_ir::CoolixAction::Clean, "清洁 / 切换", "清洁切换已发送，无回读"},
}};
constexpr size_t more_count = action_options.size() + 2;
template<class Options, class Value> size_t option_index(const Options& options, Value value)
{
    for (size_t i = 0; i < options.size(); ++i) if (options[i].value == value) return i;
    return 0;
}
size_t protocol_index(factory_ir::AcProtocol protocol)
{
    for (size_t i = 0; i < protocol_options.size(); ++i)
        if (protocol_options[i].protocol == protocol) return i;
    return 0;
}
bool is_up(int code) { return code == KEY_UP || code == KEY_SEMICOLON; }
bool is_down(int code) { return code == KEY_DOWN || code == KEY_DOT; }
bool is_left(int code) { return code == KEY_LEFT || code == KEY_COMMA; }
bool is_right(int code) { return code == KEY_RIGHT || code == KEY_SLASH; }
bool parse_byte(const std::string& input, uint8_t& result)
{
    const auto first = input.find_first_not_of(' ');
    if (first == std::string::npos) return false;
    const auto last = input.find_last_not_of(' ');
    const char* begin = input.data() + first;
    const char* end = input.data() + last + 1;
    unsigned value = 0;
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc() || parsed.ptr != end || value > 255) return false;
    result = static_cast<uint8_t>(value);
    return true;
}
void prepare_canvas()
{
    auto& c = GetHAL().canvas;
    c.fillScreen(background);
    c.setFont(FONT_BASIC);
    c.setTextSize(1);
    c.setTextScroll(false);
    c.setTextColor(TFT_WHITE, background);
}
}
AppRemote::AppRemote()
{
    setAppInfo().name = "Remote";
    setAppInfo().userData = new AppIcon_t(image_data_ir_big, image_data_ir_small);
}
AppRemote::~AppRemote()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}
void AppRemote::onOpen()
{
    _repl_view.reset();
    _view = View::Main;
    _parameter_focus = 0;
    _more_selection = 0;
    _protocol = factory_ir::AcProtocol::Midea48;
    _settings = {};
    _preferences_dirty = false;
    _save_preferences_after_send = false;
    _sending = false;
    _has_nec_command = false;
    _pending_nec_command.clear();
    _status = "设定值，无设备状态回读";
    load_preferences();
    _ready = GetHAL().irInit();
    if (!_ready) _status = "红外初始化失败，OK重试";
    // Opening or navigating the app never transmits an appliance command.
    GetHAL().keyboard.clearKeyEvent();
    render();
    mclog::tagInfo(getAppInfo().name, "ac_open protocol=COOLIX initialized={} no_transmit", _ready);
}
void AppRemote::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) { close(); return; }
    if (_sending) {
        const auto result = GetHAL().irPoll();
        if (result == ESP_OK) {
            _sending = false;
            _status = _completion_status;
            if (_save_preferences_after_send && !save_preferences()) _status = "设置已发送，偏好未保存";
            _save_preferences_after_send = false;
            mclog::tagInfo(getAppInfo().name, "ac_tx_complete elapsed_ms={} appliance_ack=unavailable",
                          GetHAL().millis() - _send_started);
            render();
        } else if (result != ESP_ERR_TIMEOUT || GetHAL().millis() - _send_started >= SEND_TIMEOUT_MS) {
            const auto stopped = GetHAL().irCancel();
            _ready = stopped == ESP_OK;
            _sending = false;
            _save_preferences_after_send = false;
            _status = "红外发送失败，OK重试";
            mclog::tagError(getAppInfo().name, "ac_tx_failed result={} cancel={}", esp_err_to_name(result), esp_err_to_name(stopped));
            render();
        }
    }
    if (_repl_view) _repl_view->update();
    const auto key = GetHAL().keyboard.getLatestKeyEvent();
    if (_sending) return;
    if (key.state && !key.isModifier && (key.keyCode == KEY_TAB || key.keyCode == KEY_ESC)) {
        if (_view == View::Main && key.keyCode == KEY_TAB) open_more();
        else open_main();
        return;
    }
    if (_view == View::Nec) {
        // The real REPL callback runs during HAL keyboard.update(), before
        // this app frame. Defer transmission so G0/page exit always wins.
        if (_has_nec_command) {
            _has_nec_command = false;
            const std::string command = std::move(_pending_nec_command);
            parse_and_send_ir(command);
        }
        return;
    }
    if (!key.state || key.isModifier || GetHAL().keyboard.getModifierMask() != 0) return;
    if (key.keyCode == KEY_ENTER && !_ready) {
        _ready = GetHAL().irInit();
        _status = _ready ? "红外就绪，请再次按键发送" : "红外初始化失败，OK重试";
        render(); return;
    }
    if (_view == View::Main) {
        if (is_up(key.keyCode) || is_down(key.keyCode)) {
            const uint8_t step = is_down(key.keyCode) ? 1 : 2;
            _parameter_focus = (_parameter_focus + step) % 3;
            render();
        } else if (is_left(key.keyCode) || is_right(key.keyCode)) {
            adjust_parameter(is_right(key.keyCode));
        } else if (key.keyCode == KEY_ENTER || key.keyCode == KEY_1) send_state(true);
        else if (key.keyCode == KEY_0) send_state(false);
    } else if (_view == View::More) {
        if (is_up(key.keyCode) || is_down(key.keyCode)) {
            const size_t step = is_down(key.keyCode) ? 1 : more_count - 1;
            _more_selection = (_more_selection + step) % more_count;
            _status = "切换指令，无状态回读";
            render();
        } else if (key.keyCode == KEY_ENTER) choose_more();
    } else if (_view == View::Compatibility) {
        if (is_left(key.keyCode) || is_right(key.keyCode)) {
            const size_t step = is_right(key.keyCode) ? 1 : protocol_options.size() - 1;
            _protocol = protocol_options[(protocol_index(_protocol) + step) % protocol_options.size()].protocol;
            _status = "候选协议，需实测确认";
            render();
        } else if (key.keyCode == KEY_1) send_state(true, true);
        else if (key.keyCode == KEY_0) send_state(false, true);
        else if (key.keyCode == KEY_ENTER) { _status = "测试页只用1开机、0关机"; render(); }
    }
}
void AppRemote::onClose()
{
    _repl_view.reset();
    const auto result = GetHAL().irCancel();
    if (result != ESP_OK) mclog::tagError(getAppInfo().name, "ir close cancellation: {}", esp_err_to_name(result));
    _sending = false;
    _save_preferences_after_send = false;
    _has_nec_command = false;
    _pending_nec_command.clear();
    save_preferences();
}
void AppRemote::render()
{
    if (_view == View::Main) render_main();
    else if (_view == View::More) render_more();
    else if (_view == View::Compatibility) render_compatibility();
}
void AppRemote::render_main()
{
    prepare_canvas();
    auto& c = GetHAL().canvas;
    c.setCursor(3, 0); c.print("美的空调");
    c.setTextColor(secondary_color, background);
    c.setCursor(150, 0); c.print("B COOLIX");
    c.fillRect(3, 14, 198, 1, divider_color);
    const uint32_t temp_bg = _parameter_focus == 0 ? focus_color : background;
    c.fillRect(3, 18, 71, 44, temp_bg);
    c.setTextColor(_parameter_focus == 0 ? background : TFT_WHITE, temp_bg);
    c.setTextSize(2);
    c.setCursor(10, 21);
    c.print(_settings.mode == factory_ir::AcMode::Fan ? "--" : std::to_string(_settings.temperature).c_str());
    c.setTextSize(1);
    c.setCursor(42, 30); c.print(_settings.mode == factory_ir::AcMode::Fan ? "" : "度");
    c.setCursor(9, 48); c.print(_settings.mode == factory_ir::AcMode::Fan ? "送风不设温" : "设定温度");
    const std::array<std::string, 2> values{{
        std::string("模式 ") + mode_options[option_index(mode_options, _settings.mode)].name,
        std::string("风速 ") + fan_options[option_index(fan_options, _settings.fan)].name,
    }};
    for (size_t row = 0; row < values.size(); ++row) {
        const int y = 19 + row * 24;
        const bool selected = _parameter_focus == row + 1;
        const uint32_t color = selected ? focus_color : background;
        c.fillRect(80, y, 121, 20, color);
        c.setTextColor(selected ? background : TFT_WHITE, color);
        c.setCursor(85, y + 4); c.print(values[row].c_str());
    }
    c.setTextColor(focus_color, background);
    c.setCursor(3, 66); c.print(_status.c_str());
    c.setTextColor(secondary_color, background);
    c.setCursor(3, 80); c.print("上下选项 左右调整 G0返回");
    c.setCursor(3, 94); c.print("OK应用/开机 1开 0关 Tab更多");
    GetHAL().pushCanvas();
}
void AppRemote::render_more()
{
    prepare_canvas();
    auto& c = GetHAL().canvas;
    c.setCursor(3, 0); c.print("更多控制");
    const std::string position = std::to_string(_more_selection + 1) + "/" + std::to_string(more_count);
    c.setCursor(180, 0); c.print(position.c_str());
    c.setTextColor(focus_color, background);
    c.setCursor(3, 16); c.print(_status.c_str());
    const size_t first = _more_selection < 4 ? 0 : _more_selection - 3;
    for (size_t row = 0; row < 4 && first + row < more_count; ++row) {
        const size_t item = first + row;
        const bool selected = item == _more_selection;
        const uint32_t color = selected ? focus_color : background;
        const int y = 33 + row * 14;
        c.fillRect(3, y, 198, 13, color);
        c.setTextColor(selected ? background : TFT_WHITE, color);
        c.setCursor(8, y);
        c.print(item < action_options.size() ? action_options[item].label :
                item == action_options.size() ? "兼容测试 A/B/C" : "NEC 手动");
    }
    c.setTextColor(secondary_color, background);
    c.setCursor(3, 94); c.print("上下选择 OK执行 Tab返回");
    GetHAL().pushCanvas();
}
void AppRemote::render_compatibility()
{
    prepare_canvas();
    auto& c = GetHAL().canvas;
    c.setCursor(3, 0); c.print("兼容测试 / 固定26度");
    c.setCursor(3, 18); c.print(protocol_options[protocol_index(_protocol)].label);
    c.setCursor(3, 39); c.print("1  开机 26度制冷 自动风");
    c.setCursor(3, 55); c.print("0  关机");
    c.setTextColor(focus_color, background);
    c.setCursor(3, 74); c.print(_status.c_str());
    c.setTextColor(secondary_color, background);
    c.setCursor(3, 94); c.print("左右切方案 Tab返回主控");
    GetHAL().pushCanvas();
}
void AppRemote::adjust_parameter(bool forward)
{
    if (_parameter_focus == 0) {
        if (_settings.mode == factory_ir::AcMode::Fan) {
            _status = "送风模式不设温"; render(); return;
        }
        const uint8_t next = forward ? (_settings.temperature < factory_ir::kAcMaximumTemperature ? _settings.temperature + 1 : _settings.temperature) :
            (_settings.temperature > factory_ir::kAcMinimumTemperature ? _settings.temperature - 1 : _settings.temperature);
        _preferences_dirty |= next != _settings.temperature;
        _settings.temperature = next;
    } else if (_parameter_focus == 1) {
        const size_t step = forward ? 1 : mode_options.size() - 1;
        _settings.mode = mode_options[(option_index(mode_options, _settings.mode) + step) % mode_options.size()].value;
        _preferences_dirty = true;
    } else {
        const size_t step = forward ? 1 : fan_options.size() - 1;
        _settings.fan = fan_options[(option_index(fan_options, _settings.fan) + step) % fan_options.size()].value;
        _preferences_dirty = true;
    }
    _status = "待发送，OK应用并开机";
    render();
}
void AppRemote::send_state(bool power, bool compatibility)
{
    if (!_ready) { _status = "红外未就绪，OK重试"; render(); return; }
    // A/C tests always receive a fresh fixed state, never the user's draft.
    factory_ir::AcSettings settings = compatibility ? factory_ir::AcSettings{} : _settings;
    settings.power = power;
    const auto protocol = compatibility ? _protocol : factory_ir::AcProtocol::Coolix24;
    std::vector<factory_ir::Pulse> pulses;
    std::string error;
    if (!factory_ir::encodeAc(settings, protocol, pulses, &error)) {
        _status = "红外数据生成失败";
        mclog::tagError(getAppInfo().name, "ac_encode: {}", error);
        render(); return;
    }
    mclog::tagInfo(getAppInfo().name, "ac_state_submit protocol={} power={} temp={} mode={} fan={} appliance_ack=unavailable",
                  static_cast<unsigned>(protocol), power, settings.temperature, static_cast<unsigned>(settings.mode), static_cast<unsigned>(settings.fan));
    submit(pulses, power ? (compatibility ? "26度制冷信号已发出" : "设置已发送，无状态回读") : "关机信号已发送", !compatibility);
}
void AppRemote::send_action(factory_ir::CoolixAction action)
{
    if (!_ready) { _status = "红外未就绪，OK重试"; render(); return; }
    std::vector<factory_ir::Pulse> pulses;
    std::string error;
    if (!factory_ir::encodeCoolixAction(action, pulses, &error)) {
        _status = "红外数据生成失败";
        mclog::tagError(getAppInfo().name, "ac_action_encode: {}", error);
        render(); return;
    }
    for (const auto& option : action_options) {
        if (option.action == action) {
            mclog::tagInfo(getAppInfo().name, "ac_action_submit action={} appliance_ack=unavailable", static_cast<unsigned>(action));
            submit(pulses, option.completion, false);
            return;
        }
    }
}
void AppRemote::submit(const std::vector<factory_ir::Pulse>& pulses, const char* completion, bool save_preferences)
{
    std::vector<uint16_t> durations;
    durations.reserve(pulses.size() * 2);
    for (const auto& pulse : pulses) { durations.push_back(pulse.markUs); durations.push_back(pulse.spaceUs); }
    const auto result = GetHAL().irSendPulses(durations.data(), durations.size());
    if (result != ESP_OK) {
        _status = "发送未开始，请稍后重试";
        mclog::tagError(getAppInfo().name, "ac_tx_submit: {}", esp_err_to_name(result));
    } else {
        _sending = true;
        _send_started = GetHAL().millis();
        _completion_status = completion;
        _save_preferences_after_send = save_preferences;
        _status = "发送中，请稍候…";
        mclog::tagInfo(getAppInfo().name, "ac_tx_start pulses={} carrier_hz=38000 appliance_ack=unavailable", pulses.size());
    }
    // Transport owns a copy; temporary vectors can now be released.
    render();
}
void AppRemote::open_main()
{
    _repl_view.reset();
    _has_nec_command = false;
    _pending_nec_command.clear();
    _view = View::Main;
    _status = "设定值，无设备状态回读";
    GetHAL().keyboard.clearKeyEvent();
    render();
}
void AppRemote::open_more()
{
    _view = View::More;
    _more_selection = 0;
    _status = "切换指令，无状态回读";
    GetHAL().keyboard.clearKeyEvent();
    render();
}
void AppRemote::choose_more()
{
    if (_more_selection < action_options.size()) {
        send_action(action_options[_more_selection].action);
    } else if (_more_selection == action_options.size()) {
        _view = View::Compatibility;
        _protocol = factory_ir::AcProtocol::Midea48;
        _status = "未验证，请对准空调";
        GetHAL().keyboard.clearKeyEvent();
        render();
    } else open_nec();
}
void AppRemote::open_nec()
{
    _view = View::Nec;
    _repl_view = std::make_unique<ReplView>();
    _repl_view->onRenderTips = [] {
        auto& c = GetHAL().canvas;
        c.setTextColor(TFT_ORANGE);
        c.println("NEC 手动遥控");
        c.println("地址,命令  十进制0~255");
        c.println("如16,1  回车发送");
        c.println("Tab主控  G0返回原厂");
    };
    _repl_view->onCommand = [this](const std::string& command) {
        _pending_nec_command = command;
        _has_nec_command = true;
    };
    _repl_view->autoClearPrompt = false;
    _repl_view->init();
    GetHAL().keyboard.clearKeyEvent();
}
void AppRemote::parse_and_send_ir(const std::string& command)
{
    const auto comma = command.find(',');
    uint8_t address = 0, code = 0;
    const bool valid = comma != std::string::npos && command.find(',', comma + 1) == std::string::npos &&
        parse_byte(command.substr(0, comma), address) && parse_byte(command.substr(comma + 1), code);
    if (!valid) { _repl_view->showMessage("格式错误，请输入地址,命令", TFT_RED); return; }
    if (!GetHAL().irSend(address, code)) { _repl_view->showMessage("红外发送失败，请稍后重试", TFT_RED); return; }
    _repl_view->showMessage("信号已发出，需观察设备响应", TFT_GREEN);
}
void AppRemote::load_preferences()
{
    nvs_handle_t handle = 0;
    auto result = nvs_open(preferences_namespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return;
    if (result != ESP_OK) {
        _status = "偏好读取失败，暂用默认";
        mclog::tagError(getAppInfo().name, "ac preferences open: {}", esp_err_to_name(result)); return;
    }
    std::array<uint8_t, 4> bytes{};
    size_t size = bytes.size();
    result = nvs_get_blob(handle, preferences_key, bytes.data(), &size);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) return;
    if (result != ESP_OK || size != bytes.size() || bytes[0] != preferences_schema ||
        bytes[1] < factory_ir::kAcMinimumTemperature || bytes[1] > factory_ir::kAcMaximumTemperature ||
        bytes[2] >= mode_options.size() || bytes[3] >= fan_options.size()) {
        _status = "偏好无效，暂用默认";
        mclog::tagError(getAppInfo().name, "ac preferences invalid result={} size={}", esp_err_to_name(result), size); return;
    }
    _settings.temperature = bytes[1];
    _settings.mode = mode_options[bytes[2]].value;
    _settings.fan = fan_options[bytes[3]].value;
}
bool AppRemote::save_preferences()
{
    if (!_preferences_dirty) return true;
    const std::array<uint8_t, 4> bytes{{preferences_schema, _settings.temperature,
        static_cast<uint8_t>(option_index(mode_options, _settings.mode)),
        static_cast<uint8_t>(option_index(fan_options, _settings.fan))}};
    nvs_handle_t handle = 0;
    auto result = nvs_open(preferences_namespace, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_blob(handle, preferences_key, bytes.data(), bytes.size());
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
    }
    if (result != ESP_OK) {
        mclog::tagError(getAppInfo().name, "ac preferences save: {}", esp_err_to_name(result));
        return false;
    }
    _preferences_dirty = false;
    return true;
}
