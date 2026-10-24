#include "coral/language_model/character_model.h"
#include "picosha2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace coral::language_model {
namespace {
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint64_t u64(const uint8_t* p) { return u32(p) | uint64_t(u32(p + 4)) << 32; }
float f32(const uint8_t* p) { const auto bits = u32(p); float f; std::memcpy(&f, &bits, 4); return f; }
bool decode(std::string_view text, std::vector<uint32_t>& out) {
    out.clear();
    for (size_t i = 0; i < text.size();) {
        uint32_t c = static_cast<uint8_t>(text[i++]);
        unsigned extra = 0;
        uint32_t minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { c &= 31; extra = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { c &= 15; extra = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { c &= 7; extra = 3; minimum = 0x10000; }
        else if (c >= 0x80 || c == 0) return false;
        if (i + extra > text.size()) return false;
        while (extra--) {
            const auto next = static_cast<uint8_t>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            c = (c << 6) | (next & 63);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
        out.push_back(c);
    }
    return true;
}
float dot(const float* a, const float* b, size_t n) {
    size_t i = 0;
    float sum = 0;
#if defined(_M_X64) || defined(__SSE2__)
    __m128 total = _mm_setzero_ps(), second = total, third = total, fourth = total;
    for (; i + 16 <= n; i += 16) {
        total = _mm_add_ps(total, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
        second = _mm_add_ps(second, _mm_mul_ps(_mm_loadu_ps(a + i + 4), _mm_loadu_ps(b + i + 4)));
        third = _mm_add_ps(third, _mm_mul_ps(_mm_loadu_ps(a + i + 8), _mm_loadu_ps(b + i + 8)));
        fourth = _mm_add_ps(fourth, _mm_mul_ps(_mm_loadu_ps(a + i + 12), _mm_loadu_ps(b + i + 12)));
    }
    total = _mm_add_ps(_mm_add_ps(total, second), _mm_add_ps(third, fourth));
    for (; i + 4 <= n; i += 4)
        total = _mm_add_ps(total, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
    float lanes[4];
    _mm_storeu_ps(lanes, total);
    sum = (lanes[0] + lanes[1]) + (lanes[2] + lanes[3]);
#endif
    for (; i < n; ++i) sum += a[i] * b[i];
    return sum;
}
float quantized_dot(const int8_t* a, const float* b, size_t n) {
    size_t i = 0;
    float sum = 0;
#if defined(_M_X64) || defined(__SSE2__)
    const auto zero = _mm_setzero_si128();
    auto first = _mm_setzero_ps(), second = first, third = first, fourth = first;
    for (; i + 16 <= n; i += 16) {
        const auto bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
        const auto sign = _mm_cmpgt_epi8(zero, bytes);
        const auto low = _mm_unpacklo_epi8(bytes, sign), high = _mm_unpackhi_epi8(bytes, sign);
        const auto low_sign = _mm_cmpgt_epi16(zero, low), high_sign = _mm_cmpgt_epi16(zero, high);
        first = _mm_add_ps(first, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(low, low_sign)), _mm_loadu_ps(b+i)));
        second = _mm_add_ps(second, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(low, low_sign)), _mm_loadu_ps(b+i+4)));
        third = _mm_add_ps(third, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(high, high_sign)), _mm_loadu_ps(b+i+8)));
        fourth = _mm_add_ps(fourth, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(high, high_sign)), _mm_loadu_ps(b+i+12)));
    }
    float lanes[4];
    _mm_storeu_ps(lanes, _mm_add_ps(_mm_add_ps(first, second), _mm_add_ps(third, fourth)));
    sum = (lanes[0]+lanes[1]) + (lanes[2]+lanes[3]);
#endif
    for (; i < n; ++i) sum += float(a[i]) * b[i];
    return sum;
}
}

struct CharacterModel::Impl {
    struct Matrix { size_t offset = 0, scales = 0, rows = 0, columns = 0; };
    struct Layer { size_t gamma1, beta1, qkv_bias, project_bias;
        size_t gamma2, beta2, expand_bias, contract_bias; Matrix qkv, project, expand, contract; };
    struct State { std::vector<std::vector<float>> keys, values; std::vector<float> next; size_t length = 0; };
    uint32_t dimensions = 0, heads = 0, width = 0, vocabulary = 0, positions = 0, bos = 0;
    float epsilon = 0;
    Matrix embedding;
    size_t positional = 0, gamma = 0, beta = 0;
    bool quantized = false;
    std::vector<float> weights;
    std::vector<int8_t> packed_weights;
    std::vector<Layer> layers;
    std::unordered_map<uint32_t, uint32_t> tokens;
    State initial;

