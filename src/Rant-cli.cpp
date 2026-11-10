// The library's hooks the eCLR calls as a program loads and unloads.
#include "eclr.h"
#include "Rant.h"

#include "blocks/runtime.h"
#include "mirror/mirror.h"

bool Rant::init()
{
    return true;
}

bool Rant::isCompatible(const EclrVersion& requestedVersion)
{
    return true;
}

bool Rant::load(ClrAppDomain* pAppDomain)
{
    return true;
}

// The node and its threads go with the program, so a download or a stop leaves nothing running.
bool Rant::unload(ClrAppDomain* pAppDomain)
{
    plc::StopMirror();
    blocks::Shutdown();
    return true;
}
