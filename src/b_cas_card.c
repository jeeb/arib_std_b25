#include "b_cas_card.h"
#include "b_cas_card_error_code.h"

#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <winscard.h>

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 inner structures
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
typedef struct {
	
	SCARDCONTEXT       mng;
	SCARDHANDLE        card;

	uint8_t           *pool;
	char              *reader;

	uint8_t           *sbuf;
	uint8_t           *rbuf;

	B_CAS_INIT_STATUS  stat;
	
} B_CAS_CARD_PRIVATE_DATA;

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 constant values
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static const uint8_t INITIAL_SETTING_CONDITIONS_CMD[] = {
	0x90, 0x30, 0x00, 0x00, 0x00,
};

static const uint8_t ECM_RECEIVE_CMD_HEADER[] = {
	0x90, 0x34, 0x00, 0x00,
};

#define B_CAS_BUFFER_MAX (4*1024)

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 function prottypes (interface method)
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static void release_b_cas_card(void *bcas);
static int init_b_cas_card(void *bcas);
static int get_init_status_b_cas_card(void *bcas, B_CAS_INIT_STATUS *stat);
static int proc_ecm_b_cas_card(void *bcas, B_CAS_ECM_RESULT *dst, uint8_t *src, int len);
static int32_t load_be_uint16(uint8_t *p);
static int64_t load_be_uint48(uint8_t *p);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 global function implementation
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
B_CAS_CARD *create_b_cas_card()
{
	int n;
	
	B_CAS_CARD *r;
	B_CAS_CARD_PRIVATE_DATA *prv;

	n = sizeof(B_CAS_CARD) + sizeof(B_CAS_CARD_PRIVATE_DATA);
	prv = (B_CAS_CARD_PRIVATE_DATA *)calloc(1, n);
	if(prv == NULL){
		return NULL;
	}

	r = (B_CAS_CARD *)(prv+1);

	r->private_data = prv;

	r->release = release_b_cas_card;
	r->init = init_b_cas_card;
	r->get_init_status = get_init_status_b_cas_card;
	r->proc_ecm = proc_ecm_b_cas_card;

	return r;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 function prottypes (private method)
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static B_CAS_CARD_PRIVATE_DATA *private_data(void *bcas);
static void teardown(B_CAS_CARD_PRIVATE_DATA *prv);
static int connect_card(B_CAS_CARD_PRIVATE_DATA *prv, const char *reader_name);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 interface method implementation
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static void release_b_cas_card(void *bcas)
{
	B_CAS_CARD_PRIVATE_DATA *prv;

	prv = private_data(bcas);
	if(prv == NULL){
		/* do nothing */
		return;
	}

	teardown(prv);
	free(prv);
}

static int init_b_cas_card(void *bcas)
{
	int m;
	LONG ret;
	DWORD len;
	
	B_CAS_CARD_PRIVATE_DATA *prv;

	prv = private_data(bcas);
	if(prv == NULL){
		return B_CAS_CARD_ERROR_INVALID_PARAMETER;
	}

	teardown(prv);

	ret = SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &(prv->mng));
	if(ret != SCARD_S_SUCCESS){
		return B_CAS_CARD_ERROR_NO_SMART_CARD_READER;
	}

	ret = SCardListReaders(prv->mng, NULL, NULL, &len);
	if(ret != SCARD_S_SUCCESS){
		return B_CAS_CARD_ERROR_NO_SMART_CARD_READER;
	}
	len += 256;
	
	m = len + (2*B_CAS_BUFFER_MAX);
	prv->pool = (uint8_t *)malloc(m);
	if(prv->pool == NULL){
		return B_CAS_CARD_ERROR_NO_ENOUGH_MEMORY;
	}

	prv->reader = (char *)(prv->pool);
	prv->sbuf = prv->pool + len;
	prv->rbuf = prv->sbuf + B_CAS_BUFFER_MAX;

	ret = SCardListReaders(prv->mng, NULL, prv->reader, &len);
	if(ret != SCARD_S_SUCCESS){
		return B_CAS_CARD_ERROR_NO_SMART_CARD_READER;
	}

	while( prv->reader[0] != 0 ){
		if(connect_card(prv, prv->reader)){
			break;
		}
		prv->reader += (strlen(prv->reader) + 1);
	}

	if(prv->card == 0){
		return B_CAS_CARD_ERROR_ALL_READERS_CONNECTION_FAILED;
	}

	return 0;
}

static int get_init_status_b_cas_card(void *bcas, B_CAS_INIT_STATUS *stat)
{
	B_CAS_CARD_PRIVATE_DATA *prv;

	prv = private_data(bcas);
	if( (prv == NULL) || (stat == NULL) ){
		return B_CAS_CARD_ERROR_INVALID_PARAMETER;
	}

	if(prv->card == 0){
		return B_CAS_CARD_ERROR_NOT_INITIALIZED;
	}

	memcpy(stat, &(prv->stat), sizeof(B_CAS_INIT_STATUS));

	return 0;
}

