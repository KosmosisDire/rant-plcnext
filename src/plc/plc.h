#pragma once
// PLCnext service handles and the RSC type aliases shared by every mirror file.

#include "Arp/Plc/Meta/Services/IVariableBrowseService.hpp"
#include "Arp/Plc/Gds/Services/ISubscriptionService.hpp"
#include "Arp/Plc/Gds/Services/IDataAccessService.hpp"
#include "Arp/Plc/Gds/Services/IForceService.hpp"
#include "Arp/Base/Rsc/Commons/RscString.hxx"
#include "Arp/Base/Rsc/Commons/RscVariant.hxx"

#include <chrono>
#include <cstdint>

namespace plc {

// The service delegates hard code RscVariant<512> and RscString<512>, so everything uses 512.
constexpr int RSC_BUF_SIZE = 512;
using RscVar = Arp::Base::Rsc::Commons::RscVariant<RSC_BUF_SIZE>;
using RscStr = Arp::Base::Rsc::Commons::RscString<RSC_BUF_SIZE>;
using Arp::Base::Rsc::Commons::IRscReadEnumerator;
using Arp::Base::Rsc::Commons::IRscWriteEnumerator;
using Arp::Base::Rsc::Commons::RscType;

// Set by the mirror thread as it starts. Any may be null when the firmware lacks that service.
extern Arp::Plc::Meta::Services::IVariableBrowseService* g_browseSvc;
extern Arp::Plc::Gds::Services::ISubscriptionService*    g_subSvc;
extern Arp::Plc::Gds::Services::IDataAccessService*      g_dataAccessSvc;
extern Arp::Plc::Gds::Services::IForceService*           g_forceSvc;

// Microseconds since the Unix epoch: the same clock as the task timestamps
// PLCnext returns from ReadTimeStampedValues, so the two compare directly.
inline int64_t now_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace plc
