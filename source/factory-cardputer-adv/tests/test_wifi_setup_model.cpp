#include "apps/app_set_wifi/WifiSetupModel.h"
#include <cassert>
#include <iostream>
using WifiSetup::Model;

int main()
{
    Model m;
    m.loadSaved("Home", "old-example-secret");
    m.setNetworks({{-70,"Home"},{-30,"Cafe"},{-40,"Home"},{-50,"Cafe"},{-10,""},{-15,std::string(33,'x')}}, true);
    assert(m.networks.size() == 2 && m.networks[0].ssid == "Cafe" && m.networks[1].rssi == -40);
    assert(m.selected == 1); // Saved network remains selected despite RSSI ordering.
    m.choose();
    assert(m.state == Model::Password && m.input == "old-example-secret");
    m.state = Model::Selecting;
    m.move(1);
    assert(m.selected == 0);
    m.choose();
    assert(m.ssid == "Cafe" && m.input.empty()); // Never send another AP's secret.
    assert(m.append("cafe-example-secret") && m.submit());
    m.connectionResult(false, false);
    assert(m.state == Model::Failed && m.isSaved("Home"));
    assert(m.submit() && m.input == "cafe-example-secret"); // Failed attempt stays editable.
    m.input.clear();
    assert(m.submit()); // Open AP accepts an empty password.
    m.connectionResult(true, false);
    assert(m.state == Model::Failed && m.saveFailed && m.isSaved("Home"));
    m.submit(); m.submit(); m.connectionResult(true, true);
    assert(m.state == Model::Success && m.isSaved("Cafe") && !m.isSaved("Home"));

    m.setNetworks({{-35,"Cafe"},{-20,"Home"}}, true);
    assert(m.selected == 1); // Preserve selection by SSID after an explicit refresh.
    m.manual();
    assert(!m.submit());
    const std::string chinese = "TP-LINK_二楼电脑";
    assert(m.append(chinese) && m.submit() && m.ssid == chinese && m.input.empty());
    m.input = std::string(64, 'a');
    assert(!m.append("x") && m.submit());
    m.manual();
    assert(m.append(std::string(32,'s')) && !m.append("x") && m.submit());
    std::string utf8 = "A中😀";
    WifiSetup::eraseLastCharacter(utf8); assert(utf8 == "A中");
    WifiSetup::eraseLastCharacter(utf8); assert(utf8 == "A");
    WifiSetup::eraseLastCharacter(utf8); WifiSetup::eraseLastCharacter(utf8); assert(utf8.empty());
    m.setNetworks({}, false); m.move(1); m.choose();
    assert(m.state == Model::Selecting && !m.scanOk && m.selected == 0);
    m.manual(); assert(m.append("Hidden Wi-Fi") && m.submit());
    assert(m.ssid == "Hidden Wi-Fi");
    std::cout << "WiFi setup model: selection, credential isolation, retries, limits, UTF-8 and hidden/open networks passed\n";
}