    void norm(std::vector<float>& x, size_t g, size_t b) const {
        float mean = 0, variance = 0;
        for (float v : x) mean += v;
        mean /= dimensions;
        for (float v : x) variance += (v - mean) * (v - mean);
        const float inverse = 1.f / std::sqrt(variance / dimensions + epsilon);
        for (size_t i = 0; i < dimensions; ++i) x[i] = (x[i] - mean) * inverse * weights[g + i] + weights[b + i];
    }
    float matrix_dot(const Matrix& matrix, size_t row, const float* x) const {
        return quantized ? weights[matrix.scales + row] * quantized_dot(
            packed_weights.data() + matrix.offset + row*matrix.columns, x, matrix.columns)
            : dot(weights.data() + matrix.offset + row*matrix.columns, x, matrix.columns);
    }
    std::vector<float> linear(const std::vector<float>& x, const Matrix& matrix, size_t bias, size_t rows) const {
        std::vector<float> y(rows);
        for (size_t i = 0; i < rows; ++i)
            y[i] = matrix_dot(matrix, i, x.data()) + weights[bias + i];
        return y;
    }
    void advance(State& state, uint32_t token) const {
        if (state.length >= positions) throw std::runtime_error("character model context limit");
        std::vector<float> x(dimensions);
        for (size_t i = 0; i < dimensions; ++i)
            x[i] = (quantized ? float(packed_weights[embedding.offset + token*dimensions + i]) * weights[embedding.scales + token]
                : weights[embedding.offset + token*dimensions + i]) + weights[positional + state.length*dimensions + i];
        for (size_t layer = 0; layer < layers.size(); ++layer) {
            const auto& w = layers[layer];
            auto normalized = x;
            norm(normalized, w.gamma1, w.beta1);
            const auto qkv = linear(normalized, w.qkv, w.qkv_bias, dimensions * 3);
            auto& keys = state.keys[layer]; auto& values = state.values[layer];
            keys.insert(keys.end(), qkv.begin() + dimensions, qkv.begin() + dimensions * 2);
            values.insert(values.end(), qkv.begin() + dimensions * 2, qkv.end());
            std::vector<float> attention(dimensions, 0.f);
            const size_t head_width = dimensions / heads;
            for (size_t head = 0; head < heads; ++head) {
                std::vector<float> logits(state.length + 1);
                for (size_t t = 0; t < logits.size(); ++t)
                    logits[t] = dot(qkv.data() + head * head_width,
                        keys.data() + t * dimensions + head * head_width, head_width) / std::sqrt(float(head_width));
                const float maximum = *std::max_element(logits.begin(), logits.end());
                float total = 0;
                for (float& value : logits) { value = std::exp(value - maximum); total += value; }
                for (size_t t = 0; t < logits.size(); ++t)
                    for (size_t i = 0; i < head_width; ++i)
                        attention[head * head_width + i] += logits[t] / total * values[t * dimensions + head * head_width + i];
            }
            const auto projected = linear(attention, w.project, w.project_bias, dimensions);
            for (size_t i = 0; i < dimensions; ++i) x[i] += projected[i];
            normalized = x;
            norm(normalized, w.gamma2, w.beta2);
            auto expanded = linear(normalized, w.expand, w.expand_bias, width);
            for (float& value : expanded)
                value = .5f * value * (1.f + std::tanh(.7978845608028654f * (value + .044715f * value * value * value)));
            const auto contracted = linear(expanded, w.contract, w.contract_bias, dimensions);
            for (size_t i = 0; i < dimensions; ++i) x[i] += contracted[i];
        }
        norm(x, gamma, beta);
        state.next.resize(vocabulary);
        for (size_t i = 0; i < vocabulary; ++i)
            state.next[i] = matrix_dot(embedding, i, x.data());
        const float maximum = *std::max_element(state.next.begin(), state.next.end());
        double total = 0;
        for (float value : state.next) total += std::exp(double(value - maximum));
        const double normalizer = maximum + std::log(total);
        for (float& value : state.next) value = float(value - normalizer);
        ++state.length;
    }
};

CharacterModel::CharacterModel() = default;
CharacterModel::~CharacterModel() = default;
CharacterModel::CharacterModel(CharacterModel&&) noexcept = default;
CharacterModel& CharacterModel::operator=(CharacterModel&&) noexcept = default;
bool CharacterModel::valid() const noexcept { return impl_ != nullptr; }
std::size_t CharacterModel::weight_bytes() const noexcept {
    return impl_ ? impl_->weights.capacity()*sizeof(float) + impl_->packed_weights.capacity() : 0;
}

