#include "migration.h"

int migration_target_version(int version)
{
    if (version == 1) {
        return 2;
    } else {
        return version;
    }
}
