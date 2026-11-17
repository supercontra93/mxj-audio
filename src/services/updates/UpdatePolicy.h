// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

/** Update system switch.

    The update check (GitHub Releases + Ed25519 signed manifest, master plan section 8)
    is built in phase 8. Until then, and in every development build after that,
    the program must not contact any update server.

    MXJ_ENABLE_UPDATES comes from the CMake option of the same name (default OFF).
    Only the official release workflow turns it on.
*/
#ifndef MXJ_ENABLE_UPDATES
 #define MXJ_ENABLE_UPDATES 0
#endif

namespace mxj::updates
{
    inline constexpr bool enabled = (MXJ_ENABLE_UPDATES != 0);
}