bool CharacterModel::load_file(const std::filesystem::path& path, std::string& error) {
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < 128 || input.tellg() > 256 * 1024 * 1024)
            throw std::runtime_error("character model file size");
        std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
        input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        const auto version = u32(bytes.data() + 8);
        if (!input || std::memcmp(bytes.data(), "CCHLM001", 8) || (version != 1 && version != 2) ||
            u32(bytes.data() + 12) != 128 || u64(bytes.data() + 16) != bytes.size() || u32(bytes.data() + 60))
            throw std::runtime_error("character model header");
        if (std::any_of(bytes.begin() + 96, bytes.begin() + 128, [](uint8_t v) { return v != 0; }))
            throw std::runtime_error("character model reserved bytes");
        std::array<uint8_t, 32> digest{};
        picosha2::hash256(bytes.begin() + 128, bytes.end(), digest.begin(), digest.end());
        if (!std::equal(digest.begin(), digest.end(), bytes.begin() + 64))
            throw std::runtime_error("character model digest");
        auto model = std::make_unique<Impl>();
        model->quantized = version == 2;
        const uint32_t count = u32(bytes.data() + 24);
        model->dimensions = u32(bytes.data() + 28); model->heads = u32(bytes.data() + 32);
        model->width = u32(bytes.data() + 36); model->vocabulary = u32(bytes.data() + 40);
        model->positions = u32(bytes.data() + 44); model->bos = u32(bytes.data() + 48);
        const auto unknown = u32(bytes.data() + 52); model->epsilon = f32(bytes.data() + 56);
        const uint64_t d = model->dimensions, f = model->width, v = model->vocabulary, p = model->positions;
        if (!count || count > 8 || !d || d > 512 || !model->heads || model->heads > d || d % model->heads ||
            !f || f > 4096 || !v || v > 32768 || !p || p > 2048 || model->bos >= v || unknown >= v ||
            !std::isfinite(model->epsilon) || model->epsilon <= 0 || model->epsilon > .1f)
            throw std::runtime_error("character model dimensions");
        const uint64_t elements = v*d + p*d + 2*d + count*(4*d*d + 2*d*f + 9*d + f);
        const uint64_t matrix_elements = v*d + count*(4*d*d + 2*d*f);
        const uint64_t scales = v + count*(5*d + f);
        const uint64_t float_elements = model->quantized ? elements - matrix_elements + scales : elements;
        const uint64_t packed_elements = model->quantized ? matrix_elements : 0;
        if (128 + v*4 + float_elements*4 + packed_elements != bytes.size()) throw std::runtime_error("character model payload size");
        for (uint32_t i = 0; i < v; ++i) {
            const auto scalar = u32(bytes.data() + 128 + i * 4);
            if (scalar >= 0x110000) {
                if (scalar != 0x110000 + i) throw std::runtime_error("character model special token");
            } else if (!scalar || (scalar >= 0xd800 && scalar <= 0xdfff) || !model->tokens.emplace(scalar, i).second)
                throw std::runtime_error("character model vocabulary");
        }
        if (u32(bytes.data() + 128 + model->bos * 4) < 0x110000 ||
            u32(bytes.data() + 128 + unknown * 4) < 0x110000)
            throw std::runtime_error("character model control tokens");
        model->weights.reserve(static_cast<size_t>(float_elements));
        model->packed_weights.reserve(static_cast<size_t>(packed_elements));
        size_t offset = static_cast<size_t>(128 + v*4);
        const auto take = [&](size_t n) {
            const auto begin = model->weights.size();
            for (size_t i = 0; i < n; ++i) {
                const float value = f32(bytes.data() + offset); offset += 4;
                if (!std::isfinite(value) || std::abs(value) > 10000) throw std::runtime_error("character model weight");
                model->weights.push_back(value);
            }
            return begin;
        };
        const auto matrix = [&](size_t rows, size_t columns) {
            Impl::Matrix result{}; result.rows = rows; result.columns = columns;
            if (!model->quantized) { result.offset = take(rows*columns); return result; }
            result.scales = take(rows);
            for (size_t row = 0; row < rows; ++row)
                if (model->weights[result.scales + row] <= 0 || model->weights[result.scales + row] > 10000.f/127.f)
                    throw std::runtime_error("character model scale");
            result.offset = model->packed_weights.size();
            for (size_t i = 0; i < rows*columns; ++i) {
                const auto raw = bytes[offset++];
                if (raw == 128) throw std::runtime_error("character model quantized weight");
                const auto value = raw < 128 ? int(raw) : int(raw) - 256;
                model->packed_weights.push_back(static_cast<int8_t>(value));
            }
            return result;
        };
        model->embedding = matrix(v,d); model->positional = take(p*d); model->gamma = take(d); model->beta = take(d);
        for (uint32_t i = 0; i < count; ++i) {
            Impl::Layer layer{};
            layer.gamma1 = take(d); layer.beta1 = take(d); layer.qkv = matrix(3*d,d); layer.qkv_bias = take(3*d);
            layer.project = matrix(d,d); layer.project_bias = take(d); layer.gamma2 = take(d); layer.beta2 = take(d);
            layer.expand = matrix(f,d); layer.expand_bias = take(f); layer.contract = matrix(d,f); layer.contract_bias = take(d);
            model->layers.push_back(layer);
        }
        if (offset != bytes.size()) throw std::runtime_error("character model weight layout");
        Impl::State empty;
        empty.keys.resize(count); empty.values.resize(count);
        model->advance(empty, model->bos);
        model->initial = std::move(empty);
        impl_ = std::move(model); error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool CharacterModel::score_candidates(std::string_view context, const std::vector<std::string_view>& texts,
    std::vector<double>& scores, const std::function<bool()>& cancelled) const {
    if (!impl_ || texts.size() > 128) return false;
    try {
        const auto& model = *impl_;
        auto state = model.initial;
        std::vector<uint32_t> scalars;
        if (!decode(context, scalars)) return false;
        // 前文最多取末尾 32 字；超出词表时从后续合法字重新建立条件。
        const size_t begin = scalars.size() > 32 ? scalars.size() - 32 : 0;
        for (size_t i = begin; i < scalars.size(); ++i) {
            if (cancelled && cancelled()) return false;
            const auto found = model.tokens.find(scalars[i]);
            if (found == model.tokens.end()) state = model.initial;
            else model.advance(state, found->second);
        }
        struct Node { uint32_t token; std::vector<size_t> children, outputs; };
        std::vector<Node> nodes(1);
        std::vector<double> result(texts.size(), std::numeric_limits<double>::quiet_NaN());
        for (size_t output = 0; output < texts.size(); ++output) {
            if (cancelled && cancelled()) return false;
            if (!decode(texts[output], scalars) || scalars.empty() || scalars.size() > 64 ||
                state.length + scalars.size() > model.positions) continue;
            std::vector<uint32_t> ids;
            for (auto scalar : scalars) {
                const auto found = model.tokens.find(scalar);
                if (found == model.tokens.end()) { ids.clear(); break; }
                ids.push_back(found->second);
            }
            if (ids.empty()) continue;
            size_t at = 0;
            for (auto id : ids) {
                size_t child = nodes.size();
                for (auto candidate : nodes[at].children) if (nodes[candidate].token == id) { child = candidate; break; }
                if (child == nodes.size()) {
                    nodes[at].children.push_back(child); nodes.push_back(Node{id, {}, {}});
                    if (nodes.size() > 2048) return false;
                }
                at = child;
            }
            nodes[at].outputs.push_back(output);
        }
        std::function<void(size_t, double)> visit;
        visit = [&](size_t at, double total) {
            // 先保存直接子边的分数；回溯只缩短 KV，不保留每层整份词表和历史副本。
            std::vector<double> child_scores;
            for (auto child : nodes[at].children) child_scores.push_back(total + state.next[nodes[child].token]);
            size_t index = 0;
            for (auto child : nodes[at].children) {
                if (cancelled && cancelled()) throw std::runtime_error("character scoring cancelled");
                const auto& node = nodes[child];
                const double score = child_scores[index++];
                if (!std::isfinite(score)) throw std::runtime_error("character model score");
                for (auto output : node.outputs) result[output] = score;
                if (!node.children.empty()) {
                    const auto length = state.length;
                    model.advance(state, node.token);
                    visit(child, score);
                    state.length = length;
                    for (auto& keys : state.keys) keys.resize(length*model.dimensions);
                    for (auto& values : state.values) values.resize(length*model.dimensions);
                }
            }
        };
        visit(0, 0);
        if (cancelled && cancelled()) return false;
        scores = std::move(result); return true;
    } catch (const std::exception&) { return false; }
}
} // 命名空间 coral::language_model 结束
