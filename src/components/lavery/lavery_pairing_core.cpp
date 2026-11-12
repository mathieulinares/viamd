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

uint8_t count_contacts(const std::vector<PairContact>& contacts, bool uses_hydrogens, const md_system_state_t& state,
                       const std::vector<uint64_t>& keys) {
    uint8_t count = 0;
    for (const auto& contact : contacts) {
        bool present = false;
        if (uses_hydrogens) {
            present = contact_present(contact, keys);
        } else if (contact.donor < state.num_atoms && contact.acceptor < state.num_atoms) {
            vec3_t delta = vec3_sub(md_state_coord(&state, contact.donor), md_state_coord(&state, contact.acceptor));
            md_util_min_image_vec3(&delta, 1, &state.unitcell);
            const float distance = vec3_length(delta);
            present = std::isfinite(distance) && distance >= 2.0f && distance <= 3.5f;
        }
        if (present) ++count;
    }
    return count;
}

PairType type_of(const PairChemistry& pair) {
    for (int t = 0; t < PairType_Count; ++t)
        if (!pair.sets[t].empty() && &pair.sets[t] != &pair.contacts && pair.sets[t].size() == pair.contacts.size() &&
            std::equal(pair.sets[t].begin(), pair.sets[t].end(), pair.contacts.begin(), [](const PairContact& x, const PairContact& y) {
                return x.donor == y.donor && x.acceptor == y.acceptor;
            })) return (PairType)t;
    return PairType_Count;
}

size_t family_contacts(PairType type, const PairChemistry& pair) {
    return type == PairType_WatsonCrick && pair.purine_side < 0 ? 3 : 2;
}

uint16_t evaluate_contacts(uint8_t* count, const PairChemistry& pair, const md_system_state_t& state,
                           const std::vector<uint64_t>& keys) {
    *count = 0;
    if (pair.contacts.empty() || pair.contacts.size() != pair.expected_contacts) return Quality_NoncanonicalPair;
    *count = count_contacts(pair.contacts, pair.uses_hydrogens, state, keys);
    return *count >= (pair.uses_hydrogens ? 1 : 2) ? Quality_Good : Quality_NoPairContacts;
}

} // namespace

bool pairing_profile_samples(std::vector<float>* out, const std::vector<PairingState>& values, PairingState state) {
    if (!out || state<=Pairing_Unavailable || state>=Pairing_Count ||
        std::any_of(values.begin(),values.end(),[](PairingState v) { return v>=Pairing_Count; })) {
        MD_LOG_ERROR("Lavery: invalid pairing occupancy-profile category");
        return false;
    }
    out->resize(values.size());
    for (size_t i=0;i<values.size();++i)
        (*out)[i]=values[i]==Pairing_Unavailable ? NAN : values[i]==state ? 100.0f : 0.0f;
    return true;
}

const char* pairing_state_name(PairingState state) {
    switch (state) {
    case Pairing_WatsonCrick: return "Watson-Crick";
    case Pairing_Hoogsteen: return "Hoogsteen (A-T/U)";
    case Pairing_Other: return "Other / ambiguous";
    case Pairing_Unpaired: return "Unpaired";
    case Pairing_Wobble: return "Wobble (G-T/U)";
    case Pairing_ReverseWatsonCrick: return "Reverse Watson-Crick";
    case Pairing_PartnerSwitch: return "Partner switch";
    default: return "Unavailable";
    }
}

