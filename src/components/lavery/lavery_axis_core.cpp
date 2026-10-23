#include "lavery_core.h"

#include <md_util.h>
#include <core/md_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lavery {

AxisPath compute_axis(const std::vector<BasePairFrame>& frames, const std::vector<StepIndex>& steps,
                       const md_unitcell_t* cell, int samples_per_step) {
    AxisPath result;
    result.offsets.push_back(0);
    if (samples_per_step < 1 || samples_per_step > 128) {
        MD_LOG_ERROR("Lavery: axis samples per step must be between 1 and 128");
        return result;
    }
    const size_t n = frames.size();
    std::vector<int> next(n, -1), previous(n, -1);
    std::vector<bool> valid(n), visited(n);
    for (size_t i = 0; i < n; ++i) {
        const auto& f = frames[i];
        valid[i] = std::isfinite(f.origin.x) && std::isfinite(f.origin.y) && std::isfinite(f.origin.z) &&
            vec3_length(f.z) > 0.5f;
    }
    for (auto step : steps) {
        if (step.a >= n || step.b >= n || step.a == step.b || next[step.a] >= 0 || previous[step.b] >= 0) {
            MD_LOG_ERROR("Lavery: invalid or branching axis connectivity");
            return result;
        }
        if (valid[step.a] && valid[step.b]) {
            next[step.a] = (int)step.b;
            previous[step.b] = (int)step.a;
        }
    }
    for (size_t start = 0; start < n; ++start) {
        if (!valid[start] || previous[start] >= 0 || visited[start]) continue;
        std::vector<vec3_t> centers;
        for (int i = (int)start; i >= 0 && !visited[i]; i = next[i]) {
            visited[i] = true;
            vec3_t point = frames[i].origin;
            if (!centers.empty()) {
                vec3_t delta = vec3_sub(point, centers.back());
                if (cell) md_util_min_image_vec3(&delta, 1, cell);
                point = vec3_add(centers.back(), delta);
            }
            centers.push_back(point);
        }
        if (centers.size() < 2) continue;
        auto smooth = centers;
        for (size_t i = 1; i + 1 < centers.size(); ++i) {
            const size_t radius = std::min({size_t(2), i, centers.size() - 1 - i});
            if (radius == 2) {
                smooth[i] = vec3_mul1(vec3_add(vec3_add(centers[i - 2], centers[i + 2]),
                    vec3_add(vec3_mul1(vec3_add(centers[i - 1], centers[i + 1]), 4), vec3_mul1(centers[i], 6))), 1.0f / 16);
            } else {
                smooth[i] = vec3_mul1(vec3_add(vec3_add(centers[i - 1], centers[i + 1]),
                    vec3_mul1(centers[i], 2)), 0.25f);
            }
        }
        for (size_t i = 0; i + 1 < smooth.size(); ++i) {
            const vec3_t p0 = smooth[i], p1 = smooth[i + 1];
            const vec3_t before = i ? smooth[i - 1] : vec3_sub(vec3_mul1(p0, 2), p1);
            const vec3_t after = i + 2 < smooth.size() ? smooth[i + 2] : vec3_sub(vec3_mul1(p1, 2), p0);
            const vec3_t t0 = vec3_mul1(vec3_sub(p1, before), 0.5f);
            const vec3_t t1 = vec3_mul1(vec3_sub(after, p0), 0.5f);
            const int end = i + 2 == smooth.size() ? samples_per_step + 1 : samples_per_step;
            for (int s = 0; s < end; ++s) {
                const float t = (float)s / samples_per_step, t2 = t * t, t3 = t2 * t;
                result.points.push_back(vec3_add(
                    vec3_add(vec3_mul1(p0, 2*t3 - 3*t2 + 1), vec3_mul1(t0, t3 - 2*t2 + t)),
                    vec3_add(vec3_mul1(p1, -2*t3 + 3*t2), vec3_mul1(t1, t3 - t2))));
                const vec3_t tangent = vec3_add(
                    vec3_add(vec3_mul1(p0, 6*t2 - 6*t), vec3_mul1(t0, 3*t2 - 4*t + 1)),
                    vec3_add(vec3_mul1(p1, -6*t2 + 6*t), vec3_mul1(t1, 3*t2 - 2*t)));
                const float length = vec3_length(tangent);
                result.tangents.push_back(length > 1.0e-6f ? vec3_div1(tangent, length) : vec3_t{});
            }
        }
        result.offsets.push_back(result.points.size());
    }
    return result;
}

