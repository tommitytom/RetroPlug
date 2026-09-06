// The dynamic-parameter diff classifier in the DPF fork (spec/12-dynamic-parameters.md).
//
// PluginExporter::reinitParameters() re-runs initParameter() and decides, per index, what a LIVE plugin
// is allowed to change. That decision is the whole contract the plugin and both backends rest on, so it
// is worth pinning independently of any plugin format: this binary builds no format at all, only DPF's
// format-neutral core plus the toy plugin below.
//
// What must hold:
//   - name / shortName / unit / description and the hidden flag apply, and report kParameterInfoChanged
//   - a structural change (symbol, ranges, other hints, enum values) is REFUSED, and refused per index
//     as a whole, so a mixed edit cannot half-apply
//   - `hints` and `ranges` are never written, because the audio thread reads them (the hidden flag lives
//     in a side array precisely so that stays true)
#include "DistrhoPlugin.hpp"
#include "src/DistrhoPluginInternal.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

START_NAMESPACE_DISTRHO

// What the toy plugin declares, as globals the test drives between reinit passes. initParameter() is
// the plugin's single source of truth, so re-declaring is how a real plugin re-labels too.
namespace {
struct Decl {
    std::string name      = "Alpha";
    std::string shortName = "Alph";
    std::string unit      = "%";
    std::string symbol    = "alpha";
    float       max       = 100.0f;
    uint32_t    extraHint = 0x0;
    bool        hidden    = false;
};
Decl gDecl;
} // namespace

class DynParamsTestPlugin : public Plugin {
public:
    DynParamsTestPlugin() : Plugin(2, 0, 0) {}

protected:
    const char* getLabel()       const noexcept override { return "dynparams"; }
    const char* getDescription() const          override { return "diff classifier fixture"; }
    const char* getMaker()       const noexcept override { return "retroplug"; }
    const char* getLicense()     const noexcept override { return "MIT"; }
    uint32_t    getVersion()     const noexcept override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const noexcept override { return d_cconst('R', 'P', 'd', 'p'); }

    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index == 0)
        {
            p.symbol      = gDecl.symbol.c_str();
            p.name        = gDecl.name.c_str();
            p.shortName   = gDecl.shortName.c_str();
            p.unit        = gDecl.unit.c_str();
            p.ranges.min  = 0.0f;
            p.ranges.max  = gDecl.max;
            p.ranges.def  = 0.0f;
            p.hints       = kParameterIsAutomatable | gDecl.extraHint | (gDecl.hidden ? kParameterIsHidden : 0x0);
            return;
        }

        // a second, never-edited parameter: proves a refusal is scoped to its own index
        p.symbol     = "beta";
        p.name       = "Beta";
        p.ranges.min = 0.0f;
        p.ranges.max = 1.0f;
        p.ranges.def = 0.0f;
        p.hints      = kParameterIsAutomatable;
    }

    float getParameterValue(uint32_t) const override { return 0.0f; }
    void  setParameterValue(uint32_t, float) override {}

    void run(const float**, float**, uint32_t) override {}
};

Plugin* createPlugin() { return new DynParamsTestPlugin(); }

END_NAMESPACE_DISTRHO

USE_NAMESPACE_DISTRHO

namespace {

// The declaration is a global because initParameter() has no other way in, so it MUST be reset before
// the exporter is built - the exporter reads it in its own constructor, and Catch2 randomises case
// order. A base class gets that ordering for free: base classes initialise before members.
struct ResetDecl {
    ResetDecl()
    {
        gDecl = Decl{};
        // DPF asserts on these in PrivateData; no backend is here to set them.
        d_nextBufferSize = 512;
        d_nextSampleRate = 48000.0;
    }
};

struct Fixture : private ResetDecl {
    PluginExporter plugin{nullptr, nullptr, nullptr, nullptr};
};

} // namespace

TEST_CASE("reinitParameters reports nothing when the declaration has not moved", "[dynparams]")
{
    Fixture f;
    REQUIRE(f.plugin.reinitParameters() == PluginExporter::kParameterInfoUnchanged);
    REQUIRE(std::string(f.plugin.getParameterName(0).buffer()) == "Alpha");
}

