// The providers a product starts (graph/rp_provider.h, graph/desktop/Providers.hpp,
// docs/modular-graph-phase-e2-design.md section 6): created in the order the product's packages list them, restored in
// that order and stopped in the reverse; one that does not start, or is of another ABI version, left out, and one that
// throws stopping those started before it; and what cmake/packages.cmake generates for a product from the providers it
// names. This binary is a product listing every package and naming the N8's provider alone, so its generated
// rp_start_providers starts the N8's into a real engine's registry and its rp_register_package_rpc mounts that pack's
// facet: the Launchpad pack is carried for its node types, and neither its provider nor its facet is there. Two more
// headers are generated beside it (tests.cmake): one over this package list naming no provider, which starts and mounts
// nothing, and one naming the launchpad provider, which starts it. The ports are deps/serial's here, so nothing
// connects: the provider is never restored from an n8.cfg that enables the link, and the facet is only read.
// [n8-adapters] drives the same provider to a fake cart.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "QjsRpc.hpp"  // the products' server type, over QuickJS (packages/n8/native/test)
#include "graph/desktop/Providers.hpp"
#include "host/devices/ProviderHost.hpp"
#include "host/engine/Engine.hpp"
#include "launchpad/SurfaceLink.hpp"
#include "launchpad/desktop/ControllerLink.hpp"
#include "launchpad/desktop/LaunchpadRpc.hpp"
#include "launchpad/desktop/MidiPortPool.hpp"
#include "launchpad/desktop/PortClaim.hpp"
#include "n8/CartLink.hpp"

#include "rp_product_providers.hpp"  // this product's (retroplug-engine-graph-test's), generated
// Inside a namespace of its own, so its functions do not collide with this product's. That works only because every
// header it includes was included above already: their guards keep them out of the namespace.
namespace without {
#include "without/rp_product_providers.hpp"  // this package list, naming no provider
}
namespace named {
#include "named/rp_product_providers.hpp"  // and naming the launchpad provider, whose desktop half this binary links
}

namespace {

// A provider that records what is done to it, as "<id>:<call>".
std::vector<std::string> gLog;
struct Fake {
    const char* id;
    int         service = 0;
};
Fake gA{"a"}, gB{"b"};

template <Fake* F>
const rp_provider* fakeProvider() {
    static const rp_provider kProvider{
        RP_PROVIDER_ABI_VERSION, F->id,
        [](const rp_device_host* host) -> void* {
            gLog.push_back(std::string(F->id) + ":create " + host->config_dir);
            return F;
        },
        [](void* self) { gLog.push_back(std::string(static_cast<Fake*>(self)->id) + ":restore"); },
        [](void* self) -> void* { return &static_cast<Fake*>(self)->service; },
        [](void* self) { gLog.push_back(std::string(static_cast<Fake*>(self)->id) + ":destroy"); }};
    return &kProvider;
}
const rp_provider* failingProvider() {
    static const rp_provider kProvider{RP_PROVIDER_ABI_VERSION, "failing",
                                       [](const rp_device_host*) -> void* {
                                           gLog.push_back("failing:create");
                                           return nullptr;
                                       },
                                       [](void*) { gLog.push_back("failing:restore"); }, nullptr,
                                       [](void*) { gLog.push_back("failing:destroy"); }};
    return &kProvider;
}
// Built against another layout of the table: only its version may be read, so nothing of it is ever called.
const rp_provider* otherAbiProvider() {
    static const rp_provider kProvider{RP_PROVIDER_ABI_VERSION + 1, "other",
                                       [](const rp_device_host*) -> void* {
                                           gLog.push_back("other:create");
                                           return &gLog;
                                       },
                                       nullptr, nullptr, [](void*) { gLog.push_back("other:destroy"); }};
    return &kProvider;
}
// A create that throws, which no provider's may: the table is C, but a C++ provider that broke the rule must not leave
// the providers started before it running.
const rp_provider* throwingProvider() {
    static const rp_provider kProvider{RP_PROVIDER_ABI_VERSION, "throwing",
                                       [](const rp_device_host*) -> void* {
                                           gLog.push_back("throwing:create");
                                           throw std::runtime_error("no");
                                       },
                                       nullptr, nullptr, [](void*) { gLog.push_back("throwing:destroy"); }};
    return &kProvider;
}

rp_device_host fakeHost() {
    rp_device_host host{};
    host.abi_version = RP_PROVIDER_ABI_VERSION;
    host.config_dir  = "/cfg";
    return host;
}

// Whether a link is installed under a device's name and interface: the probe is installed (its own pointer) and never
// acquired, so the answer is which pointer a second install of that name would refuse.
bool holdsSurface(Engine& engine) {
    int probe = 0;
    if (!engine.installDevice(rp::launchpad::kDeviceName, rp::launchpad::kSurfaceLinkInterface, &probe)) return true;
    engine.clearDevice(&probe);
    return false;
}

bool holdsCart(Engine& engine) {
    int probe = 0;
    if (!engine.installDevice(rp::n8::kDeviceName, rp::n8::kCartLinkInterface, &probe)) return true;
    engine.clearDevice(&probe);
    return false;
}

std::string emptyConfigDir() {
    const auto dir = std::filesystem::temp_directory_path() / "rp-providers-test";
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "n8.cfg");  // never one that would reconnect a real port
    return dir.string();
}

}  // namespace

