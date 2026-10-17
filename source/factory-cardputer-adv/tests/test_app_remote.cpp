// Drive production AppRemote, its real encoders, and async boundary fakes.
#include <apps/app_remote/app_remote.h>
#include <nvs.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static FakeHal hal;
FakeHal& GetHAL() { return hal; }
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool screen(const char* value) { return hal.canvas.text.find(value) != std::string::npos; }
struct Session {
    AppRemote app;
    Session() { hal = {}; remote_nvs_test::state = {}; }
    ~Session() { app.onClose(); }
    void open() { app.onOpen(); }
};
void tick(AppRemote& app, uint32_t elapsed = 1) { hal.now += elapsed; hal.update(); app.onRunning(); }
void press(AppRemote& app, int key, bool is_modifier = false) { hal.keyboard.press(key, is_modifier); tick(app); }
std::vector<uint16_t> flatten(const std::vector<factory_ir::Pulse>& pulses) {
    std::vector<uint16_t> values;
    for (const auto& p : pulses) { values.push_back(p.markUs); values.push_back(p.spaceUs); }
    return values;
}
std::vector<uint16_t> expected(factory_ir::AcProtocol protocol, bool power, uint8_t temperature = 26,
        factory_ir::AcMode mode = factory_ir::AcMode::Cool, factory_ir::AcFan fan = factory_ir::AcFan::Auto) {
    factory_ir::AcSettings state;
    state.power = power; state.temperature = temperature; state.mode = mode; state.fan = fan;
    std::vector<factory_ir::Pulse> pulses;
    check(factory_ir::encodeAc(state, protocol, pulses), "valid UI setting rejected by encoder");
    return flatten(pulses);
}
void complete(AppRemote& app) { hal.poll_result = ESP_OK; tick(app); hal.poll_result = ESP_ERR_TIMEOUT; }
void more(AppRemote& app, int selection) {
    press(app, KEY_TAB);
    for (int i = 0; i < selection; ++i) press(app, KEY_DOWN);
}
void compatibility(AppRemote& app) { more(app, 6); press(app, KEY_ENTER); }
void nec(AppRemote& app) { more(app, 7); press(app, KEY_ENTER); check(ReplView::active, "NEC page did not open"); }
void submit_nec_command(AppRemote& app, const std::string& value) { ReplView::active->onCommand(value); tick(app); }
void expect_last(const std::vector<uint16_t>& wanted) {
    check(!hal.submitted.empty() && hal.submitted.back() == wanted, "UI routed wrong state, action, or protocol");
}
void load_fixture(std::vector<uint8_t> bytes) { remote_nvs_test::state.namespaces["ac_remote"]["settings"] = std::move(bytes); }