TEST_CASE("descriptor fields apply and report a change", "[dynparams]")
{
    Fixture f;

    gDecl.name      = "PU1 Pulse Width";
    gDecl.shortName = "PU1 Wid";
    gDecl.unit      = "steps";

    REQUIRE(f.plugin.reinitParameters() == PluginExporter::kParameterInfoChanged);
    CHECK(std::string(f.plugin.getParameterName(0).buffer()) == "PU1 Pulse Width");
    CHECK(std::string(f.plugin.getParameterShortName(0).buffer()) == "PU1 Wid");
    CHECK(std::string(f.plugin.getParameterUnit(0).buffer()) == "steps");

    // idempotent: re-running with the same declaration is a no-op again
    CHECK(f.plugin.reinitParameters() == PluginExporter::kParameterInfoUnchanged);
}

TEST_CASE("the hidden flag moves without ever writing hints", "[dynparams]")
{
    Fixture f;
    const uint32_t hintsBefore = f.plugin.getParameterHints(0);
    REQUIRE_FALSE(f.plugin.isParameterHidden(0));

    gDecl.hidden = true;
    REQUIRE(f.plugin.reinitParameters() == PluginExporter::kParameterInfoChanged);
    CHECK(f.plugin.isParameterHidden(0));

    // The audio thread reads `hints`. If the hidden bit lived in there this would have changed, and
    // the main thread would be racing the RT path on that word.
    CHECK(f.plugin.getParameterHints(0) == hintsBefore);

    gDecl.hidden = false;
    REQUIRE(f.plugin.reinitParameters() == PluginExporter::kParameterInfoChanged);
    CHECK_FALSE(f.plugin.isParameterHidden(0));
    CHECK(f.plugin.getParameterHints(0) == hintsBefore);
}

TEST_CASE("a changed range is refused and nothing on that index moves", "[dynparams]")
{
    Fixture f;

    gDecl.max  = 64.0f;   // structural: CLAP needs a full rescan for it, and the RT path reads ranges
    gDecl.name = "Should Not Apply";

    REQUIRE(f.plugin.reinitParameters() == PluginExporter::kParameterInfoRefused);
    CHECK(f.plugin.getParameterRanges(0).max == 100.0f);
    // the refusal covers the whole index: the name rode along in the same declaration and is dropped too
    CHECK(std::string(f.plugin.getParameterName(0).buffer()) == "Alpha");
}

TEST_CASE("a changed symbol is refused", "[dynparams]")
{
    Fixture f;

    // Symbols are how DPF keys its own state save/restore, so a moving one would silently break
    // previously saved host projects.
    gDecl.symbol = "renamed";

    REQUIRE(f.plugin.reinitParameters() == PluginExporter::kParameterInfoRefused);
    CHECK(std::string(f.plugin.getParameterSymbol(0).buffer()) == "alpha");
}

TEST_CASE("a changed hint other than kParameterIsHidden is refused", "[dynparams]")
{
    Fixture f;

    gDecl.extraHint = kParameterIsInteger;   // CLAP's IS_STEPPED: a critical flag, needs RESCAN_ALL

    REQUIRE(f.plugin.reinitParameters() == PluginExporter::kParameterInfoRefused);
    CHECK((f.plugin.getParameterHints(0) & kParameterIsInteger) == 0x0);
}

TEST_CASE("a refusal is scoped to its own index", "[dynparams]")
{
    Fixture f;

    gDecl.symbol = "renamed";   // index 0 is refused...

    const uint32_t result = f.plugin.reinitParameters();
    REQUIRE((result & PluginExporter::kParameterInfoRefused) != 0);
    // ...and index 1, which never changed, is untouched and still reports its own declaration
    CHECK(std::string(f.plugin.getParameterName(1).buffer()) == "Beta");
    CHECK(f.plugin.getParameterRanges(1).max == 1.0f);
}