TEST_CASE("providers start in table order, restore in it and stop in the reverse, and one that fails is left out",
          "[engine-graph][providers]") {
    gLog.clear();
    const rp_device_host host = fakeHost();
    const rp_provider_fn table[] = {&fakeProvider<&gA>, &failingProvider, &fakeProvider<&gB>};
    {
        rp::Providers providers(table, 3, &host);
        CHECK(gLog == std::vector<std::string>{"a:create /cfg", "failing:create", "b:create /cfg"});
        CHECK(providers.size() == 3);
        CHECK(providers.service(0) == &gA.service);
        CHECK(providers.service(1) == nullptr);  // it did not start
        CHECK(providers.service(2) == &gB.service);
        CHECK(providers.service(3) == nullptr);
        // By id, which is how a product asks whether the pack whose state it waits for is running at all: one that
        // failed to start names no id.
        CHECK(providers.startedNamed("a"));
        CHECK(providers.startedNamed("b"));
        CHECK_FALSE(providers.startedNamed("failing"));
        CHECK_FALSE(providers.startedNamed("nope"));

        gLog.clear();
        providers.restore();
        CHECK(gLog == std::vector<std::string>{"a:restore", "b:restore"});

        // Moved, the providers are stopped once, by their new owner.
        rp::Providers moved = std::move(providers);
        gLog.clear();
        providers.stop();
        CHECK(gLog.empty());
        moved.stop();
        CHECK(gLog == std::vector<std::string>{"b:destroy", "a:destroy"});
        CHECK(moved.service(0) == nullptr);
        CHECK_FALSE(moved.startedNamed("a"));  // stopped: nothing of it runs
        gLog.clear();
    }
    CHECK(gLog.empty());  // stopped already: the destructor stops nothing twice

    // And a destructor stops what was not stopped.
    {
        rp::Providers providers(table, 3, &host);
        gLog.clear();
    }
    CHECK(gLog == std::vector<std::string>{"b:destroy", "a:destroy"});
}

TEST_CASE("a provider of another ABI version is refused, and a create that throws stops what started before it",
          "[engine-graph][providers]") {
    const rp_device_host host = fakeHost();
    SECTION("another ABI version") {
        gLog.clear();
        const rp_provider_fn table[] = {&fakeProvider<&gA>, &otherAbiProvider, &fakeProvider<&gB>};
        {
            rp::Providers providers(table, 3, &host);
            CHECK(gLog == std::vector<std::string>{"a:create /cfg", "b:create /cfg"});  // never created
            CHECK(providers.service(1) == nullptr);
            gLog.clear();
            providers.restore();
            CHECK(gLog == std::vector<std::string>{"a:restore", "b:restore"});
            gLog.clear();
        }
        CHECK(gLog == std::vector<std::string>{"b:destroy", "a:destroy"});
    }
    SECTION("a create that throws") {
        gLog.clear();
        const rp_provider_fn table[] = {&fakeProvider<&gA>, &fakeProvider<&gB>, &throwingProvider};
        CHECK_THROWS_AS(rp::Providers(table, 3, &host), std::runtime_error);
        CHECK(gLog == std::vector<std::string>{"a:create /cfg", "b:create /cfg", "throwing:create", "b:destroy",
                                               "a:destroy"});
    }
}

