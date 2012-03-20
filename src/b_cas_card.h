#ifndef B_CAS_CARD_H
#define B_CAS_CARD_H

#include "portable.h"

typedef struct {
	uint8_t  system_key[32];
	uint8_t  init_cbc[8];
	int64_t  bcas_card_id;
	int32_t  card_status;
} B_CAS_INIT_STATUS;

typedef struct {
	uint8_t  scramble_key[16];
} B_CAS_ECM_RESULT;

typedef struct {

	void *private_data;

	void (* release)(void *bcas);

	int (* init)(void *bcas);

	int (* get_init_status)(void *bcas, B_CAS_INIT_STATUS *stat);

	int (* proc_ecm)(void *bcas, B_CAS_ECM_RESULT *dst, uint8_t *src, int len);
	
} B_CAS_CARD;

#ifdef __cplusplus
extern "C" {
#endif

extern B_CAS_CARD *create_b_cas_card();

#ifdef __cplusplus
}
#endif

#endif /* B_CAS_CARD_H */