PairType identify_pair_type(const PairChemistry& chemistry, const PairingEvidence& frame0, bool parallel_normals) {
    auto supported = [&](PairType type) {
        return !chemistry.sets[type].empty() && chemistry.sets[type].size() == family_contacts(type, chemistry);
    };
    auto formed = [&](PairType type) { return supported(type) && frame0.present[type] >= 2; };
    if (parallel_normals) return formed(PairType_ReverseWatsonCrick) ? PairType_ReverseWatsonCrick : PairType_Count;
    for (PairType type : {PairType_WatsonCrick, PairType_Hoogsteen, PairType_Wobble}) if (formed(type)) return type;
    if (supported(PairType_WatsonCrick)) return PairType_WatsonCrick;
    if (supported(PairType_Wobble)) return PairType_Wobble;
    return PairType_Count;
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
    if (ev.type == PairType_Wobble || ev.type == PairType_ReverseWatsonCrick) {
        if (compatible && ev.native_expected && ev.native == ev.native_expected && anti(chi_a) && anti(chi_b))
            return ev.type == PairType_Wobble ? Pairing_Wobble : Pairing_ReverseWatsonCrick;
        if (!compatible && !ev.native) return Pairing_Unpaired;
        return Pairing_Other;
    }
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
        const auto range = md_system_residue_atom_range(&sys, base.component);
        for (uint32_t i = range.beg; i < range.end; ++i) {
            if (md_str_eq_cstr(md_atom_name(&sys.atom, i), name)) return (int32_t)i;
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
        auto& sets = chemistry.sets;
        if (a->reference == 0 && b->reference == 3) {
            add(sets[PairType_WatsonCrick], *a, "N6", *b, "O4");
            add(sets[PairType_WatsonCrick], *b, "N3", *a, "N1");
            add(sets[PairType_Hoogsteen], *a, "N6", *b, "O4");
            add(sets[PairType_Hoogsteen], *b, "N3", *a, "N7");
            add(sets[PairType_ReverseWatsonCrick], *b, "N3", *a, "N1");
            add(sets[PairType_ReverseWatsonCrick], *a, "N6", *b, "O2");
            chemistry.purine_side = a == &bases[pairs[p].a] ? 0 : 1;
        } else if (a->reference == 1 && b->reference == 2) {
            add(sets[PairType_WatsonCrick], *a, "N1", *b, "N3");
            add(sets[PairType_WatsonCrick], *a, "N2", *b, "O2");
            add(sets[PairType_WatsonCrick], *b, "N4", *a, "O6");
        } else if (a->reference == 1 && b->reference == 3) {
            add(sets[PairType_Wobble], *a, "N1", *b, "O2");
            add(sets[PairType_Wobble], *b, "N3", *a, "O6");
        }
        const PairType type = pairs[p].type < PairType_Count ? (PairType)pairs[p].type : PairType_WatsonCrick;
        chemistry.contacts = sets[type];
        chemistry.expected_contacts = family_contacts(type, chemistry);
        chemistry.hoogsteen_contacts = sets[PairType_Hoogsteen];
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
        std::vector<std::vector<PairContact>*> lists = {&pair.contacts, &pair.hoogsteen_contacts};
        for (auto& set : pair.sets) lists.push_back(&set);
        for (auto* contacts : lists) for (auto& contact : *contacts) {
            for (size_t d = 0; d < query_.num_donors; ++d) {
                if (query_.donor_d[d] == contact.donor) {
                    contact.explicit_hydrogen = true;
                    contact.hydrogens.push_back(query_.donor_h[d]);
                }
            }
        }
        for (const auto& set : pair.sets) for (const auto& contact : set) pair.uses_hydrogens |= contact.explicit_hydrogen;
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
            const auto& canonical = pair.sets[PairType_WatsonCrick];
            ev.type = (uint8_t)type_of(pair);
            ev.expected = (uint8_t)(canonical.empty() ? pair.expected_contacts : family_contacts(PairType_WatsonCrick, pair));
            ev.native_expected = (uint8_t)pair.expected_contacts;
            ev.purine_side = pair.purine_side;
            for (int t = 0; t < PairType_Count; ++t)
                ev.present[t] = count_contacts(pair.sets[t], pair.uses_hydrogens, state, keys);
            ev.complete = pair.contacts.size() == pair.expected_contacts &&
                (ev.type != PairType_WatsonCrick && ev.type != PairType_Hoogsteen ? true :
                 canonical.size() == ev.expected && (pair.purine_side < 0 || pair.hoogsteen_contacts.size() == 2));
            for (const auto* list : {&canonical, &pair.hoogsteen_contacts, &pair.contacts}) for (const auto& contact : *list) {
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
                    if (list == &canonical) ++ev.canonical;
                    else if (list == &pair.hoogsteen_contacts) ++ev.hoogsteen;
                    else ++ev.native;
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

static bool compute_3dna_pair(Values* values, BasePairFrame* midpoint, BaseFrame a, BaseFrame b,
                             const md_unitcell_t* cell) {
    vec3_t delta = vec3_sub(b.origin, a.origin);
    if (cell) md_util_min_image_vec3(&delta, 1, cell);
    b.origin = vec3_add(a.origin, delta);
    if (vec3_dot(a.z, b.z) < 0.0f) {
        b.y = vec3_mul1(b.y, -1.0f);
        b.z = vec3_mul1(b.z, -1.0f);
    }
    return compute_3dna_step(values, {b.origin,b.x,b.y,b.z}, {a.origin,a.x,a.y,a.z}, midpoint);
}

bool analyze_3dna_pair_step(ThreeDnaPairStepAnalysis* out, const md_system_t& sys,
                            const md_system_state_t& state, const std::vector<BaseTopology>& bases,
                            const std::vector<Pair>& pairs, const std::vector<StepIndex>& steps,
                            const PairingValidator& validator, PairValuePolicy policy) {
    if (!out || validator.chemistry().size() != pairs.size() ||
        (policy != PairValues_RequireGoodQuality && policy != PairValues_ReferenceWithQualityFlags)) {
        MD_LOG_ERROR("Lavery: invalid 3DNA pair/step analysis output, pairing topology, or value policy");
        return false;
    }
    for (const auto& pair : pairs) if (pair.a >= bases.size() || pair.b >= bases.size()) {
        MD_LOG_ERROR("Lavery: invalid nucleotide index for 3DNA pair/step analysis");
        return false;
    }
    for (const auto& step : steps) if (step.a >= pairs.size() || step.b >= pairs.size()) {
        MD_LOG_ERROR("Lavery: invalid step index for 3DNA pair/step analysis");
        return false;
    }
    ThreeDnaPairStepAnalysis result;
    Values missing;
    std::fill(std::begin(missing.metric), std::end(missing.metric), std::numeric_limits<float>::quiet_NaN());
    result.pair_frames.resize(pairs.size());
    result.pair_values.assign(pairs.size(), missing);
    result.step_values.assign(steps.size(), missing);
    result.step_quality.assign(steps.size(), Quality_AdjacentPair);
    std::vector<uint8_t> contacts;
    if (!validator.evaluate(result.pair_quality, contacts, state)) return false;
    std::vector<BaseFrame> frames;
    std::vector<uint16_t> base_quality;
    std::vector<uint8_t> pair_available(pairs.size(), 0);
    if (!extract_3dna_base_frames(frames, sys, state, bases, &base_quality)) return false;
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto pair = pairs[p];
        auto& quality = result.pair_quality[p];
        quality |= base_quality[pair.a] | base_quality[pair.b];
        if (!(base_quality[pair.a] | base_quality[pair.b]) &&
            (policy == PairValues_ReferenceWithQualityFlags || quality == Quality_Good))
            quality |= pair_quality(frames[pair.a], frames[pair.b], &state.unitcell);
        if ((base_quality[pair.a] | base_quality[pair.b]) ||
            (policy == PairValues_RequireGoodQuality && quality != Quality_Good)) continue;
        Values cehs = missing;
        if (!compute_3dna_pair(&cehs, &result.pair_frames[p], frames[pair.a], frames[pair.b], &state.unitcell)) {
            quality |= Quality_AmbiguousTransform;
            continue;
        }
        for (int k = 0; k < 3; ++k) {
            result.pair_values[p].metric[Shear+k] = cehs.metric[Shift+k];
            result.pair_values[p].metric[Buckle+k] = cehs.metric[Tilt+k];
        }
        pair_available[p] = 1;
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        const auto step = steps[s];
        auto& quality = result.step_quality[s];
        quality = result.pair_quality[step.a] | result.pair_quality[step.b];
        if (!pair_available[step.a] || !pair_available[step.b] ||
            (policy == PairValues_RequireGoodQuality && quality != Quality_Good)) {
            quality |= Quality_AdjacentPair;
            continue;
        }
        auto a = result.pair_frames[step.a], b = result.pair_frames[step.b];
        vec3_t delta = vec3_sub(b.origin, a.origin);
        md_util_min_image_vec3(&delta, 1, &state.unitcell);
        b.origin = vec3_add(a.origin, delta);
        Values values = missing;
        if (!compute_3dna_step(&values, a, b)) {
            quality |= Quality_AmbiguousTransform;
            continue;
        }
        for (int k = Tilt; k <= (int)Rise; ++k) result.step_values[s].metric[k] = values.metric[k];
    }
    *out = std::move(result);
    return true;
}

static bool apply_3dna_helical(FrameAnalysis* out, const md_system_t& sys, const md_system_state_t& state,
                           const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs,
                           const std::vector<StepIndex>& steps) {
    if (!out || out->pair_values.size() != pairs.size() || out->pair_quality.size() != pairs.size() ||
        out->pair_frames.size() != pairs.size() || out->step_values.size() != steps.size() ||
        out->step_quality.size() != steps.size()) {
        MD_LOG_ERROR("Lavery: invalid input for 3DNA-style parameters");
        return false;
    }
    std::vector<BaseFrame> frames;
    std::vector<uint16_t> quality;
    if (!extract_3dna_base_frames(frames, sys, state, bases, &quality)) return false;
    out->helical_pair_frames.assign(pairs.size(), {});
    out->helical_quality.assign(steps.size(), Quality_AdjacentPair);
    std::vector<uint16_t> pair_flags = out->pair_quality;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto pair = pairs[p];
        if (pair.a >= frames.size() || pair.b >= frames.size()) {
            MD_LOG_ERROR("Lavery: invalid nucleotide index for 3DNA-style parameters");
            return false;
        }
        pair_flags[p] |= quality[pair.a] | quality[pair.b];
        if (pair_flags[p]) continue;
        Values v = {};
        if (!compute_3dna_pair(&v, &out->helical_pair_frames[p], frames[pair.a], frames[pair.b], &state.unitcell)) {
            pair_flags[p] |= Quality_AmbiguousTransform;
            continue;
        }
    }
    for (size_t s = 0; s < steps.size(); ++s) {
        const auto step = steps[s];
        if (step.a >= pairs.size() || step.b >= pairs.size()) {
            MD_LOG_ERROR("Lavery: invalid step index for 3DNA-style parameters");
            return false;
        }
        auto& quality_flags = out->helical_quality[s];
        quality_flags = out->step_quality[s] | pair_flags[step.a] | pair_flags[step.b];
        for (int k = XDisplacement; k <= (int)HelicalTwist; ++k) out->step_values[s].metric[k] = nan;
        if (quality_flags) {
            quality_flags |= Quality_AdjacentPair;
            continue;
        }
        BasePairFrame a = out->helical_pair_frames[step.a], b = out->helical_pair_frames[step.b];
        vec3_t delta = vec3_sub(b.origin, a.origin);
        md_util_min_image_vec3(&delta, 1, &state.unitcell);
        b.origin = vec3_add(a.origin, delta);
        if (!compute_helical_step(&out->step_values[s], a, b)) quality_flags |= Quality_AmbiguousTransform;
    }
    return true;
}

static void apply_groove_widths(FrameAnalysis* out, const md_system_state_t& state,
                                const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs,
                                const std::vector<StepIndex>& steps, GrooveWidthPolicy policy) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const vec3_t none = {nan, nan, nan};
    auto coord = [&](int32_t atom) { return atom >= 0 ? md_state_coord(&state, atom) : none; };
    std::vector<int64_t> step_from(pairs.size(), -1);
    for (size_t s = steps.size(); s-- > 0;) if (steps[s].a < pairs.size()) step_from[steps[s].a] = (int64_t)s;
    std::vector<vec3_t> p1, p2, c1, c2;
    std::vector<float> minor, major;
    for (const auto& chain : helix_chains(pairs.size(), steps)) {
        const size_t n = chain.size();
        const uint32_t strand = bases[pairs[chain[0]].a].strand;
        p1.assign(n, none);
        p2.assign(n, none);
        c1.assign(n, none);
        c2.assign(n, none);
        for (size_t k = 0; k < n; ++k) {
            const auto& a = bases[pairs[chain[k]].a];
            const auto& b = bases[pairs[chain[k]].b];
            if ((a.strand == strand) == (b.strand == strand)) continue;
            const auto& first = a.strand == strand ? a : b;
            const auto& second = a.strand == strand ? b : a;
            p1[k] = coord(first.backbone[BB_P]); p2[k] = coord(second.backbone[BB_P]);
            c1[k] = coord(first.backbone[BB_C1]); c2[k] = coord(second.backbone[BB_C1]);
        }
        minor.assign(n, nan);
        major.assign(n, nan);
        groove_widths(minor.data(), major.data(), p1.data(), p2.data(), n, &state.unitcell, c1.data(), c2.data(), policy);
        for (size_t k = 0; k + 1 < n; ++k) {
            const int64_t s = step_from[chain[k]];
            if (s < 0 || steps[s].b != chain[k + 1]) continue;
            out->step_values[s].metric[MinorGroove] = minor[k];
            out->step_values[s].metric[MajorGroove] = major[k];
        }
    }
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
    std::vector<BasePairFrame> orientation_frames(pairs.size());
    std::vector<uint8_t> orientation_valid(pairs.size(), 0);
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto pair = pairs[p];
        if (pair.a >= bases.size() || pair.b >= bases.size()) {
            MD_LOG_ERROR("Lavery: invalid nucleotide index during frame analysis");
            return false;
        }
        uint16_t geometry = 0;
        BasePairFrame frame;
        Values values;
        if (!base_quality[pair.a] && !base_quality[pair.b]) {
            auto b = frames[pair.b];
            vec3_t delta = vec3_sub(b.origin, frames[pair.a].origin);
            md_util_min_image_vec3(&delta, 1, &state.unitcell);
            b.origin = vec3_add(frames[pair.a].origin, delta);
            Values unused;
            orientation_valid[p] = compute_base_pair(&unused, &orientation_frames[p], frames[pair.a], b, pair.reversed);
        }
        const bool present = compute_present_base_pair(&values, &frame, &geometry,
            frames[pair.a], frames[pair.b], &state.unitcell, pair.reversed);
        out->pair_quality[p] |= geometry | base_quality[pair.a] | base_quality[pair.b];
        if (present && out->pair_quality[p] == Quality_Good) {
            out->pair_values[p] = values;
            out->pair_frames[p] = frame;
        }
    }
    out->pair_inversions = curves_pair_inversions(orientation_frames, orientation_valid, steps, &state.unitcell);
    for (size_t p = 0; p < pairs.size(); ++p) {
        if (out->pair_quality[p] != Quality_Good) continue;
        if (out->pair_inversions[p] > 0) {
            out->pair_values[p].metric[Shear] = -out->pair_values[p].metric[Shear];
            out->pair_values[p].metric[Buckle] = -out->pair_values[p].metric[Buckle];
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
        if (compute_step(&values, a, b)) {
            if (out->pair_inversions[step.a] > 0) {
                values.metric[Rise] = -values.metric[Rise];
                values.metric[Twist] = -values.metric[Twist];
            }
            out->step_values[s] = values;
        }
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
    std::vector<uint16_t> geometry(pairs.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto pair = pairs[p];
        geometry[p] = pair_quality(frames[pair.a], frames[pair.b], &state.unitcell) |
            base_quality[pair.a] | base_quality[pair.b];
        out->pairing_states[p] = classify_pairing(evidence[p], out->nucleotide_values[2*p].metric[Chi],
            out->nucleotide_values[2*p+1].metric[Chi], geometry[p]);
    }
    std::vector<uint8_t> switched;
    if (!detect_partner_switches(&switched, frames, pairs, geometry, &state.unitcell)) return false;
    for (size_t p = 0; p < pairs.size(); ++p) if (switched[p]) out->pairing_states[p] = Pairing_PartnerSwitch;
    apply_groove_widths(out, state, bases, pairs, steps, Groove_DirectFallback);
    if (!apply_3dna_helical(out, sys, state, bases, pairs, steps)) return false;
    for (size_t p = 0; p < pairs.size(); ++p) {
        if (out->pair_quality[p] == Quality_Good && out->pair_inversions[p] < 0) {
            out->pair_quality[p] |= Quality_AmbiguousTransform;
            out->pair_values[p] = missing;
            out->pair_frames[p] = {};
        }
    }
    return true;
}

static bool antiparallel_helix_steps(const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs,
                                     const std::vector<StepIndex>& steps) {
    for (const auto& pair : pairs) if (pair.a >= bases.size() || pair.b >= bases.size()) {
        MD_LOG_ERROR("Lavery: invalid nucleotide index in helix topology");
        return false;
    }
    std::vector<uint8_t> incoming(pairs.size(), 0), outgoing(pairs.size(), 0);
    for (const auto& step : steps) {
        if (step.a >= pairs.size() || step.b >= pairs.size()) {
            MD_LOG_ERROR("Lavery: invalid helix step index");
            return false;
        }
        const auto a = pairs[step.a], b = pairs[step.b];
        const auto& a0 = bases[a.a]; const auto& b0 = bases[b.a];
        const auto& a1 = bases[a.b]; const auto& b1 = bases[b.b];
        if (a0.strand != b0.strand || a1.strand != b1.strand || a0.strand == a1.strand ||
            a0.chain_order == UINT32_MAX || b0.chain_order == UINT32_MAX ||
            a1.chain_order == UINT32_MAX || b1.chain_order == UINT32_MAX ||
            b0.chain_order != a0.chain_order + 1 || a1.chain_order != b1.chain_order + 1 ||
            outgoing[step.a]++ || incoming[step.b]++) {
            MD_LOG_ERROR("Lavery: geometry requires non-circular antiparallel helix steps");
            return false;
        }
    }
    return true;
}

bool initialize_curves_trajectory(CurvesTrajectoryAnalysis* out, size_t frames, size_t pair_count,
                                  const std::vector<StepIndex>& steps,
                                  const CurvesFrameAnalysis& first_frame) {
    if (!out || !frames ||
        first_frame.frame.pair_values.size() != pair_count || first_frame.frame.step_values.size() != steps.size()) {
        MD_LOG_ERROR("Lavery: invalid Curves+ trajectory dimensions");
        return false;
    }
    CurvesTrajectoryAnalysis result;
    result.frames = frames;
    result.pairs = pair_count;
    result.steps = steps.size();
    result.helices.reserve(first_frame.helices.size());
    std::vector<uint8_t> seen_pairs(pair_count, 0), seen_steps(steps.size(), 0);
    size_t groove_count = 0;
    for (const auto& geometry : first_frame.helices) {
        const size_t n = geometry.pairs.size();
        if (n < 2) {
            MD_LOG_ERROR("Lavery: invalid Curves+ helix length for trajectory storage");
            return false;
        }
        for (size_t i = 0; i < n; ++i) {
            const uint32_t pair = geometry.pairs[i];
            if (pair >= pair_count || seen_pairs[pair]) {
                MD_LOG_ERROR("Lavery: invalid or duplicate pair in Curves+ trajectory helix");
                return false;
            }
            seen_pairs[pair] = 1;
            if (i + 1 == n) continue;
            size_t match = steps.size();
            for (size_t s = 0; s < steps.size(); ++s) {
                if (steps[s].a == pair && steps[s].b == geometry.pairs[i + 1]) {
                    match = s;
                    break;
                }
            }
            if (match == steps.size() || seen_steps[match]) {
                MD_LOG_ERROR("Lavery: missing or duplicate Curves+ helix step in trajectory layout");
                return false;
            }
            seen_steps[match] = 1;
        }
        CurvesTrajectoryAnalysis::Helix layout;
        layout.pairs = geometry.pairs;
        for (size_t i = 0; i + 1 < n; ++i) {
            const auto pair = geometry.pairs[i];
            size_t match = steps.size();
            for (size_t s = 0; s < steps.size(); ++s)
                if (steps[s].a == pair && steps[s].b == geometry.pairs[i + 1]) { match = s; break; }
            layout.steps.push_back(match);
        }
        layout.groove_offset = groove_count;
        layout.groove_count = n >= 4 ? 2 * n - 3 : 0;
        if (!geometry.grooves.empty() && geometry.grooves.size() != layout.groove_count) {
            MD_LOG_ERROR("Lavery: inconsistent Curves+ groove count in first trajectory frame");
            return false;
        }
        if (geometry.axis_quality == Quality_Good &&
            (geometry.axis.size() != n || geometry.pair_parameters.size() != n ||
             geometry.step_parameters.size() != n - 1)) {
            MD_LOG_ERROR("Lavery: incomplete Curves+ axis geometry in first trajectory frame");
            return false;
        }
        if (geometry.groove_quality == Quality_Good && layout.groove_count &&
            geometry.grooves.size() != layout.groove_count) {
            MD_LOG_ERROR("Lavery: incomplete Curves+ grooves in first trajectory frame");
            return false;
        }
        groove_count += layout.groove_count;
        result.helices.push_back(std::move(layout));
    }
    if (std::find(seen_steps.begin(), seen_steps.end(), uint8_t(0)) != seen_steps.end()) {
        MD_LOG_ERROR("Lavery: Curves+ trajectory contains steps outside ordered helices");
        return false;
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    result.pair_axis.assign(frames * pair_count, {nan, nan, nan, nan, nan});
    result.step_axis.assign(frames * steps.size(), {nan, nan});
    result.grooves.resize(frames * groove_count);
    for (size_t f = 0; f < frames; ++f) {
        for (const auto& helix : result.helices)
            for (size_t g = 0; g < helix.groove_count; ++g)
                result.grooves[f * groove_count + helix.groove_offset + g] =
                    {1.5f + 0.5f * (float)g, nan, nan, nan, nan};
    }
    result.axis_quality.assign(frames * result.helices.size(), Quality_Good);
    result.groove_quality.assign(frames * result.helices.size(), Quality_Good);
    *out = std::move(result);
    return true;
}

bool store_curves_trajectory_frame(CurvesTrajectoryAnalysis* trajectory, size_t frame,
                                   const CurvesFrameAnalysis& analysis) {
    if (!trajectory || frame >= trajectory->frames ||
        analysis.helices.size() != trajectory->helices.size() ||
        analysis.frame.pair_values.size() != trajectory->pairs ||
        analysis.frame.step_values.size() != trajectory->steps) {
        MD_LOG_ERROR("Lavery: invalid Curves+ frame for trajectory storage");
        return false;
    }
    size_t groove_count = 0;
    for (const auto& helix : trajectory->helices) groove_count += helix.groove_count;
    std::vector<uint8_t> seen_pairs(trajectory->pairs, 0), seen_steps(trajectory->steps, 0);
    for (size_t h = 0; h < trajectory->helices.size(); ++h) {
        const auto& layout = trajectory->helices[h];
        const auto& geometry = analysis.helices[h];
        const size_t n = layout.pairs.size();
        if (n < 2 || layout.steps.size() != n - 1 || geometry.pairs != layout.pairs ||
            (!geometry.grooves.empty() && geometry.grooves.size() != layout.groove_count) ||
            (geometry.axis_quality == Quality_Good &&
             (geometry.axis.size() != n || geometry.pair_parameters.size() != n ||
              geometry.step_parameters.size() != n - 1)) ||
            (geometry.groove_quality == Quality_Good && layout.groove_count &&
             geometry.grooves.size() != layout.groove_count)) {
            MD_LOG_ERROR("Lavery: Curves+ frame geometry does not match trajectory layout");
            return false;
        }
        for (uint32_t pair : layout.pairs) {
            if (pair >= trajectory->pairs || seen_pairs[pair]++) {
                MD_LOG_ERROR("Lavery: invalid or duplicate pair in Curves+ trajectory frame");
                return false;
            }
        }
        for (size_t step : layout.steps) {
            if (step >= trajectory->steps || seen_steps[step]++) {
                MD_LOG_ERROR("Lavery: invalid or duplicate step in Curves+ trajectory frame");
                return false;
            }
        }
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const size_t pair_offset = frame * trajectory->pairs;
    const size_t step_offset = frame * trajectory->steps;
    const size_t groove_offset = frame * groove_count;
    const size_t helix_offset = frame * trajectory->helices.size();
    std::fill(trajectory->pair_axis.begin() + pair_offset, trajectory->pair_axis.begin() + pair_offset + trajectory->pairs,
              CurvesPairAxisParameters{nan, nan, nan, nan, nan});
    std::fill(trajectory->step_axis.begin() + step_offset, trajectory->step_axis.begin() + step_offset + trajectory->steps,
              CurvesStepAxisParameters{nan, nan});
    for (size_t h = 0; h < trajectory->helices.size(); ++h) {
        const auto& layout = trajectory->helices[h];
        const auto& geometry = analysis.helices[h];
        trajectory->axis_quality[helix_offset + h] = geometry.axis_quality;
        trajectory->groove_quality[helix_offset + h] = geometry.groove_quality;
        if (geometry.axis_quality == Quality_Good) {
            for (size_t i = 0; i < layout.pairs.size(); ++i)
                trajectory->pair_axis[pair_offset + layout.pairs[i]] = geometry.pair_parameters[i];
            for (size_t i = 0; i < layout.steps.size(); ++i)
                trajectory->step_axis[step_offset + layout.steps[i]] = geometry.step_parameters[i];
        }
        if (geometry.groove_quality == Quality_Good) {
            std::copy(geometry.grooves.begin(), geometry.grooves.end(),
                      trajectory->grooves.begin() + groove_offset + layout.groove_offset);
        } else {
            for (size_t i = 0; i < layout.groove_count; ++i)
                trajectory->grooves[groove_offset + layout.groove_offset + i] =
                    {1.5f + 0.5f * (float)i, nan, nan, nan, nan};
        }
    }
    return true;
}

bool analyze_3dna_frame(FrameAnalysis* out, const md_system_t& sys, const md_system_state_t& state,
                        const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs,
                        const std::vector<StepIndex>& steps, const PairingValidator& validator) {
    if (!out) {
        MD_LOG_ERROR("Lavery: invalid 3DNA frame analysis output");
        return false;
    }
    if (!antiparallel_helix_steps(bases, pairs, steps)) return false;
    ThreeDnaPairStepAnalysis dna;
    if (!analyze_3dna_pair_step(&dna, sys, state, bases, pairs, steps, validator,
                                PairValues_ReferenceWithQualityFlags)) return false;
    FrameAnalysis result;
    if (!analyze_frame(&result, sys, state, bases, pairs, steps, validator)) return false;
    result.pair_frames = dna.pair_frames;
    result.helical_pair_frames = dna.pair_frames;
    result.pair_values = std::move(dna.pair_values);
    result.step_values = std::move(dna.step_values);
    result.pair_quality = std::move(dna.pair_quality);
    result.step_quality = std::move(dna.step_quality);
    result.helical_quality = result.step_quality;
    result.pair_inversions.clear();
    for (size_t s = 0; s < steps.size(); ++s) {
        if (!std::isfinite(result.step_values[s].metric[Tilt])) continue;
        auto a = result.pair_frames[steps[s].a], b = result.pair_frames[steps[s].b];
        vec3_t delta = vec3_sub(b.origin, a.origin);
        md_util_min_image_vec3(&delta, 1, &state.unitcell);
        b.origin = vec3_add(a.origin, delta);
        if (!compute_helical_step(&result.step_values[s], a, b))
            result.helical_quality[s] |= Quality_AmbiguousTransform;
    }
    apply_groove_widths(&result, state, bases, pairs, steps, Groove_3DnaRefined);
    *out = std::move(result);
    return true;
}

bool analyze_curves_helices(std::vector<CurvesHelixGeometry>* out, const md_system_t& sys,
                            const md_system_state_t& state, const std::vector<BaseTopology>& bases,
                            const std::vector<Pair>& pairs, const std::vector<StepIndex>& steps) {
    if (!out) {
        MD_LOG_ERROR("Lavery: invalid Curves+ helix output");
        return false;
    }
    if (!antiparallel_helix_steps(bases, pairs, steps)) return false;
    std::vector<BaseFrame> frames;
    std::vector<uint16_t> quality;
    if (!extract_base_frames(frames, sys, state, bases, &quality)) return false;
    std::vector<CurvesHelixGeometry> result;
    for (const auto& chain : helix_chains(pairs.size(), steps)) {
        CurvesHelixGeometry helix;
        helix.pairs = chain;
        const size_t n = chain.size();
        std::vector<BaseFrame> first, second;
        std::vector<BasePairFrame> midpoints;
        first.reserve(n); second.reserve(n); midpoints.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const auto pair = pairs[chain[i]];
            helix.axis_quality |= quality[pair.a] | quality[pair.b];
            if (helix.axis_quality) break;
            auto a = frames[pair.a], b = frames[pair.b];
            if (i) {
                vec3_t delta = vec3_sub(a.origin, first.back().origin);
                md_util_min_image_vec3(&delta, 1, &state.unitcell);
                const vec3_t shift = vec3_sub(vec3_add(first.back().origin, delta), a.origin);
                a.origin = vec3_add(a.origin, shift);
                a.groove_reference = vec3_add(a.groove_reference, shift);
            }
            vec3_t delta = vec3_sub(b.origin, a.origin);
            md_util_min_image_vec3(&delta, 1, &state.unitcell);
            const vec3_t shift = vec3_sub(vec3_add(a.origin, delta), b.origin);
            b.origin = vec3_add(b.origin, shift);
            b.groove_reference = vec3_add(b.groove_reference, shift);
            if (i) {
                vec3_t link = vec3_sub(b.origin, second.back().origin);
                vec3_t nearest = link;
                md_util_min_image_vec3(&nearest, 1, &state.unitcell);
                if (vec3_length(vec3_sub(nearest, link)) > 0.01f) {
                    helix.axis_quality |= Quality_AmbiguousTransform;
                    break;
                }
            }
            BasePairFrame midpoint;
            Values unused;
            if (!compute_base_pair(&unused, &midpoint, a, b, pair.reversed)) {
                helix.axis_quality |= Quality_AmbiguousTransform;
                break;
            }
            first.push_back(a); second.push_back(b); midpoints.push_back(midpoint);
        }
        if (!helix.axis_quality) {
            std::vector<StepIndex> adjacent;
            for (uint32_t i = 0; i + 1 < n; ++i) adjacent.push_back({i, i + 1});
            const std::vector<uint8_t> available(n, 1);
            const auto inversions = curves_pair_inversions(midpoints, available, adjacent);
            if (!curves_duplex_axis(&helix.axis, first, second, midpoints) ||
                !curves_pair_axis_parameters(&helix.pair_parameters, midpoints, helix.axis, inversions) ||
                !curves_step_axis_parameters(&helix.step_parameters, midpoints, helix.axis, inversions)) {
                helix.axis_quality |= Quality_AmbiguousTransform;
                helix.axis.clear();
                helix.pair_parameters.clear();
                helix.step_parameters.clear();
            }
        }
        helix.groove_quality = helix.axis_quality;
        if (!helix.axis_quality) {
            std::array<std::vector<vec3_t>, 2> phosphates, backbones, depths;
            for (size_t i = 0; i < n; ++i) {
                const auto pair = pairs[chain[i]];
                depths[0].push_back(first[i].groove_reference);
                depths[1].push_back(second[i].groove_reference);
                for (int side = 0; side < 2; ++side) {
                    if ((!side && i == 0) || (side && i + 1 == n)) continue;
                    const uint32_t base = side ? pair.b : pair.a;
                    const int32_t atom = bases[base].backbone[BB_P];
                    if (atom < 0) {
                        helix.groove_quality |= Quality_MissingAtoms;
                        continue;
                    }
                    vec3_t delta = vec3_sub(md_state_coord(&state, atom), frames[base].origin);
                    md_util_min_image_vec3(&delta, 1, &state.unitcell);
                    phosphates[side].push_back(vec3_add(side ? second[i].origin : first[i].origin, delta));
                }
            }
            if (!helix.groove_quality &&
                (!curves_groove_backbone(&backbones[0], phosphates[0]) ||
                 !curves_groove_backbone(&backbones[1], phosphates[1]) ||
                 !curves_grooves(&helix.grooves, backbones[0], backbones[1], midpoints, helix.axis, depths[0], depths[1]))) {
                helix.groove_quality |= Quality_AmbiguousTransform;
            }
        }
        result.push_back(std::move(helix));
    }
    *out = std::move(result);
    return true;
}

bool analyze_curves_frame(CurvesFrameAnalysis* out, const md_system_t& sys, const md_system_state_t& state,
                          const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs,
                          const std::vector<StepIndex>& steps, const PairingValidator& validator) {
    if (!out) {
        MD_LOG_ERROR("Lavery: invalid Curves+ frame analysis output");
        return false;
    }
    CurvesFrameAnalysis result;
    if (!analyze_curves_helices(&result.helices, sys, state, bases, pairs, steps) ||
        !analyze_frame(&result.frame, sys, state, bases, pairs, steps, validator)) return false;
    auto& frame = result.frame;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Values missing;
    std::fill(std::begin(missing.metric), std::end(missing.metric), nan);
    std::vector<BaseFrame> base_frames;
    std::vector<uint16_t> base_quality, contact_quality;
    std::vector<PairingEvidence> evidence;
    if (!extract_base_frames(base_frames, sys, state, bases, &base_quality) ||
        !validator.evaluate(contact_quality, frame.contacts, state, &evidence)) return false;
    frame.pair_quality = contact_quality;
    frame.pair_values.assign(pairs.size(), missing);
    frame.pair_frames.assign(pairs.size(), {});
    std::vector<uint8_t> available(pairs.size(), 0);
    std::vector<uint16_t> geometry(pairs.size());
    for (size_t p = 0; p < pairs.size(); ++p) {
        const auto pair = pairs[p];
        geometry[p] = pair_quality(base_frames[pair.a], base_frames[pair.b], &state.unitcell) |
                      base_quality[pair.a] | base_quality[pair.b];
        frame.pair_quality[p] |= geometry[p];
        if (base_quality[pair.a] || base_quality[pair.b]) continue;
        auto b = base_frames[pair.b];
        vec3_t delta = vec3_sub(b.origin, base_frames[pair.a].origin);
        md_util_min_image_vec3(&delta, 1, &state.unitcell);
        b.origin = vec3_add(base_frames[pair.a].origin, delta);
        if (!compute_base_pair(&frame.pair_values[p], &frame.pair_frames[p],
                               base_frames[pair.a], b, pair.reversed)) {
            frame.pair_quality[p] |= Quality_AmbiguousTransform;
            frame.pair_values[p] = missing;
            continue;
        }
        available[p] = 1;
    }
    frame.pair_inversions = curves_pair_inversions(frame.pair_frames, available, steps, &state.unitcell);
    for (size_t p = 0; p < pairs.size(); ++p) {
        if (!available[p]) continue;
        if (frame.pair_inversions[p] < 0) {
            frame.pair_quality[p] |= Quality_AmbiguousTransform;
            frame.pair_values[p] = missing;
        } else if (frame.pair_inversions[p] > 0) {
            frame.pair_values[p].metric[Shear] = -frame.pair_values[p].metric[Shear];
            frame.pair_values[p].metric[Buckle] = -frame.pair_values[p].metric[Buckle];
        }
    }
    frame.step_values.assign(steps.size(), missing);
    frame.step_quality.assign(steps.size(), Quality_Good);
    for (size_t s = 0; s < steps.size(); ++s) {
        const auto step = steps[s];
        frame.step_quality[s] = frame.pair_quality[step.a] | frame.pair_quality[step.b];
        if (!available[step.a] || !available[step.b] || frame.pair_inversions[step.a] < 0) {
            frame.step_quality[s] |= Quality_AdjacentPair;
            continue;
        }
        auto a = frame.pair_frames[step.a], b = frame.pair_frames[step.b];
        vec3_t delta = vec3_sub(b.origin, a.origin);
        md_util_min_image_vec3(&delta, 1, &state.unitcell);
        b.origin = vec3_add(a.origin, delta);
        if (!compute_step(&frame.step_values[s], a, b)) {
            frame.step_quality[s] |= Quality_AmbiguousTransform;
            continue;
        }
        if (frame.pair_inversions[step.a] > 0) {
            frame.step_values[s].metric[Rise] = -frame.step_values[s].metric[Rise];
            frame.step_values[s].metric[Twist] = -frame.step_values[s].metric[Twist];
        }
    }
    frame.helical_pair_frames.clear();
    frame.helical_quality.clear();
    for (auto& values : frame.step_values) {
        values.metric[MinorGroove] = values.metric[MajorGroove] = nan;
        for (int id = XDisplacement; id <= (int)HelicalTwist; ++id) values.metric[id] = nan;
    }
    std::vector<uint8_t> previous(2 * pairs.size()), next(2 * pairs.size());
    for (const auto& step : steps) {
        for (int side = 0; side < 2; ++side) {
            const auto& pa = pairs[step.a]; const auto& pb = pairs[step.b];
            const auto& a = bases[side ? pa.b : pa.a];
            const auto& b = bases[side ? pb.b : pb.a];
            if (a.chain_order < b.chain_order) {
                next[2*step.a + side] = 1;
                previous[2*step.b + side] = 1;
            } else {
                previous[2*step.a + side] = 1;
                next[2*step.b + side] = 1;
            }
        }
    }
    for (size_t p = 0; p < pairs.size(); ++p) for (int side = 0; side < 2; ++side) {
        const auto& base = bases[side ? pairs[p].b : pairs[p].a];
        vec3_t atoms[BB_Count];
        uint32_t present = 0;
        for (int k = 0; k < BB_Count; ++k) {
            const int32_t atom = base.backbone[k];
            atoms[k] = atom >= 0 ? md_state_coord(&state, atom) : vec3_t{nan, nan, nan};
            if (atom >= 0) present |= 1u << k;
        }
        const size_t member = 2*p + side;
        compute_curves_nucleotide(&frame.nucleotide_values[member], atoms, &state.unitcell,
                                  &frame.nucleotide_quality[member], present,
                                  previous[member], next[member]);
    }
    for (size_t p = 0; p < pairs.size(); ++p) {
        frame.pairing_states[p] = classify_pairing(evidence[p], frame.nucleotide_values[2*p].metric[Chi],
                                                    frame.nucleotide_values[2*p+1].metric[Chi], geometry[p]);
    }
    std::vector<uint8_t> switched;
    if (!detect_partner_switches(&switched, base_frames, pairs, geometry, &state.unitcell)) return false;
    for (size_t p = 0; p < pairs.size(); ++p) if (switched[p]) frame.pairing_states[p] = Pairing_PartnerSwitch;
    *out = std::move(result);
    return true;
}

bool analyze_convention_frame(ConventionFrameAnalysis* out, AnalysisConvention convention,
                              const md_system_t& sys, const md_system_state_t& state,
                              const std::vector<BaseTopology>& bases, const std::vector<Pair>& pairs,
                              const std::vector<StepIndex>& steps, const PairingValidator& validator) {
    if (!out) {
        MD_LOG_ERROR("Lavery: invalid convention frame output");
        return false;
    }
    switch (convention) {
    case AnalysisConvention::LegacyMixed: {
        FrameAnalysis result;
        if (!analyze_frame(&result, sys, state, bases, pairs, steps, validator)) return false;
        *out = std::move(result);
        return true;
    }
    case AnalysisConvention::Curves: {
        CurvesFrameAnalysis result;
        if (!analyze_curves_frame(&result, sys, state, bases, pairs, steps, validator)) return false;
        *out = std::move(result);
        return true;
    }
    case AnalysisConvention::ThreeDna: {
        FrameAnalysis result;
        if (!analyze_3dna_frame(&result, sys, state, bases, pairs, steps, validator)) return false;
        *out = std::move(result);
        return true;
    }
    }
    MD_LOG_ERROR("Lavery: unknown analysis convention");
    return false;
}

} // namespace lavery