TEST_CASE("a product starts the providers it names, and no other package's: the N8's here, the Launchpad's not",
          "[engine-graph][providers]") {
    Engine                 engine(48000.0);
    rp::host::ProviderHost deviceHost(engine, emptyConfigDir());
    REQUIRE_FALSE(holdsCart(engine));
    REQUIRE_FALSE(holdsSurface(engine));

    // The product names n8 alone (retroplug_product's PROVIDERS), so one provider starts without the product naming
    // any class: the N8's cart link. The Launchpad pack is listed for its node types, and its provider, its ports and
    // its writer thread stay out.
    rp::Providers providers = rp_start_providers(deviceHost.get());
    REQUIRE(providers.size() == 1);
    CHECK(holdsCart(engine));      // installed as n8 / rp.n8.cart-link/1
    CHECK_FALSE(holdsSurface(engine));  // nothing installed under launchpad / rp.launchpad.surface-link/2
    rp_restore_providers(providers);  // no n8.cfg: nothing connects
    CHECK(holdsCart(engine));

    rp::n8::test::QjsRpc server;
    rp_register_package_rpc(server.server(), providers);
    const std::string config = server.ask("n8LinkConfig");
    INFO(config);
    CHECK(config.find(R"("connected":false)") != std::string::npos);
    CHECK(config.find(R"("enabled":false)") != std::string::npos);
    CHECK(config.find(R"("ports":)") != std::string::npos);
    // And no Launchpad facet: a product that starts no Launchpad provider has no control-surface seam, which is what
    // the UI reads the facet's absence as.
    CHECK(server.ask("launchpadStatus").find("-32601") != std::string::npos);  // Method not found

    // Stopped after the plans (none here), the provider takes its link back.
    engine.clearGraph();
    rp_stop_providers(providers);
    CHECK_FALSE(holdsCart(engine));
    CHECK(providers.size() == 0);
}

TEST_CASE("a generated header that names a package's provider starts it, into the product's own engine",
          "[engine-graph][providers]") {
    // The other half of the selection: naming it is all it takes (tests.cmake's named/rp_product_providers.hpp, over the
    // launchpad package). This product does not name it, so the desktop half is linked for this header alone.
    Engine                 engine(48000.0);
    rp::host::ProviderHost deviceHost(engine, emptyConfigDir());
    rp::Providers          providers = named::rp_start_providers(deviceHost.get());
    REQUIRE(providers.size() == 1);
    CHECK(holdsSurface(engine));   // installed as launchpad / rp.launchpad.surface-link/2
    CHECK_FALSE(holdsCart(engine));  // and the N8's is not

    rp::n8::test::QjsRpc server;
    named::rp_register_package_rpc(server.server(), providers);
    const std::string surface = server.ask("launchpadStatus");
    INFO(surface);
    CHECK(surface.find(R"("connected":false)") != std::string::npos);
    CHECK(surface.find(R"("generation":0)") != std::string::npos);
    CHECK(server.ask("n8LinkConfig").find("-32601") != std::string::npos);
    // The per-frame wake is the generated glue's too, and a provider with nothing queued answers it silently.
    named::rp_wake_providers(providers);

    engine.clearGraph();
    named::rp_stop_providers(providers);
    CHECK_FALSE(holdsSurface(engine));
}

TEST_CASE("a product listing no provider starts nothing and mounts nothing", "[engine-graph][providers]") {
    Engine                 engine(48000.0);
    rp::host::ProviderHost deviceHost(engine, emptyConfigDir());
    rp::Providers          providers = without::rp_start_providers(deviceHost.get());
    CHECK(providers.size() == 0);
    CHECK_FALSE(holdsCart(engine));

    rp::n8::test::QjsRpc server;
    without::rp_register_package_rpc(server.server(), providers);
    CHECK(server.ask("n8LinkConfig").find("-32601") != std::string::npos);  // Method not found
    without::rp_restore_providers(providers);
    without::rp_stop_providers(providers);
    CHECK(providers.size() == 0);
}
