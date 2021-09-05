#include "../../../git-compat-util.h"
#include "../../../object.h"

char *obj_to_hex_oid(struct object *obj) {
    return oid_to_hex(&obj->oid);
}
