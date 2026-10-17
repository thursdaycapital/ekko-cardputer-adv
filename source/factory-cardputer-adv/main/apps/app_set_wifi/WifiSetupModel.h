#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace WifiSetup {
inline void eraseLastCharacter(std::string& text)
{
    if (text.empty()) return;
    size_t start = text.size() - 1;
    while (start && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) --start;
    text.erase(start);
}
struct Network { int rssi; std::string ssid; };

// Selection and editing rules shared by the actual UI and host verification.
class Model {
public:
    enum State { Scanning, Selecting, Ssid, Password, Connecting, Success, Failed };
    State state = Scanning;
    std::vector<Network> networks;
    size_t selected = 0;
    std::string input, ssid, password;
    bool scanOk = true, saveFailed = false;

    void loadSaved(const std::string& name, const std::string& secret)
    { savedSsid = name; savedPassword = secret; }
    bool isSaved(const std::string& name) const { return name == savedSsid && !name.empty(); }
    void setNetworks(const std::vector<std::pair<int, std::string>>& results, bool ok)
    {
        const auto previous = selected < networks.size() ? networks[selected].ssid : savedSsid;
        networks.clear();
        for (const auto& result : results) {
            if (result.second.empty() || result.second.size() > 32) continue;
            auto found = std::find_if(networks.begin(), networks.end(), [&](const Network& n) { return n.ssid == result.second; });
            if (found == networks.end()) networks.push_back({result.first, result.second});
            else found->rssi = std::max(found->rssi, result.first);
        }
        std::sort(networks.begin(), networks.end(), [](const Network& a, const Network& b) {
            return a.rssi == b.rssi ? a.ssid < b.ssid : a.rssi > b.rssi;
        });
        selected = 0;
        for (size_t i = 0; i < networks.size(); ++i) if (networks[i].ssid == previous) selected = i;
        scanOk = ok;
        state = Selecting;
    }
    void move(int delta)
    {
        if (networks.empty()) return;
        const int count = static_cast<int>(networks.size());
        selected = (static_cast<int>(selected) + delta % count + count) % count;
    }
    void choose()
    {
        if (selected >= networks.size()) return;
        ssid = networks[selected].ssid;
        enterPassword();
    }
    void manual() { input.clear(); state = Ssid; }
    bool submit()
    {
        if (state == Ssid) {
            if (input.empty() || input.size() > 32) return false;
            ssid = input;
            enterPassword();
            return true;
        }
        if (state == Password) {
            if (input.size() > 64) return false;
            password = input;
            input.clear();
            state = Connecting;
            return true;
        }
        if (state == Failed) { input = password; state = Password; return true; }
        return false;
    }
    bool append(const std::string& text)
    {
        const size_t limit = state == Ssid ? 32 : 64;
        if ((state != Ssid && state != Password) || input.size() > limit || text.size() > limit - input.size()) return false;
        input += text;
        return true;
    }
    void connectionResult(bool connected, bool committed)
    {
        saveFailed = connected && !committed;
        state = connected && committed ? Success : Failed;
        if (state == Success) loadSaved(ssid, password);
    }
private:
    std::string savedSsid, savedPassword;
    void enterPassword()
    {
        password = isSaved(ssid) ? savedPassword : std::string();
        input = password;
        saveFailed = false;
        state = Password;
    }
};
}
