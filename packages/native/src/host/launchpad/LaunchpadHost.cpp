#include "host/launchpad/LaunchpadHost.hpp"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace retroplug {

namespace {

/** Read one line, minus its newline. Returns false at EOF. */
bool readLine(std::FILE* f, std::string& out) {
    char line[512];
    if (!std::fgets(line, sizeof line, f)) return false;
    out = line;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return true;
}

}  // namespace

LaunchpadHost::LaunchpadHost(LaunchpadLink::PortFactory factory, PortLister lister, std::string configDir)
    : link_(std::move(factory)), lister_(std::move(lister)), configDir_(std::move(configDir)) {}

LaunchpadConfigDto LaunchpadHost::getConfig() {
    LaunchpadConfigDto c;
    c.inputs         = lister_ ? lister_(true) : std::vector<std::string>{};
    c.outputs        = lister_ ? lister_(false) : std::vector<std::string>{};
    c.selectedInput  = input_;
    c.selectedOutput = output_;
    c.connected      = link_.isConnected();
    c.enabled        = enabled_;
    c.sent           = link_.messagesSent();
    c.dropped        = link_.messagesDropped();
    c.error          = link_.lastError();
    return c;
}

void LaunchpadHost::setPorts(const std::string& input, const std::string& output) {
    input_  = input;
    output_ = output;
    applyLink();  // a live switch releases the old pair (with its farewell) before claiming the new one
    save();
}

void LaunchpadHost::connect(bool enable) {
    enabled_ = enable;
    applyLink();
    save();
}

void LaunchpadHost::applyLink() {
    const bool want = enabled_ && !input_.empty() && !output_.empty();
    if (want)
        link_.connect(input_, output_);
    else
        link_.disconnect();
    if (onLinkChanged_) onLinkChanged_();
}

std::string LaunchpadHost::reservedInputPort() const {
    return link_.isConnected() ? input_ : std::string{};
}

void LaunchpadHost::restore() {
    if (std::FILE* f = std::fopen((configDir_ + "/launchpad.cfg").c_str(), "r")) {
        std::string in, out, en;
        readLine(f, in);
        readLine(f, out);
        readLine(f, en);
        std::fclose(f);
        input_   = in;
        output_  = out;
        enabled_ = std::atoi(en.c_str()) != 0;
    }
    // Reconnect the persisted pair, if any. This claims the ports; it does NOT put the device into
    // Programmer mode - only the controller role does that, on the connect edge it sees in the block info -
    // so a link restored before the UI has run owes the device no farewell yet.
    if (enabled_) applyLink();
}

void LaunchpadHost::save() {
    if (std::FILE* f = std::fopen((configDir_ + "/launchpad.cfg").c_str(), "w")) {
        std::fprintf(f, "%s\n%s\n%d\n", input_.c_str(), output_.c_str(), enabled_ ? 1 : 0);
        std::fclose(f);
    }
}

}  // namespace retroplug
