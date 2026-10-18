#pragma once
// Creates the right ChillerModel from a config's "type" field.
// New model types register themselves here; nothing else needs to change.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "chiller/chiller_model.hpp"

namespace chiller {

using ChillerFactoryFn = std::function<std::unique_ptr<ChillerModel>(const ChillerConfig&, std::uint32_t seed)>;

void registerChillerType(const std::string& type, ChillerFactoryFn factory);
std::unique_ptr<ChillerModel> makeChiller(const ChillerConfig& cfg, std::uint32_t seed = 42);
std::vector<std::string> registeredChillerTypes();

}  // namespace chiller
