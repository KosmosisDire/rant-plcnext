#include "mirror/sync.h"

#include "plc/codec.h"
#include "core/log.h"

#include "Arp/Plc/Gds/Services/WriteItem.hpp"
#include "Arp/Plc/Gds/Services/ForceItem.hpp"
#include "Arp/Plc/Gds/Services/DataAccessError.hpp"

#include <cstring>
#include <optional>
#include <sstream>

namespace plc {

namespace {

using Arp::Plc::Gds::Services::IDataAccessService;
using Arp::Plc::Gds::Services::WriteItem;
using Arp::Plc::Gds::Services::ForceItem;
using Arp::Plc::Gds::Services::DataAccessError;

bool SameAsLast(const GdsVar& gv, const std::vector<uint8_t>& buf)
{
    return gv.hasValue && gv.lastValue.size() == buf.size() &&
           std::memcmp(gv.lastValue.data(), buf.data(), buf.size()) == 0;
}

bool MatchesSchema(const GdsVar& gv, const std::vector<uint8_t>& buf)
{
    // A fixed schema's byte count must match or a typed subscriber drops the
    // message. The unbounded string schema reports size 0: self-framed, no check.
    return gv.schema.empty() || gv.schema.size() == 0 || buf.size() == gv.schema.size();
}

void Remember(GdsVar& gv, const std::vector<uint8_t>& buf)
{
    gv.lastValue = buf;
    gv.hasValue  = true;
}

// PLC write back primitives

// AddVariable both starts a force and updates an already-forced value.
void ForcePlc(const WriteList& items)
{
    if (!g_forceSvc) return;
    for (const auto& [name, value] : items) {
        ForceItem fi;
        fi.VariableName = RscStr(name.c_str());
        fi.ForceValue   = value;
        try { g_forceSvc->AddVariable(fi); } catch (...) {}
    }
}

void UnforcePlc(const NameList& names)
{
    if (!g_forceSvc) return;
    for (const auto& n : names) {
        try { g_forceSvc->RemoveVariable(RscStr(n.c_str())); } catch (...) {}
    }
}

// Drop the RANT force and republish the live (now unforced) value.
void UnforceRant(GdsVar& gv, std::vector<uint8_t>& buf)
{
    gv.var.unforce();
    if (ReadLiveValue(gv, buf)) {
        gv.var.set(rant::Bytes(buf.data(), buf.size()));
        Remember(gv, buf);
    }
    gv.wasForced = false;
    gv.last_writeback_us = now_us();
}

} // namespace

std::string WritePlc(const WriteList& items)
{
    if (items.empty() || !g_dataAccessSvc) return "";
    std::vector<WriteItem> wi;
    wi.reserve(items.size());
    for (const auto& [name, value] : items) {
        WriteItem w;
        w.PortName = RscStr(name.c_str());
        w.Value    = value;
        wi.push_back(std::move(w));
    }
    std::string refused;
    try {
        using WD = IDataAccessService::WriteDataDelegate;
        using WR = IDataAccessService::WriteResultDelegate;
        WD wd = WD::create([&wi](IRscWriteEnumerator<WriteItem>& writer) {
            writer.BeginWrite(wi.size());
            for (const auto& w : wi) writer.WriteNext(w);
            writer.EndWrite();
        });
        // One result per item, in item order.
        WR wr = WR::create([&items, &refused](IRscReadEnumerator<DataAccessError>& reader) {
            reader.BeginRead();
            DataAccessError err;
            for (size_t i = 0; reader.ReadNext(err); ++i) {
                if (err == DataAccessError::None || !refused.empty() || i >= items.size()) continue;
                std::ostringstream os;
                os << items[i].first << " refused (" << err << ")";
                refused = os.str();
            }
            reader.EndRead();
        });
        g_dataAccessSvc->Write(wd, wr);
    } catch (...) { return "the GDS write failed"; }
    return refused;
}

NameIndex BuildNameIndex(const std::vector<GdsVar>& vars)
{
    NameIndex names;
    for (size_t i = 0; i < vars.size(); ++i) {
        names[vars[i].name] = static_cast<int>(i);
        if (!vars[i].isStruct) continue;
        NameList leaves;
        MemberForceNames(vars[i].fields, leaves);
        for (auto& leaf : leaves) names[leaf] = static_cast<int>(i);
    }
    return names;
}

bool ReadLiveValue(const GdsVar& gv, std::vector<uint8_t>& buf)
{
    buf.clear();
    if (!g_dataAccessSvc) return false;
    try {
        auto item = g_dataAccessSvc->ReadSingle(RscStr(gv.name.c_str()));
        SerializeVar(buf, item.Value, gv);
    } catch (...) { buf.clear(); }
    if (buf.empty() || !MatchesSchema(gv, buf)) { buf.clear(); return false; }
    return true;
}

void PublishSample(GdsVar& gv, int64_t ts, const RscVar& value)
{
    // Every decline drains the variant: a sample left on the RSC stream corrupts every value after it.
    if (!gv.var) { DrainVariant(value); return; }

    // A forced variable's displayed value belongs to PollForces. This sample is
    // the PLC's raw program value, which forcing does not change.
    if (gv.var.forced()) { DrainVariant(value); return; }

    // Flicker guard: a sample captured at or before our last write-back is a
    // pre-write read that would echo the overwritten value for a cycle.
    if (ts != 0 && ts <= gv.last_writeback_us) { DrainVariant(value); return; }

    // Ordering guard: the slow snapshot can carry a value older than what the
    // fast drain already published, and must never move the value back.
    if (ts != 0 && ts <= gv.last_sample_us) { DrainVariant(value); return; }

    static std::vector<uint8_t> scratch;   // mirror thread only
    SerializeVar(scratch, value, gv);
    if (scratch.empty()) return;

    if (!MatchesSchema(gv, scratch)) {
        if (!gv.sizeWarned) {
            gv.sizeWarned = true;
            log::Error(gv.topic + " value is " + std::to_string(scratch.size())
                       + " bytes but schema expects " + std::to_string(gv.schema.size()) + "; skipping");
        }
        return;
    }
    if (ts != 0) gv.last_sample_us = ts;

    // Only a real change is published, which also keeps a remote write from echoing back.
    if (SameAsLast(gv, scratch)) return;

    gv.var.set(rant::Bytes(scratch.data(), scratch.size()));
    Remember(gv, scratch);
}

void PollForces(std::vector<GdsVar>& vars, const NameIndex& names)
{
    if (!g_forceSvc) return;

    bool active = false;
    try { active = g_forceSvc->IsActive(); } catch (...) { return; }

    std::vector<uint8_t> buf;
    if (!active) {
        for (auto& gv : vars) {
            if (gv.var && gv.var.forced()) UnforceRant(gv, buf);
        }
        return;
    }

    // Map each forced name, a scalar or an element such as MyArr[3], back to its variable. Only a
    // scalar's force value is exact (docs/design.md, keeping the two sides agreeing).
    std::vector<bool> forcedNow(vars.size(), false);
    std::vector<std::optional<RscVar>> forceValue(vars.size());
    try {
        for (const auto& fi : g_forceSvc->GetVariables()) {
            std::string name = fi.VariableName.CStr();
            auto it = names.find(name.substr(0, name.find('[')));
            if (it == names.end()) continue;
            forcedNow[it->second] = true;
            const auto& gv = vars[it->second];
            if (!gv.isArray && !gv.isStruct) forceValue[it->second] = fi.ForceValue;
        }
    } catch (...) { return; }

    for (size_t i = 0; i < vars.size(); ++i) {
        auto& gv = vars[i];
        if (!gv.var) continue;
        const bool rantForced = gv.var.forced();

        if (!forcedNow[i]) {
            if (rantForced) UnforceRant(gv, buf);
            continue;
        }
        if (!gv.forceable) continue;   // Rant force not enabled, so it cannot be reflected

        if (forceValue[i]) {
            SerializeVar(buf, *forceValue[i], gv);
            if (buf.empty() || !MatchesSchema(gv, buf)) continue;
        } else if (!ReadLiveValue(gv, buf)) {
            continue;
        }
        if (!rantForced || !SameAsLast(gv, buf)) {
            gv.var.force(rant::Bytes(buf.data(), buf.size()));
            Remember(gv, buf);
            gv.wasForced = true;
            gv.last_writeback_us = now_us();
        }
    }
}

// on_write fires on every applied write, so a re force to the same value is seen too.
void OnRemoteWrite(GdsVar& gv, const std::vector<uint8_t>& val, bool nowForced)
{

    if (nowForced || !gv.wasForced) {
        WriteList items;
        BuildWriteItems(gv, val.data(), val.size(), items);
        const char* what = nowForced ? "force" : "write";
        if (items.empty()) {
            log::Error(std::string("could not decode ") + what + " value for " + gv.name);
        } else {
            std::string refused;
            if (nowForced) ForcePlc(items); else refused = WritePlc(items);
            log::Info(std::string(what) + " " + gv.name + " from remote ("
                      + std::to_string(items.size()) + " leaf/leaves)");
            if (!refused.empty()) log::Error("write " + gv.name + ": " + refused);
        }
    } else {
        NameList names;
        ElementNames(gv, names);
        UnforcePlc(names);
        log::Info("unforce " + gv.name + " from remote");
    }

    gv.wasForced = nowForced;
    gv.lastValue = val;
    gv.hasValue = true;
    // The publish path ignores samples captured before the PLC applied this.
    gv.last_writeback_us = now_us();
}

} // namespace plc
