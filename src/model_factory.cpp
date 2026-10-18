#include "chiller/model_factory.hpp"

#include <map>

#include "chiller/models.hpp"

namespace chiller {

namespace {

std::map<std::string, ChillerFactoryFn>& registry() {
    static std::map<std::string, ChillerFactoryFn> r = {
        {"screw", [](const ChillerConfig& c, std::uint32_t seed) { return std::make_unique<ScrewChiller>(c, seed); }},
        {"centrifugal",
         [](const ChillerConfig& c, std::uint32_t seed) { return std::make_unique<CentrifugalChiller>(c, seed); }},
        {"scroll", [](const ChillerConfig& c, std::uint32_t seed) { return std::make_unique<ScrollChiller>(c, seed); }},
    };
    return r;
}

}  // namespace

void registerChillerType(const std::string& type, ChillerFactoryFn factory) { registry()[type] = std::move(factory); }

std::unique_ptr<ChillerModel> makeChiller(const ChillerConfig& cfg, std::uint32_t seed) {
    auto it = registry().find(cfg.type);
    if (it == registry().end()) {
        std::string known;
        for (const auto& name : registeredChillerTypes()) known += (known.empty() ? "" : ", ") + name;
        throw ConfigError(cfg.sourceFile + ": unknown chiller type '" + cfg.type + "' (known: " + known + ")");
    }
    return it->second(cfg, seed);
}

std::vector<std::string> registeredChillerTypes() {
    std::vector<std::string> names;
    for (const auto& entry : registry()) names.push_back(entry.first);
    return names;
}

}  // namespace chiller