static int proc_ecm_b_cas_card(void *bcas, B_CAS_ECM_RESULT *dst, uint8_t *src, int len)
{
	int m;
	int retry_count;
	
	LONG ret;
	DWORD rlen;
	
	B_CAS_CARD_PRIVATE_DATA *prv;

	SCARD_IO_REQUEST sir;

	prv = private_data(bcas);
	if( (prv == NULL) ||
	    (dst == NULL) ||
	    (src == NULL) ||
	    (len < 1) ){
		return B_CAS_CARD_ERROR_INVALID_PARAMETER;
	}

	if(prv->card == 0){
		return B_CAS_CARD_ERROR_NOT_INITIALIZED;
	}

	memcpy(prv->sbuf, ECM_RECEIVE_CMD_HEADER, sizeof(ECM_RECEIVE_CMD_HEADER));
	m = sizeof(ECM_RECEIVE_CMD_HEADER);
	prv->sbuf[m] = (uint8_t)(len & 0xff);
	m += 1;
	memcpy(prv->sbuf+m, src, len);
	m += len;
	prv->sbuf[m] = 0;
	m += 1;

	memcpy(&sir, SCARD_PCI_T1, sizeof(sir));
	rlen = B_CAS_BUFFER_MAX;

	retry_count = 0;
	ret = SCardTransmit(prv->card, SCARD_PCI_T1, prv->sbuf, m, &sir, prv->rbuf, &rlen);
	while( (ret != SCARD_S_SUCCESS) && (retry_count < 10) ){
		retry_count += 1;
		if(!connect_card(prv, prv->reader)){
			continue;
		}
		memcpy(&sir, SCARD_PCI_T1, sizeof(sir));
		rlen = B_CAS_BUFFER_MAX;
		ret = SCardTransmit(prv->card, SCARD_PCI_T1, prv->sbuf, m, &sir, prv->rbuf, &rlen);
	}

	if( (ret != SCARD_S_SUCCESS) || (rlen < 25) ){
		return B_CAS_CARD_ERROR_TRANSMIT_FAILED;
	}

	memcpy(dst->scramble_key, prv->rbuf+6, 16);

	return 0;
}


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 private method implementation
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static B_CAS_CARD_PRIVATE_DATA *private_data(void *bcas)
{
	B_CAS_CARD_PRIVATE_DATA *r;
	B_CAS_CARD *p;

	p = (B_CAS_CARD *)bcas;
	if(p == NULL){
		return NULL;
	}

	r = (B_CAS_CARD_PRIVATE_DATA *)(p->private_data);
	if( ((void *)(r+1)) != ((void *)p) ){
		return NULL;
	}

	return r;
}

static void teardown(B_CAS_CARD_PRIVATE_DATA *prv)
{
	if(prv->card != 0){
		SCardDisconnect(prv->card, SCARD_LEAVE_CARD);
		prv->card = 0;
	}

	if(prv->mng != 0){
		SCardReleaseContext(prv->mng);
		prv->mng = 0;
	}

	if(prv->pool != NULL){
		free(prv->pool);
		prv->pool = NULL;
	}

	prv->reader = NULL;
	prv->sbuf = NULL;
	prv->rbuf = NULL;
}

static int connect_card(B_CAS_CARD_PRIVATE_DATA *prv, const char *reader_name)
{
	int m,n;
	
	LONG ret;
	DWORD rlen,protocol;

	uint8_t *p;
	
	SCARD_IO_REQUEST sir;

	if(prv->card != 0){
		SCardDisconnect(prv->card, SCARD_LEAVE_CARD);
		prv->card = 0;
	}

	ret = SCardConnect(prv->mng, reader_name, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1, &(prv->card), &protocol);
	if(ret != SCARD_S_SUCCESS){
		return 0;
	}

	m = sizeof(INITIAL_SETTING_CONDITIONS_CMD);
	memcpy(prv->sbuf, INITIAL_SETTING_CONDITIONS_CMD, m);
	memcpy(&sir, SCARD_PCI_T1, sizeof(sir));
	rlen = B_CAS_BUFFER_MAX;
	ret = SCardTransmit(prv->card, SCARD_PCI_T1, prv->sbuf, m, &sir, prv->rbuf, &rlen);
	if(ret != SCARD_S_SUCCESS){
		return 0;
	}

	if(rlen < 57){
		return 0;
	}
	
	p = prv->rbuf;

	n = load_be_uint16(p+4);
	if(n != 0x2100){ // return code missmatch
		return 0;
	}

	memcpy(prv->stat.system_key, p+16, 32);
	memcpy(prv->stat.init_cbc, p+48, 8);
	prv->stat.bcas_card_id = load_be_uint48(p+8);
	prv->stat.card_status = load_be_uint16(p+2);

	return 1;
}

static int32_t load_be_uint16(uint8_t *p)
{
	return ((p[0]<<8)|p[1]);
}

static int64_t load_be_uint48(uint8_t *p)
{
	int i;
	int64_t r;

	r = p[0];
	for(i=1;i<6;i++){
		r <<= 8;
		r |= p[i];
	}

	return r;
}

