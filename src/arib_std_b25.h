#ifndef ARIB_STD_B25_H
#define ARIB_STD_B25_H

#include "portable.h"
#include "b_cas_card.h"

typedef struct {
	uint8_t *data;
	int32_t  size;
} ARIB_STD_B25_BUFFER;

typedef struct {

	void *private_data;

	void (* release)(void *std_b25);

	int (* set_multi2_round)(void *std_b25, int32_t round);
	int (* set_b_cas_card)(void *std_b25, B_CAS_CARD *bcas);
	
	int (* reset)(void *std_b25);
	int (* flush)(void *std_b25);

	int (* put)(void *std_b25, ARIB_STD_B25_BUFFER *buf);
	int (* get)(void *std_b25, ARIB_STD_B25_BUFFER *buf);

} ARIB_STD_B25;

#ifdef __cplusplus
extern "C" {
#endif

extern ARIB_STD_B25 *create_arib_std_b25();

#endif /* ARIB_STD_B25_H */