void launchConsumesKeyWithoutSending() {
    Session s; hal.keyboard.press(KEY_ENTER); hal.update();
    const auto clears = hal.keyboard.clear_calls;
    s.open();
    check(hal.keyboard.clear_calls == clears + 1 && !hal.keyboard.getLatestKeyEvent().state, "launch event was not cleared");
    tick(s.app);
    check(hal.init_calls == 1 && hal.submitted.empty() && hal.nec.empty(), "opening page transmitted IR");
    check(screen("B COOLIX") && screen("无设备状态回读") && screen("OK应用/开机"), "default COOLIX page or truthful hint missing");
    bool large_temperature = false;
    for (const auto& p : hal.canvas.printed) large_temperature |= p.text == "26" && p.size == 2;
    check(large_temperature, "temperature is not visibly enlarged");
    check(remote_nvs_test::state.written.empty(), "opening page wrote preferences");
}
void explicitOkAndPowerUseCoolix() {
    Session s; s.open();
    press(s.app, KEY_ENTER); expect_last(expected(factory_ir::AcProtocol::Coolix24, true));
    check(screen("发送中") && !screen("设置已发送"), "submission falsely reported completion");
    complete(s.app); check(screen("设置已发送，无状态回读") && !screen("空调已开"), "completion claimed appliance acknowledgement");
    press(s.app, KEY_0); expect_last(expected(factory_ir::AcProtocol::Coolix24, false));
    complete(s.app); check(screen("关机信号已发送"), "off completion missing");
    press(s.app, KEY_1); expect_last(expected(factory_ir::AcProtocol::Coolix24, true));
    check(hal.submitted.size() == 3, "explicit commands missing or duplicated");
}
void navigationEditsWithoutTransmitting() {
    Session s; s.open();
    press(s.app, KEY_SLASH); press(s.app, KEY_LEFT); press(s.app, KEY_COMMA); // 25
    press(s.app, KEY_DOT); press(s.app, KEY_RIGHT); check(screen("模式 除湿"), "mode focus or next mode wrong");
    press(s.app, KEY_SLASH); check(screen("模式 自动"), "second mode missing");
    press(s.app, KEY_LEFT); press(s.app, KEY_COMMA); check(screen("模式 制冷"), "mode reverse order wrong");
    press(s.app, KEY_SEMICOLON); press(s.app, KEY_DOWN); press(s.app, KEY_DOWN);
    for (const char* name : {"风速 低风", "风速 中风", "风速 高风", "风速 自动"}) {
        press(s.app, KEY_RIGHT); check(screen(name), "fan order or wrap incorrect");
    }
    press(s.app, KEY_LEFT); check(screen("风速 高风"), "fan reverse wrap incorrect");
    press(s.app, KEY_RIGHT);
    check(hal.submitted.empty(), "navigation sent commands before explicit apply");
    press(s.app, KEY_ENTER); expect_last(expected(factory_ir::AcProtocol::Coolix24, true, 25));
}
void temperatureClampsToSupportedBounds() {
    Session s; s.open();
    for (int i = 0; i < 50; ++i) press(s.app, KEY_LEFT);
    check(screen("17"), "temperature below minimum or display wrong");
    press(s.app, KEY_ENTER); expect_last(expected(factory_ir::AcProtocol::Coolix24, true, 17)); complete(s.app);
    for (int i = 0; i < 50; ++i) press(s.app, KEY_SLASH);
    check(screen("30"), "temperature above maximum or display wrong");
    press(s.app, KEY_1); expect_last(expected(factory_ir::AcProtocol::Coolix24, true, 30));
    check(hal.submitted.size() == 2, "clamping navigation transmitted");
}
void allModeFanSelectionsReachTheEncoder() {
    const std::array<factory_ir::AcMode, 5> modes{{factory_ir::AcMode::Cool, factory_ir::AcMode::Dry,
        factory_ir::AcMode::Auto, factory_ir::AcMode::Heat, factory_ir::AcMode::Fan}};
    const std::array<factory_ir::AcFan, 4> fans{{factory_ir::AcFan::Auto, factory_ir::AcFan::Low,
        factory_ir::AcFan::Medium, factory_ir::AcFan::High}};
    for (size_t mode = 0; mode < modes.size(); ++mode) for (size_t fan = 0; fan < fans.size(); ++fan) {
        Session s; s.open(); press(s.app, KEY_DOWN);
        for (size_t i = 0; i < mode; ++i) press(s.app, KEY_RIGHT);
        press(s.app, KEY_DOWN);
        for (size_t i = 0; i < fan; ++i) press(s.app, KEY_RIGHT);
        check(hal.submitted.empty(), "mode/fan edit transmitted automatically");
        press(s.app, KEY_ENTER); expect_last(expected(factory_ir::AcProtocol::Coolix24, true, 26, modes[mode], fans[fan]));
        complete(s.app);
    }
}
void fanModeDisablesTemperatureButPreservesPreference() {
    Session s; s.open(); press(s.app, KEY_RIGHT); // 27
    press(s.app, KEY_DOWN); press(s.app, KEY_LEFT); // Cool -> Fan
    check(screen("模式 送风") && screen("--") && screen("送风不设温"), "fan mode displays a false temperature");
    press(s.app, KEY_UP); press(s.app, KEY_RIGHT); press(s.app, KEY_LEFT);
    check(screen("送风模式不设温"), "fan mode temperature edit not explained");
    press(s.app, KEY_ENTER); expect_last(expected(factory_ir::AcProtocol::Coolix24, true, 27, factory_ir::AcMode::Fan));
    complete(s.app); press(s.app, KEY_DOWN); press(s.app, KEY_RIGHT); // Fan -> Cool
    check(screen("模式 制冷") && screen("27") && !screen("--"), "returning to cool lost temperature preference");
    press(s.app, KEY_1); expect_last(expected(factory_ir::AcProtocol::Coolix24, true, 27));
}
void modifiersAndReleaseIgnored() {
    Session s; s.open(); press(s.app, KEY_1, true);
    hal.keyboard.modifiers = 1;
    for (int key : {KEY_ENTER, KEY_0, KEY_1, KEY_LEFT, KEY_DOWN}) press(s.app, key);
    hal.keyboard.modifiers = 0;
    hal.keyboard.release(KEY_1); tick(s.app);
    check(hal.submitted.empty(), "modifier, release, or navigation caused transmission");
    press(s.app, KEY_1); expect_last(expected(factory_ir::AcProtocol::Coolix24, true));
}
void busySuppressesAllOperations() {
    Session s; s.open(); press(s.app, KEY_ENTER);
    for (int key : {KEY_0, KEY_1, KEY_LEFT, KEY_RIGHT, KEY_COMMA, KEY_SLASH, KEY_UP, KEY_DOWN, KEY_TAB, KEY_ESC, KEY_ENTER}) press(s.app, key);
    check(hal.submitted.size() == 1 && !ReplView::active && screen("B COOLIX") && screen("发送中"), "busy UI accepted another operation");
    tick(s.app, 2480); check(hal.cancel_calls == 0, "transmission cancelled before deadline");
    complete(s.app); check(hal.submitted.size() == 1 && screen("设置已发送"), "busy key was reused on completion");
    press(s.app, KEY_ENTER); expect_last(expected(factory_ir::AcProtocol::Coolix24, true));
}
void timeoutCancellationAndRetry() {
    Session s; s.open(); press(s.app, KEY_1); tick(s.app, 2500);
    check(hal.cancel_calls == 1 && screen("红外发送失败"), "timeout did not cancel/report failure");
    press(s.app, KEY_ENTER); check(hal.submitted.size() == 2, "timeout could not be retried");
}
void pollAndCancelFailureRequireReinitialization() {
    Session s; s.open(); press(s.app, KEY_1);
    hal.poll_result = ESP_FAIL; hal.cancel_result = ESP_FAIL; tick(s.app);
    check(hal.cancel_calls == 1 && screen("红外发送失败"), "poll error not cancelled");
    press(s.app, KEY_0); check(hal.submitted.size() == 1 && screen("红外未就绪"), "failed cancellation allowed new submission");
    hal.cancel_result = ESP_OK; hal.poll_result = ESP_ERR_TIMEOUT; press(s.app, KEY_ENTER);
    check(hal.init_calls == 2 && hal.submitted.size() == 1 && screen("请再次按键发送"), "initialization retry unexpectedly sent command");
    press(s.app, KEY_ENTER); check(hal.submitted.size() == 2, "reinitialized transport could not send");
}
void initializationAndSubmissionFailures() {
    Session s; hal.init_ok = false; s.open(); press(s.app, KEY_1);
    check(hal.submitted.empty() && screen("红外未就绪"), "failed init still sent IR");
    hal.init_ok = true; press(s.app, KEY_ENTER); check(hal.submitted.empty(), "init retry sent IR");
    hal.submit_result = ESP_ERR_NO_MEM; press(s.app, KEY_ENTER);
    check(screen("发送未开始") && hal.poll_calls == 0, "rejected submission entered wait");
    hal.submit_result = ESP_OK; press(s.app, KEY_1);
    check(hal.submitted.size() == 2 && screen("发送中"), "submit failure could not retry");
}
void homeCancelsBeforePollAndKeys() {
    Session s; s.open(); press(s.app, KEY_1);
    hal.homeButton.clicked = true; hal.keyboard.press(KEY_0); tick(s.app);
    check(s.app.closed && hal.cancel_calls == 1 && hal.poll_calls == 0 && hal.submitted.size() == 1, "G0 not prioritized over poll and keyboard");
}
void idleHomeDoesNotTransmit() {
    Session s; s.open(); hal.homeButton.clicked = true; tick(s.app);
    check(s.app.closed && hal.submitted.empty() && hal.nec.empty(), "idle G0 transmitted");
}
void getterDoesNotConsumeAndIdleFrameClears() {
    Session s; s.open(); hal.keyboard.press(KEY_0); hal.update();
    const auto& first = hal.keyboard.getLatestKeyEvent(); const auto& second = hal.keyboard.getLatestKeyEvent();
    check(&first == &second && first.state && first.keyCode == KEY_0, "getter consumed or copied event");
    s.app.onRunning(); check(hal.submitted.size() == 1 && second.keyCode == KEY_0, "UI consumed latest within a frame");
    tick(s.app);
    check(!hal.keyboard.getLatestKeyEvent().state && hal.keyboard.getLatestKeyEvent().keyCode == KEY_NONE,
          "idle frame not fully invalidated like hardened production keyboard");
    hal.keyboard.release(KEY_0); tick(s.app);
    check(!hal.keyboard.getLatestKeyEvent().state && hal.keyboard.getLatestKeyEvent().keyCode == KEY_0, "queued release lost");
}
void onePressDoesNotRepeatAcrossCompletion() {
    Session s; s.open(); press(s.app, KEY_1);
    for (int i = 0; i < 16; ++i) tick(s.app, 50);
    check(hal.submitted.size() == 1, "single press repeated while busy");
    complete(s.app);
    for (int i = 0; i < 80; ++i) tick(s.app, 50);
    check(hal.submitted.size() == 1 && screen("设置已发送"), "old press reused after completion");
    hal.keyboard.release(KEY_1); tick(s.app); press(s.app, KEY_1);
    check(hal.submitted.size() == 2, "new press did not send after completion");
}
void moreMenuScrollsWithoutSending() {
    Session s; s.open(); press(s.app, KEY_TAB);
    check(screen("更多控制") && screen("切换指令，无状态回读") && screen("1/8"), "more menu missing disclosure/focus");
    press(s.app, KEY_1); press(s.app, KEY_0); press(s.app, KEY_UP);
    check(screen("8/8") && screen("NEC 手动"), "more menu did not scroll/wrap upward");
    press(s.app, KEY_DOT); check(screen("1/8"), "more menu did not wrap downward");
    press(s.app, KEY_ESC); check(screen("B COOLIX") && hal.submitted.empty(), "Esc did not safely return to main");
}
void sixActionsSendToggleCommandsWithoutInventedState() {
    const std::array<factory_ir::CoolixAction, 6> actions{{factory_ir::CoolixAction::Swing, factory_ir::CoolixAction::SwingStep,
        factory_ir::CoolixAction::Sleep, factory_ir::CoolixAction::Turbo, factory_ir::CoolixAction::Led, factory_ir::CoolixAction::Clean}};
    for (size_t index = 0; index < actions.size(); ++index) {
        Session s; s.open(); more(s.app, index); check(hal.submitted.empty(), "action navigation transmitted");
        std::vector<factory_ir::Pulse> pulses;
        check(factory_ir::encodeCoolixAction(actions[index], pulses), "action encode failed");
        press(s.app, KEY_ENTER); expect_last(flatten(pulses));
        press(s.app, KEY_ENTER); press(s.app, KEY_DOWN); check(hal.submitted.size() == 1, "busy action resubmitted or changed selection");
        complete(s.app); check(screen("已发送，无回读") && !screen("已开启") && !screen("已关闭"), "action invented toggle state");
        press(s.app, KEY_ENTER); expect_last(flatten(pulses)); check(hal.submitted.size() == 2, "second toggle was suppressed or changed into absolute state");
        check(remote_nvs_test::state.written.empty(), "toggle truth was persisted");
    }
}
void compatibilityIsFixedAndDoesNotPolluteMainDraft() {
    Session s; s.open();
    for (int i = 0; i < 4; ++i) press(s.app, KEY_RIGHT); // 30
    press(s.app, KEY_DOWN); press(s.app, KEY_LEFT); press(s.app, KEY_LEFT); // Heat
    press(s.app, KEY_DOWN); press(s.app, KEY_LEFT); // High
    compatibility(s.app); check(screen("方案A") && hal.submitted.empty(), "test page did not default A without sending");
    for (const auto protocol : {factory_ir::AcProtocol::Midea48, factory_ir::AcProtocol::Coolix24, factory_ir::AcProtocol::Bosch144}) {
        press(s.app, KEY_1); expect_last(expected(protocol, true)); complete(s.app);
        press(s.app, KEY_0); expect_last(expected(protocol, false)); complete(s.app);
        press(s.app, KEY_RIGHT);
    }
    check(remote_nvs_test::state.written.empty(), "compatibility test persisted test settings");
    press(s.app, KEY_TAB); check(screen("B COOLIX") && screen("模式 制热") && screen("风速 高风"), "test page polluted main draft");
    press(s.app, KEY_ENTER); expect_last(expected(factory_ir::AcProtocol::Coolix24, true, 30, factory_ir::AcMode::Heat, factory_ir::AcFan::High));
}
void compatibilityNavigationCyclesBothDirections() {
    Session s; s.open(); compatibility(s.app);
    for (const auto& step : std::vector<std::pair<int, const char*>>{{KEY_LEFT, "方案C"}, {KEY_COMMA, "方案B"},
            {KEY_LEFT, "方案A"}, {KEY_COMMA, "方案C"}, {KEY_RIGHT, "方案A"}, {KEY_SLASH, "方案B"}, {KEY_RIGHT, "方案C"}, {KEY_SLASH, "方案A"}}) {
        press(s.app, step.first); check(screen(step.second), "compatibility navigation/wrap order incorrect");
    }
    press(s.app, KEY_ENTER); check(hal.submitted.empty(), "OK on compatibility page transmitted");
    press(s.app, KEY_ESC); check(screen("B COOLIX"), "compatibility Esc did not return main");
}
void manualNecAcceptsStrictDecimal() {
    Session s; s.open(); nec(s.app); check(!ReplView::active->autoClearPrompt && hal.submitted.empty(), "NEC entry transmitted");
    for (const auto& pair : std::vector<std::pair<std::string, std::pair<uint8_t, uint8_t>>>{{"16,1", {16,1}}, {" 0 , 255 ", {0,255}}, {"255,0", {255,0}}, {"0001,002", {1,2}}}) {
        submit_nec_command(s.app, pair.first);
        check(hal.nec.back() == pair.second && ReplView::active->color == TFT_GREEN, "valid decimal NEC failed");
        check(ReplView::active->message.find("需观察设备响应") != std::string::npos, "NEC claimed appliance acknowledgement");
    }
}
void manualNecRejectsMalformedInput() {
    Session s; s.open(); nec(s.app);
    for (const std::string command : {"", " ", ",", "1", "1,", ",1", "1,2,", "1,,2", "1,2,3", "256,1", "1,256", "-1,2", "+1,2", "0x10,1", "1.0,2", "1,2junk", "1,2\n", "1,\t2", "429496729600,1", "中,1"}) {
        submit_nec_command(s.app, command);
        check(hal.nec.empty() && hal.submitted.empty() && ReplView::active->color == TFT_RED, "malformed NEC transmitted");
    }
    submit_nec_command(s.app, "16,1"); check(hal.nec.size() == 1, "invalid command poisoned next valid command");
}
void manualNecFailureAndSafeReturn() {
    Session s; s.open(); nec(s.app); hal.nec_ok = false; submit_nec_command(s.app, "16,1");
    check(ReplView::active->color == TFT_RED && ReplView::active->message.find("发送失败") != std::string::npos, "NEC failure reported success");
    press(s.app, KEY_ESC); check(!ReplView::active && screen("B COOLIX") && hal.submitted.empty(), "NEC Esc return transmitted");
    nec(s.app); press(s.app, KEY_TAB);
    check(!ReplView::active && screen("B COOLIX") && hal.nec.size() == 1, "NEC Tab return transmitted");
}
void pendingManualCommandIsCancelledByPageExitOrHome() {
    for (bool use_home : {true, false}) {
        Session s; s.open(); nec(s.app);
        ReplView::active->onCommand("16,1");
        check(hal.nec.empty(), "REPL callback transmitted before prioritized app frame");
        if (use_home) hal.homeButton.clicked = true;
        else hal.keyboard.press(KEY_TAB);
        tick(s.app);
        check(hal.nec.empty() && !ReplView::active, "pending NEC command beat page exit or G0");
    }
}
void preferencesLoadValidSettingsWithoutSending() {
    Session s; load_fixture({1, 22, 3, 2}); s.open();
    check(screen("22") && screen("模式 制热") && screen("风速 中风") && hal.submitted.empty(), "valid preferences not loaded");
    check(remote_nvs_test::state.handles.empty() && remote_nvs_test::state.written.empty(), "NVS read leaked handle or wrote");
    press(s.app, KEY_0); expect_last(expected(factory_ir::AcProtocol::Coolix24, false, 22, factory_ir::AcMode::Heat, factory_ir::AcFan::Medium));
}
void preferencesRoundTripAndNamespaceIsolation() {
    Session s; s.open();
    auto& nv = remote_nvs_test::state;
    nv.namespaces["wifi_private"]["secret"] = {99, 88};
    press(s.app, KEY_LEFT); press(s.app, KEY_DOWN); press(s.app, KEY_RIGHT); press(s.app, KEY_DOWN); press(s.app, KEY_RIGHT);
    check(nv.commits == 0, "every edit writes flash instead of deferring");
    press(s.app, KEY_ENTER); complete(s.app);
    check(nv.namespaces["ac_remote"]["settings"] == std::vector<uint8_t>({1,25,1,1}) && nv.commits == 1, "schema1 preferences not atomically saved");
    s.app.onClose(); check(nv.commits == 1, "unchanged close rewrote NVS");
    hal = {}; AppRemote rebooted; rebooted.onOpen();
    check(screen("25") && screen("模式 除湿") && screen("风速 低风") && hal.submitted.empty(), "reboot did not restore three preferences safely");
    press(rebooted, KEY_ENTER); expect_last(expected(factory_ir::AcProtocol::Coolix24, true, 25, factory_ir::AcMode::Dry, factory_ir::AcFan::Low));
    rebooted.onClose();
    check(nv.namespaces["wifi_private"]["secret"] == std::vector<uint8_t>({99,88}), "unrelated namespace changed");
    for (const auto& name : nv.opened) check(name == "ac_remote", "opened unrelated NVS namespace");
    for (const auto& name : nv.written) check(name == "ac_remote/settings", "wrote unrelated NVS key");
    check(nv.handles.empty(), "NVS handle leaked");
}
void preferencesSaveOnExitWithoutTransmitting() {
    Session s; s.open(); press(s.app, KEY_RIGHT); s.app.onClose();
    check(hal.submitted.empty() && remote_nvs_test::state.namespaces["ac_remote"]["settings"] == std::vector<uint8_t>({1,27,0,0}),
          "unsent preference was not saved on close or close transmitted");
}
void invalidPreferencesFallBackAsAUnit() {
    for (const auto& invalid : std::vector<std::vector<uint8_t>>{{}, {1,22,3}, {1,22,3,2,9}, {2,22,3,2}, {1,16,3,2},
            {1,31,3,2}, {1,22,5,2}, {1,22,3,4}, {255,255,255,255}}) {
        Session s; load_fixture(invalid); s.open();
        check(screen("偏好无效") && screen("26") && screen("模式 制冷") && screen("风速 自动"), "invalid preference partially loaded");
        check(remote_nvs_test::state.written.empty(), "load erased/replaced invalid NVS data");
        press(s.app, KEY_ENTER); expect_last(expected(factory_ir::AcProtocol::Coolix24, true));
    }
}
void preferenceReadFailuresDoNotBlockControl() {
    for (bool fail_open : {true, false}) {
        Session s; load_fixture({1,22,3,2});
        if (fail_open) remote_nvs_test::state.open_error = ESP_FAIL;
        else remote_nvs_test::state.get_error = ESP_FAIL;
        s.open(); check(screen(fail_open ? "偏好读取失败" : "偏好无效"), "NVS read failure not shown");
        press(s.app, KEY_1); expect_last(expected(factory_ir::AcProtocol::Coolix24, true));
        check(remote_nvs_test::state.handles.empty(), "failed NVS read leaked handle");
    }
}
void preferenceWriteFailuresDoNotBlockAndCanRetry() {
    for (int failure = 0; failure < 3; ++failure) {
        Session s; load_fixture({1,26,0,0}); s.open(); press(s.app, KEY_RIGHT);
        auto& nv = remote_nvs_test::state;
        if (failure == 0) nv.open_error = ESP_FAIL;
        if (failure == 1) nv.set_error = ESP_FAIL;
        if (failure == 2) nv.commit_error = ESP_FAIL;
        press(s.app, KEY_ENTER); complete(s.app);
        check(screen("设置已发送，偏好未保存") && nv.namespaces["ac_remote"]["settings"] == std::vector<uint8_t>({1,26,0,0}),
              "NVS failure hid successful send or corrupted saved settings");
        nv.open_error = nv.set_error = nv.commit_error = ESP_OK;
        press(s.app, KEY_ENTER); complete(s.app);
        check(nv.namespaces["ac_remote"]["settings"] == std::vector<uint8_t>({1,27,0,0}), "failed preference save could not retry");
        check(nv.handles.empty(), "NVS write failure leaked handle");
    }
}
}
int main() {
    try {
        launchConsumesKeyWithoutSending(); explicitOkAndPowerUseCoolix(); navigationEditsWithoutTransmitting();
        temperatureClampsToSupportedBounds(); allModeFanSelectionsReachTheEncoder(); fanModeDisablesTemperatureButPreservesPreference();
        modifiersAndReleaseIgnored(); busySuppressesAllOperations(); timeoutCancellationAndRetry();
        pollAndCancelFailureRequireReinitialization(); initializationAndSubmissionFailures(); homeCancelsBeforePollAndKeys();
        idleHomeDoesNotTransmit(); getterDoesNotConsumeAndIdleFrameClears(); onePressDoesNotRepeatAcrossCompletion();
        moreMenuScrollsWithoutSending(); sixActionsSendToggleCommandsWithoutInventedState();
        compatibilityIsFixedAndDoesNotPolluteMainDraft(); compatibilityNavigationCyclesBothDirections();
        manualNecAcceptsStrictDecimal(); manualNecRejectsMalformedInput(); manualNecFailureAndSafeReturn();
        pendingManualCommandIsCancelledByPageExitOrHome();
        preferencesLoadValidSettingsWithoutSending(); preferencesRoundTripAndNamespaceIsolation(); preferencesSaveOnExitWithoutTransmitting();
        invalidPreferencesFallBackAsAUnit(); preferenceReadFailuresDoNotBlockControl(); preferenceWriteFailuresDoNotBlockAndCanRetry();
        std::cout << "PASS: 29 production AppRemote UI/routing/lifecycle/NVS test groups under ASan/UBSan\n";
        std::cout << "Includes all 20 mode/fan selections, 6 repeated actions, 3 compatibility protocols, NVS faults and reboot restore.\n";
        std::cout << "Host fakes do not establish physical IR emission or appliance compatibility.\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