bool compute_distributions(DistributionComparison* out, const std::vector<float>& values,
                           size_t member_count, size_t bins) {
    if (!out || !member_count || !bins || bins > 4096 || values.size() % member_count) {
        MD_LOG_ERROR("Lavery: invalid distribution dimensions");
        return false;
    }
    *out = {};
    double lo = INFINITY, hi = -INFINITY;
    for (float v : values) {
        if (std::isfinite(v)) {
            lo = std::min(lo, (double)v);
            hi = std::max(hi, (double)v);
        }
    }
    out->valid.assign(member_count, 0);
    if (!std::isfinite(lo)) return true;
    if (lo == hi) {
        const double padding = std::max(0.5, std::fabs(lo) * 1.0e-6);
        lo -= padding;
        hi += padding;
    }
    out->bin_width = (hi - lo) / bins;
    out->centers.resize(bins);
    out->density.assign(member_count * bins, 0);
    for (size_t b = 0; b < bins; ++b) out->centers[b] = lo + (b + 0.5) * out->bin_width;
    for (size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) continue;
        const size_t m = i % member_count;
        const size_t bin = std::min(bins - 1, (size_t)std::max(0.0, ((double)values[i] - lo) / out->bin_width));
        ++out->valid[m];
        ++out->density[m * bins + bin];
    }
    for (size_t m = 0; m < member_count; ++m) {
        if (!out->valid[m]) continue;
        for (size_t b = 0; b < bins; ++b) out->density[m * bins + b] /= out->valid[m] * out->bin_width;
    }
    return true;
}

std::string csv_axis(const std::string& run, double frame, const AxisPath& axis) {
    if (axis.points.size() != axis.tangents.size() || axis.offsets.empty() ||
        axis.offsets.front() != 0 || axis.offsets.back() != axis.points.size() ||
        !std::is_sorted(axis.offsets.begin(), axis.offsets.end())) {
        MD_LOG_ERROR("Lavery: invalid axis export dimensions");
        return {};
    }
    std::string output = "run,frame,segment,sample,x_A,y_A,z_A,tangent_x,tangent_y,tangent_z\n";
    char row[512];
    for (size_t s = 1; s < axis.offsets.size(); ++s) {
        for (size_t i = axis.offsets[s - 1]; i < axis.offsets[s]; ++i) {
            const auto p = axis.points[i], t = axis.tangents[i];
            snprintf(row, sizeof(row), ",%.9g,%zu,%zu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                frame, s, i - axis.offsets[s - 1], p.x, p.y, p.z, t.x, t.y, t.z);
            output += csv_quote(run) + row;
        }
    }
    return output;
}

bool publish_series(md_attributes_t* table, const std::string& run, const char* leaf, const char* label,
                     md_attribute_type_t type, size_t frames, size_t members,
                     const void* data, size_t bytes, md_unit_t unit) {
    if (!table || run.empty() || !leaf || !label || !data || !frames || !members ||
        frames > UINT32_MAX || members > UINT32_MAX) {
        MD_LOG_ERROR("Lavery: invalid series publication arguments");
        return false;
    }
    const std::string path = run + "/lavery/" + leaf;
    const md_attribute_desc_t desc = {
        .path = str_from_cstr(path.c_str()),
        .format = {.type = type, .components = 1, .rank = 2, .shape = {(uint32_t)frames, (uint32_t)members}},
        .flags = MD_ATTRIBUTE_FLAG_TEMPORAL,
        .unit = unit,
        .label = str_from_cstr(label),
        .description = STR_INIT("Canonical fixed partners; per-frame geometric and canonical-contact validity; fitted Tsukuba/Curves+ intra/step convention"),
        .data = data,
        .byte_size = bytes,
    };
    const auto id = md_attributes_replace(table, &desc);
    if (id == MD_ATTRIBUTE_INVALID) {
        MD_LOG_ERROR("Lavery: failed to publish '%s'", path.c_str());
        return false;
    }
    return true;
}

} // namespace lavery
