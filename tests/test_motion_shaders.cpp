// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual SPIR-V backend without opening a window or loading the game.
// Output is validated by spirv-val (see docs/upscaler.md).
// bbport: motion vertex shaders take the run's buffer addresses as specialization constants, so
// the pipeline cache can keep them (vk_pipeline_serialization.cpp).
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <setjmp.h>
#include <span>
#include "common/serdes.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/specialization.h"

namespace Vulkan { // vk_pipeline_serialization.cpp
Serialization::Archive SerializeShaderMeta(const Shader::Info& info,
                                           const Shader::StageSpecialization& spec,
                                           size_t perm_hash, size_t perm_idx);
bool LoadShaderMeta(Serialization::Archive& ar, Shader::Info& info,
                    std::optional<Shader::Gcn::FetchShaderData>& fetch_shader_data,
                    Shader::StageSpecialization& spec, size_t& perm_idx);
} // namespace Vulkan

namespace {
using namespace Shader;

struct MotionConstants {
    std::set<u32> spec_ids;       ///< SpecId decorations
    bool spec_ids_are_u64 = true; ///< each decorates a 64-bit OpSpecConstant
    bool embeds_address = false;  ///< a 64-bit OpConstant holds one of the run's addresses
};

MotionConstants Inspect(std::span<const u32> code) {
    constexpr u32 OpTypeInt = 21, OpConstant = 43, OpSpecConstant = 50, OpDecorate = 71;
    constexpr u32 SpecId = 1;
    const std::set<u64> addresses{MotionVectors::params_address, MotionVectors::params_address + 16,
                                  MotionVectors::positions_address};
    std::set<u32> u64_types;
    std::map<u32, u32> spec_constant_types;
    std::map<u32, u32> decorated;
    MotionConstants result;
    for (size_t i = 5; i < code.size();) {
        const u32 op = code[i] & 0xffff;
        const u32 words = code[i] >> 16;
        assert(words > 0 && i + words <= code.size());
        if (op == OpTypeInt && code[i + 2] == 64) {
            u64_types.insert(code[i + 1]);
        } else if (op == OpConstant && words == 5 && u64_types.contains(code[i + 1])) {
            result.embeds_address |= addresses.contains(code[i + 3] | u64(code[i + 4]) << 32);
        } else if (op == OpSpecConstant) {
            spec_constant_types[code[i + 2]] = code[i + 1];
        } else if (op == OpDecorate && code[i + 2] == SpecId) {
            decorated[code[i + 1]] = code[i + 3];
        }
        i += words;
    }
    for (const auto& [id, spec_id] : decorated) {
        result.spec_ids.insert(spec_id);
        const auto type = spec_constant_types.find(id);
        result.spec_ids_are_u64 &=
            type != spec_constant_types.end() && u64_types.contains(type->second);
    }
    return result;
}

// A cache entry of a vertex shader, with or without object motion, as the pipeline cache writes
// it. Bytes 4-7 hold the binary version.
std::vector<u8> VertexMeta(bool motion) {
    Info info{};
    info.hw_stage = HwStage::Vertex;
    info.sw_stage = SwStage::Vertex;
    StageSpecialization spec{};
    spec.runtime_info.Initialize(HwStage::Vertex, SwStage::Vertex);
    spec.runtime_info.hw.vs.motion_vectors = motion;
    return Vulkan::SerializeShaderMeta(info, spec, 1, 0).TakeOff();
}

bool Preloads(std::vector<u8> meta) {
    Serialization::Archive ar{std::move(meta)};
    Info info{};
    std::optional<Gcn::FetchShaderData> fetch_shader_data;
    StageSpecialization spec{};
    spec.info = &info;
    size_t perm_idx{};
    return Vulkan::LoadShaderMeta(ar, info, fetch_shader_data, spec, perm_idx);
}

void CheckCacheEntries() {
    assert(Preloads(VertexMeta(false)));
    assert(Preloads(VertexMeta(true)));
    // An entry from before the specialization constants: the version a plain shader gets.
    auto old_motion = VertexMeta(true);
    const auto plain = VertexMeta(false);
    std::copy_n(plain.begin() + 4, 4, old_motion.begin() + 4);
    assert(!Preloads(old_motion));
    // A run without object motion has no buffers for a motion shader.
    const u64 positions_address = MotionVectors::positions_address;
    MotionVectors::positions_address = 0;
    assert(!Preloads(VertexMeta(true)));
    assert(Preloads(VertexMeta(false)));
    MotionVectors::positions_address = positions_address;
}
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(dir);
    MotionVectors::params_address = 0x10000;
    MotionVectors::positions_address = 0x20000;
    for (bool vertex : {true, false}) {
        for (bool motion : {false, true}) {
            Info info{};
            info.hw_stage = vertex ? HwStage::Vertex : HwStage::Fragment;
            info.sw_stage = vertex ? SwStage::Vertex : SwStage::Fragment;
            RuntimeInfo runtime{};
            runtime.Initialize(info.hw_stage, info.sw_stage);
            if (vertex) {
                runtime.hw.vs.motion_vectors = motion;
            } else {
                runtime.hw.fs.motion_vectors = motion;
                runtime.hw.fs.color_buffers[0].num_format = AmdGpu::NumberFormat::Float;
            }
            Common::ObjectPool<IR::Inst> pool;
            IR::Block block(pool);
            IR::IREmitter ir(block);
            const auto output = vertex ? IR::Attribute::Position0 : IR::Attribute::RenderTarget0;
            for (u32 component = 0; component < 4; ++component) {
                info.stores.Set(output, component);
                if (!vertex && component < 2) {
                    info.loads.Set(IR::Attribute::FragCoord, component);
                    ir.SetAttribute(output, ir.GetAttribute(IR::Attribute::FragCoord, component), component);
                } else {
                    ir.SetAttribute(output, ir.Imm32(component == 3 ? 1.0f : 0.0f), component);
                }
            }
            ir.Epilogue();
            IR::Program program(info);
            program.blocks.push_back(&block);
            program.syntax_list.push_back({.data = {.block = &block},
                                           .type = IR::AbstractSyntaxNode::Type::Block});
            program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});
            Profile profile{};
            profile.supported_spirv = 0x00010600;
            profile.support_int64 = true;
            Backend::Bindings bindings{};
            const auto code = Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
            const auto path = dir / (std::string(vertex ? "vertex" : "fragment") +
                                      (motion ? "-motion.spv" : "-plain.spv"));
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(code.data()), code.size() * sizeof(u32));
            if (!out) { return 1; }

            const auto constants = Inspect(code);
            if (vertex && motion) {
                assert((constants.spec_ids == std::set<u32>{MotionVectors::ParamsAddressSpecId,
                                                            MotionVectors::PositionsAddressSpecId}));
                assert(constants.spec_ids_are_u64);
            } else {
                assert(constants.spec_ids.empty());
            }
            assert(!constants.embeds_address);
        }
    }
    CheckCacheEntries();
    std::puts("Motion shaders: PASS (buffer addresses are specialization constants; cache entries "
              "of motion vertex shaders preload only with them and with object motion on)");
}
