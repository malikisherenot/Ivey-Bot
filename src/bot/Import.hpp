#pragma once
#include <Geode/Geode.hpp>
#include <cstdint>
#include <vector>
#include "Macro.hpp"

namespace ivey {

    // .gdr2 (binary, starts with "GDR")
    bool looksLikeGdr2(std::vector<uint8_t> const& data);
    geode::Result<Macro> importGdr2(std::vector<uint8_t> const& data);

    // .gdr (msgpack)
    geode::Result<Macro> importGdr1(std::vector<uint8_t> const& data);

    // .gdr.json (the same data as .gdr, written as text)
    bool looksLikeJson(std::vector<uint8_t> const& data);
    geode::Result<Macro> importGdrJson(std::vector<uint8_t> const& data);
}
