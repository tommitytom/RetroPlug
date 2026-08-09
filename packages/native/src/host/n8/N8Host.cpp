#include "host/n8/N8Host.hpp"

#include <cstdio>
#include <cstdlib>

namespace retroplug {

N8Host::N8Host(N8Link::PortFactory factory, PortLister lister, std::string configDir)
    : link_(std::move(factory)), lister_(std::move(lister)), configDir_(std::move(configDir)) {}

N8ConfigDto N8Host::getConfig() {
    N8ConfigDto c;
    c.ports        = lister_ ? lister_() : std::vector<N8PortDto>{};
    c.selectedPort = port_;
    c.connected    = link_.isConnected();
    c.enabled      = enabled_;
    c.lookaheadMs  = link_.lookaheadMs();
    c.bytes        = link_.bytesForwarded();
    c.error        = link_.lastError();
    return c;
}

void N8Host::setPort(const std::string& port) {
    const bool wasStreaming = link_.isConnected();
    port_ = port;
    if (wasStreaming) {
        link_.disconnect();
        if (!port.empty()) link_.connect(port);
    }
    save();
}

void N8Host::connect(bool enable) {
    if (enable) {
        if (port_.empty()) {  // auto-pick the first attached N8 (USB VID:PID 38df:0017)
            for (const N8PortDto& p : (lister_ ? lister_() : std::vector<N8PortDto>{}))
                if (p.isN8) { port_ = p.port; break; }
        }
        enabled_ = true;
        if (!port_.empty()) link_.connect(port_);
    } else {
        enabled_ = false;
        link_.disconnect();
    }
    save();
}

void N8Host::setLookahead(int ms) {
    link_.setLookaheadMs(ms < 0 ? 0 : ms);
    save();
}

void N8Host::restore() {
    if (FILE* f = std::fopen((configDir_ + "/n8.cfg").c_str(), "r")) {
        char        line[512];
        std::string port;
        int         la = 10, en = 0;  // defaults: lookahead 10ms, disabled
        if (std::fgets(line, sizeof line, f)) {
            port = line;
            while (!port.empty() && (port.back() == '\n' || port.back() == '\r')) port.pop_back();
        }
        if (std::fgets(line, sizeof line, f)) la = std::atoi(line);
        if (std::fgets(line, sizeof line, f)) en = std::atoi(line);
        std::fclose(f);
        port_ = port;
        link_.setLookaheadMs(la < 0 ? 0 : la);
        enabled_ = (en != 0);
    }
    if (enabled_) connect(true);  // reconnect the persisted link (auto-picks if the saved port is empty)
}

void N8Host::save() {
    if (FILE* f = std::fopen((configDir_ + "/n8.cfg").c_str(), "w")) {
        std::fprintf(f, "%s\n%d\n%d\n", port_.c_str(), link_.lookaheadMs(), enabled_ ? 1 : 0);
        std::fclose(f);
    }
}

}  // namespace retroplug
