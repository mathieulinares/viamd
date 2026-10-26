#include "lavery_core.h"

#include <md_system.h>
#include <md_util.h>
#include <core/md_allocator.h>
#include <core/md_bitset.h>
#include <core/md_log.h>
#include <core/md_str.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace lavery {
namespace {

uint64_t contact_key(uint32_t donor, uint32_t acceptor) {
    return ((uint64_t)donor << 32) | acceptor;
}

std::vector<uint64_t> bond_keys(const md_hbond_set_t& bonds) {
    std::vector<uint64_t> keys;
    keys.reserve(bonds.count);
    for (size_t i = 0; i < bonds.count; ++i) keys.push_back(contact_key(bonds.donor[i], bonds.acceptor[i]));
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool contact_present(const PairContact& contact, const std::vector<uint64_t>& keys) {
    return contact.explicit_hydrogen &&
        std::binary_search(keys.begin(), keys.end(), contact_key(contact.donor, contact.acceptor));
}

uint16_t evaluate_contacts(uint8_t* count, const PairChemistry& pair, const md_system_state_t& state,
                           const std::vector<uint64_t>& keys) {
    *count = 0;
    if (pair.contacts.size() != pair.expected_contacts) return Quality_NoncanonicalPair;
    for (const auto& contact : pair.contacts) {
        bool present = false;
        if (pair.uses_hydrogens) {
            present = contact_present(contact, keys);
        } else if (contact.donor < state.num_atoms && contact.acceptor < state.num_atoms) {
            vec3_t delta = vec3_sub(md_state_coord(&state, contact.donor), md_state_coord(&state, contact.acceptor));
            md_util_min_image_vec3(&delta, 1, &state.unitcell);
            const float distance = vec3_length(delta);
            present = std::isfinite(distance) && distance >= 2.0f && distance <= 3.5f;
        }
        if (present) ++*count;
    }
    return *count >= (pair.uses_hydrogens ? 1 : 2) ? Quality_Good : Quality_NoPairContacts;
}

} // namespace

const char* pairing_state_name(PairingState state) {
    switch (state) {
    case Pairing_WatsonCrick: return "Watson-Crick";
    case Pairing_Hoogsteen: return "Hoogsteen (A-T/U)";
    case Pairing_Other: return "Other / ambiguous";
    case Pairing_Unpaired: return "Unpaired";
    default: return "Unavailable";
    }
}

PairingState classify_pairing(const PairingEvidence& ev, float chi_a, float chi_b, uint16_t geometry) {
    constexpr uint16_t unavailable = Quality_InvalidBase | Quality_UnsupportedBase | Quality_NonfiniteCoordinates |
        Quality_DegenerateBase | Quality_MissingAtoms;
    if (!ev.complete || !ev.expected || !std::isfinite(chi_a) || !std::isfinite(chi_b) || (geometry & unavailable))
        return Pairing_Unavailable;
    const bool compatible = !(geometry & (Quality_Separation | Quality_Alignment | Quality_PlaneOffset));
    // Conservative, disjoint orientation windows; do not reuse the coarse anti/syn state bins.
    const auto wrap = [](float chi) { return chi-360.0*std::floor((chi+180.0)/360.0); };
    const auto anti = [&](float chi) { const auto a = wrap(chi); return a >= -180 && a <= -60; };
    const auto syn = [&](float chi) { const auto a = wrap(chi); return a >= 0 && a <= 120; };
    const bool wc = compatible && ev.canonical == ev.expected && anti(chi_a) && anti(chi_b);
    const bool hg = compatible && ev.purine_side >= 0 && ev.hoogsteen == 2 &&
        syn(ev.purine_side == 0 ? chi_a : chi_b) && anti(ev.purine_side == 0 ? chi_b : chi_a);
    if (wc && !hg) return Pairing_WatsonCrick;
    if (hg && !wc) return Pairing_Hoogsteen;
    if (!compatible && !ev.canonical && !ev.hoogsteen) return Pairing_Unpaired;
    return Pairing_Other;
}

bool pairing_runs(std::vector<PairingRun>* runs, const std::vector<PairingState>& states, size_t members, size_t member) {
    if (!runs || !members || member >= members || states.size() % members) {
        MD_LOG_ERROR("Lavery: invalid pairing-state series");
        return false;
    }
    runs->clear();
    const size_t frames = states.size() / members;
    for (size_t f = 0; f < frames; ++f) {
        const auto state = states[f*members+member];
        if (state >= Pairing_Count) {
            runs->clear();
            MD_LOG_ERROR("Lavery: invalid pairing-state code");
            return false;
        }
        if (runs->empty() || runs->back().state != state) runs->push_back({f, f, state});
        else runs->back().end = f;
    }
    return true;
}

bool pairing_transitions(std::vector<PairingTransition>* out, const std::vector<PairingRun>& runs, const EventOptions& options) {
    if (!out || !std::isfinite(options.min_duration) || options.min_duration < 0) {
        MD_LOG_ERROR("Lavery: invalid pairing-state residence");
        return false;
    }
    out->clear();
    const auto& times = options.times;
    for (size_t f = 0; f < times.size(); ++f) if (!std::isfinite(times[f]) || (f && times[f] <= times[f-1])) {
        MD_LOG_ERROR("Lavery: pairing-state timestamps must be finite and strictly increasing");
        return false;
    }
    bool has_source = false;
    PairingRun source;
    size_t expected = 0;
    for (const auto& run : runs) {
        if (run.begin != expected || run.end < run.begin || run.state >= Pairing_Count ||
            (!times.empty() && run.end >= times.size())) {
            MD_LOG_ERROR("Lavery: invalid pairing-state run");
            return false;
        }
        expected = run.end+1;
        if (run.end < options.first_frame || run.begin > options.last_frame) continue;
        PairingRun clipped = {std::max(run.begin, options.first_frame), std::min(run.end, options.last_frame), run.state};
        if (clipped.state == Pairing_Unavailable) { has_source = false; continue; }
        const bool qualified = times.empty() ? clipped.end-clipped.begin+1 >= std::max(size_t(1), options.min_frames) :
            times[clipped.end]-times[clipped.begin] >= options.min_duration;
        if (!qualified) continue;
        if (has_source && source.state != clipped.state) out->push_back({source, clipped});
        source = clipped;
        has_source = true;
    }
    if ((!times.empty() && expected != times.size()) || (!runs.empty() && options.first_frame > std::min(options.last_frame, expected-1))) {
        MD_LOG_ERROR("Lavery: pairing-state range or timestamp count does not match series");
        out->clear();
        return false;
    }
    return true;
}

const char* csv_pairing_header() {
    return "run,pair,label,state,begin_frame,end_frame,begin_time,end_time,time_unit,residence,min_residence,qualifies\n";
}

std::string csv_pairing_run(const std::string& run, const std::string& label, size_t pair, const PairingRun& span,
                            double begin, double end, const std::string& unit, double minimum, bool physical_time) {
    const double duration = physical_time ? end-begin : (double)(span.end-span.begin+1);
    char row[256];
    snprintf(row, sizeof(row), ",%zu,%zu,%.9g,%.9g,", span.begin, span.end, begin, end);
    std::string output = csv_quote(run)+","+std::to_string(pair+1)+","+csv_quote(label)+","+
        csv_quote(pairing_state_name(span.state))+row+csv_quote(unit);
    snprintf(row, sizeof(row), ",%.9g,%.9g,%d\n", duration, minimum, span.state != Pairing_Unavailable && duration >= minimum);
    return output+row;
}

PairingValidator::~PairingValidator() {
    md_hbond_query_free(&query_);
}

bool PairingValidator::initialize(const md_system_t& sys, const std::vector<BaseTopology>& bases,
                                  const std::vector<Pair>& pairs) {
    md_hbond_query_free(&query_);
    query_ = {};
    initialized_ = false;
    pairs_.assign(pairs.size(), {});
    md_bitmask_t donors = {}, acceptors = {};
    md_bitmask_init(&donors, 0, sys.atom.count, md_get_heap_allocator());
    md_bitmask_init(&acceptors, 0, sys.atom.count, md_get_heap_allocator());
    auto atom = [&](const BaseTopology& base, const char* name) -> int32_t {
        const auto range = md_component_atom_range(&sys.component, base.component);
        for (uint32_t i = range.beg; i < range.end; ++i) {
            if (str_eq_cstr(md_atom_name(&sys.atom, i), name)) return (int32_t)i;
        }
        return -1;
    };
    for (size_t p = 0; p < pairs.size(); ++p) {
        if (pairs[p].a >= bases.size() || pairs[p].b >= bases.size()) {
            MD_LOG_ERROR("Lavery: invalid pair index for chemistry validation");
            md_bitmask_free(&donors);
            md_bitmask_free(&acceptors);
            return false;
        }
        const BaseTopology* a = &bases[pairs[p].a];
        const BaseTopology* b = &bases[pairs[p].b];
        if (a->reference > b->reference) std::swap(a, b);
        auto add = [&](std::vector<PairContact>& contacts, const BaseTopology& d, const char* dn, const BaseTopology& ac, const char* an) {
            const int32_t donor = atom(d, dn), acceptor = atom(ac, an);
            if (donor < 0 || acceptor < 0) return;
            contacts.push_back({(uint32_t)donor, (uint32_t)acceptor, false});
            md_bitmask_set_bit(&donors, (uint32_t)donor);
            md_bitmask_set_bit(&acceptors, (uint32_t)acceptor);
        };
        auto& chemistry = pairs_[p];
        if (a->reference == 0 && b->reference == 3) {
            add(chemistry.contacts, *a, "N6", *b, "O4");
            add(chemistry.contacts, *b, "N3", *a, "N1");
            add(chemistry.hoogsteen_contacts, *a, "N6", *b, "O4");
            add(chemistry.hoogsteen_contacts, *b, "N3", *a, "N7");
            chemistry.purine_side = a == &bases[pairs[p].a] ? 0 : 1;
        } else if (a->reference == 1 && b->reference == 2) {
            pairs_[p].expected_contacts = 3;
            add(chemistry.contacts, *a, "N1", *b, "N3");
            add(chemistry.contacts, *a, "N2", *b, "O2");
            add(chemistry.contacts, *b, "N4", *a, "O6");
        }
    }
    const md_hbond_params_t params = md_hbond_params_preset(MD_HBOND_PRESET_GROMACS);
    const md_bitset_t donor_view = md_bitmask_view(&donors), acceptor_view = md_bitmask_view(&acceptors);
    const md_hbond_desc_t desc = {.params = &params, .donors = &donor_view, .acceptors = &acceptor_view};
    const bool ok = md_hbond_query_init(&query_, &desc, &sys, md_get_heap_allocator());
    md_bitmask_free(&donors);
    md_bitmask_free(&acceptors);
    if (!ok) {
        MD_LOG_ERROR("Lavery: failed to prepare canonical hydrogen-bond query");
        return false;
    }
    for (auto& pair : pairs_) {
        for (auto* contacts : {&pair.contacts, &pair.hoogsteen_contacts}) for (auto& contact : *contacts) {
            for (size_t d = 0; d < query_.num_donors; ++d) {
                if (query_.donor_d[d] == contact.donor) {
                    contact.explicit_hydrogen = true;
                    contact.hydrogens.push_back(query_.donor_h[d]);
                }
                for (const auto& contact : pair.contacts) pair.uses_hydrogens |= contact.explicit_hydrogen;
            }
        }
    }
    initialized_ = true;
    return true;
}

bool PairingValidator::evaluate(std::vector<uint16_t>& quality, std::vector<uint8_t>& contacts,
                                const md_system_state_t& state, std::vector<PairingEvidence>* evidence) const {
    if (!initialized_) {
        MD_LOG_ERROR("Lavery: hydrogen-bond query is not initialized");
        return false;
    }
    md_hbond_set_t bonds = {};
    if (!md_hbond_query_eval(&bonds, &query_, &state, md_get_heap_allocator())) {
        MD_LOG_ERROR("Lavery: canonical hydrogen-bond evaluation failed");
        md_hbond_set_free(&bonds);
        return false;
    }
    const auto keys = bond_keys(bonds);
    quality.resize(pairs_.size());
    contacts.resize(pairs_.size());
    for (size_t p = 0; p < pairs_.size(); ++p) quality[p] = evaluate_contacts(&contacts[p], pairs_[p], state, keys);
    if (evidence) {
        evidence->assign(pairs_.size(), {});
        for (size_t p = 0; p < pairs_.size(); ++p) {
            const auto& pair = pairs_[p];
            auto& ev = (*evidence)[p];
            ev.expected = (uint8_t)pair.expected_contacts;
            ev.purine_side = pair.purine_side;
            ev.complete = pair.contacts.size() == pair.expected_contacts &&
                (pair.purine_side < 0 || pair.hoogsteen_contacts.size() == 2);
            for (const auto* list : {&pair.contacts, &pair.hoogsteen_contacts}) for (const auto& contact : *list) {
                bool finite = contact.donor < state.num_atoms && contact.acceptor < state.num_atoms;
                if (finite) for (uint32_t atom : {contact.donor, contact.acceptor}) {
                    const auto v = md_state_coord(&state, atom);
                    finite &= std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
                }
                for (uint32_t atom : contact.hydrogens) {
                    if (atom >= state.num_atoms) { finite = false; break; }
                    const auto v = md_state_coord(&state, atom);
                    finite &= std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
                }
                ev.complete &= contact.explicit_hydrogen && finite;
                if (contact_present(contact, keys)) {
                    if (list == &pair.contacts) ++ev.canonical;
                    else ++ev.hoogsteen;
                }
            }
        }
    }
    md_hbond_set_free(&bonds);
    return true;
}

uint16_t evaluate_pair_contacts(uint8_t* count, const PairChemistry& pair,
                                const md_system_state_t& state, const md_hbond_set_t& bonds) {
    if (!count) {
        MD_LOG_ERROR("Lavery: missing contact-count output");
        return Quality_NoPairContacts;
    }
    return evaluate_contacts(count, pair, state, bond_keys(bonds));
}

bool analyze_frame(FrameAnalysis* out, const md_system_t& sys, const md_system_state_t& state,
                    const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs,
                    const std::vector<StepIndex>& steps, const PairingValidator& validator) {
    if (!out || validator.chemistry().size() != pairs.size()) {
        MD_LOG_ERROR("Lavery: invalid frame analysis output or pairing topology");
        return false;
    }
    *out = {};
    Values missing;
    std::fill(std::begin(missing.metric), std::end(missing.metric), std::numeric_limits<float>::quiet_NaN());
    out->pair_values.assign(pairs.size(), missing);
    out->step_values.assign(steps.size(), missing);
    out->pair_frames.resize(pairs.size());
    out->step_quality.assign(steps.size(), Quality_AdjacentPair);
    std::vector<PairingEvidence> evidence;
    if (!validator.evaluate(out->pair_quality, out->contacts, state, &evidence)) return false;
    std::vector<BaseFrame> frames;
    std::vector<uint16_t> base_quality;
    if (!extract_base_frames(frames, sys, state, bases, &base_quality)) return false;
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto pair = pairs[p];
        if (pair.a >= bases.size() || pair.b >= bases.size()) {
            MD_LOG_ERROR("Lavery: invalid nucleotide index during frame analysis");
            return false;
        }
        uint16_t geometry = 0;
        BasePairFrame frame;
        Values values;
        const bool present = compute_present_base_pair(&values, &frame, &geometry,
            frames[pair.a], frames[pair.b], &state.unitcell);
        out->pair_quality[p] |= geometry | base_quality[pair.a] | base_quality[pair.b];
        if (present && out->pair_quality[p] == Quality_Good) {
            out->pair_values[p] = values;
            out->pair_frames[p] = frame;
        }
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        const auto step = steps[s];
        if (step.a >= pairs.size() || step.b >= pairs.size()) {
            MD_LOG_ERROR("Lavery: invalid pair index during step analysis");
            return false;
        }
        auto& quality = out->step_quality[s];
        quality = out->pair_quality[step.a] | out->pair_quality[step.b];
        if (quality) {
            quality |= Quality_AdjacentPair;
            continue;
        }
        BasePairFrame a = out->pair_frames[step.a], b = out->pair_frames[step.b];
        vec3_t delta = vec3_sub(b.origin, a.origin);
        md_util_min_image_vec3(&delta, 1, &state.unitcell);
        b.origin = vec3_add(a.origin, delta);
        Values values;
        if (compute_step(&values, a, b)) out->step_values[s] = values;
        else quality |= Quality_AmbiguousTransform;
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const vec3_t none = {nan, nan, nan};
    auto coord = [&](int32_t atom) { return atom >= 0 ? md_state_coord(&state, atom) : none; };
    // Backbone and sugar geometry belongs to the nucleotide, so it is measured whether or not the pair is intact.
    out->nucleotide_values.assign(2 * pairs.size(), missing);
    out->nucleotide_quality.resize(2 * pairs.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        for (int side = 0; side < 2; ++side) {
            const auto& base = bases[side ? pairs[p].b : pairs[p].a];
            vec3_t atoms[BB_Count];
            uint32_t present = 0;
            for (int k = 0; k < BB_Count; ++k) {
                atoms[k] = coord(base.backbone[k]);
                if (base.backbone[k] >= 0) present |= 1u << k;
            }
            compute_nucleotide(&out->nucleotide_values[2 * p + side], atoms, &state.unitcell,
                &out->nucleotide_quality[2 * p + side], present);
        }
    }
    out->pairing_states.resize(pairs.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto pair = pairs[p];
        const uint16_t geometry = pair_quality(frames[pair.a], frames[pair.b], &state.unitcell) |
            base_quality[pair.a] | base_quality[pair.b];
        out->pairing_states[p] = classify_pairing(evidence[p], out->nucleotide_values[2*p].metric[Chi],
            out->nucleotide_values[2*p+1].metric[Chi], geometry);
    }
    // Groove widths from phosphorus positions along each helix; strand I is the strand of the first pair's base a.
    std::vector<int64_t> step_from(pairs.size(), -1);
    for (size_t s = steps.size(); s-- > 0;) if (steps[s].a < pairs.size()) step_from[steps[s].a] = (int64_t)s;
    std::vector<vec3_t> p1, p2;
    std::vector<float> minor, major;
    for (const auto& chain : helix_chains(pairs.size(), steps)) {
        const size_t n = chain.size();
        const uint32_t strand = bases[pairs[chain[0]].a].strand;
        p1.assign(n, none);
        p2.assign(n, none);
        for (size_t k = 0; k < n; ++k) {
            const auto& a = bases[pairs[chain[k]].a];
            const auto& b = bases[pairs[chain[k]].b];
            if (a.strand == strand && b.strand != strand) { p1[k] = coord(a.backbone[BB_P]); p2[k] = coord(b.backbone[BB_P]); }
            else if (b.strand == strand && a.strand != strand) { p1[k] = coord(b.backbone[BB_P]); p2[k] = coord(a.backbone[BB_P]); }
        }
        minor.assign(n, nan);
        major.assign(n, nan);
        groove_widths(minor.data(), major.data(), p1.data(), p2.data(), n, &state.unitcell);
        for (size_t k = 0; k + 1 < n; ++k) {
            const int64_t s = step_from[chain[k]];
            if (s < 0 || steps[s].b != chain[k + 1]) continue;
            out->step_values[s].metric[MinorGroove] = minor[k];
            out->step_values[s].metric[MajorGroove] = major[k];
        }
    }
    return true;
}

} // namespace lavery
