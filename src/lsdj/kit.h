#ifndef LSDJ_KIT_H
#define LSDJ_KIT_H

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

#include "liblsdj/error.h"
#include "liblsdj/vio.h"
#include "sample.h"

#define KIT_SAMPLE_COUNT (15)

typedef struct lsdj_kit_t lsdj_kit_t;

lsdj_kit_t* lsdj_kit_read(lsdj_vio_t* vio, lsdj_error_t** error);

lsdj_sample_t* lsdj_kit_get_sample(lsdj_kit_t* kit, size_t idx);
const char* lsdj_kit_get_name(lsdj_kit_t* kit);

void lsdj_kit_free(lsdj_kit_t* kit);

#ifdef __cplusplus
}
#endif

#endif
