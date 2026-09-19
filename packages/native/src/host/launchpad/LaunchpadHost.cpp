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

LaunchpadHost::LaunchpadHost(LaunchpadLink::PortFactory factory, LaunchpadScanner::OpenFn opener,
                             PortLister lister, std::string configDir)
    : link_(std::move(factory)), scanner_(std::move(opener)), lister_(std::move(lister)),
      configDir_(std::move(configDir)) {}

LaunchpadConfigDto LaunchpadHost::getConfig() {
    LaunchpadConfigDto c;
    c.inputs         = lister_ ? lister_(true) : std::vector<std::string>{};
    c.outputs        = lister_ ? lister_(false) : std::vector<std::string>{};
    c.selectedInput  = input_;
    c.selectedOutput = output_;
    c.deviceLabel    = deviceLabel_;
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

void LaunchpadHost::setDeviceLabel(const std::string& label) {
    deviceLabel_.clear();
    deviceLabel_.reserve(label.size());
    for (char c : label)                       // the cfg is line-oriented; a newline in the label would
        if (c != '\n' && c != '\r') deviceLabel_ += c;  // shift every field after it on the next load
    save();
}

bool LaunchpadHost::scan(std::vector<std::uint8_t> probe, unsigned windowMs) {
    if (scanner_.busy() || link_.isConnected()) return false;
    auto inputs  = lister_ ? lister_(true) : std::vector<std::string>{};
    auto outputs = lister_ ? lister_(false) : std::vector<std::string>{};
    // The false edge is fired from scanStatus(), on the UI thread - see the ScanBusyFn comment. Raised before
    // start() so the host's inputs are already down when the first port is opened.
    if (onScanBusy_ && !scanReported_) {
        scanReported_ = true;
        onScanBusy_(true);
    }
    if (scanner_.start(std::move(probe), std::move(inputs), std::move(outputs), windowMs)) return true;
    if (scanReported_) {  // refused after all - give the host its inputs back rather than leaving them down
        scanReported_ = false;
        if (onScanBusy_) onScanBusy_(false);
    }
    return false;
}

LaunchpadScanStatusDto LaunchpadHost::scanStatus() {
    LaunchpadScanStatusDto s = scanner_.status();
    if (scanReported_ && !s.busy) {
        scanReported_ = false;
        if (onScanBusy_) onScanBusy_(false);
    }
    return s;
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
        // Line-oriented, so the device label appended below is readable by an older build and a file written
        // by one (three lines) loads here with an empty label - readLine leaves its output alone at EOF.
        std::string in, out, en, label;
        readLine(f, in);
        readLine(f, out);
        readLine(f, en);
        readLine(f, label);
        std::fclose(f);
        input_       = in;
        output_      = out;
        enabled_     = std::atoi(en.c_str()) != 0;
        deviceLabel_ = label;
    }
    // Reconnect the persisted pair, if any. This claims the ports; it does NOT put the device into
    // Programmer mode - only the controller role does that, on the connect edge it sees in the block info -
    // so a link restored before the UI has run owes the device no farewell yet.
    if (enabled_) applyLink();
}

void LaunchpadHost::save() {
    if (std::FILE* f = std::fopen((configDir_ + "/launchpad.cfg").c_str(), "w")) {
        std::fprintf(f, "%s\n%s\n%d\n%s\n", input_.c_str(), output_.c_str(), enabled_ ? 1 : 0,
                     deviceLabel_.c_str());
        std::fclose(f);
    }
}

}  // namespace retroplug
