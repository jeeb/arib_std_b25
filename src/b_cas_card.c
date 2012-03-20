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
	
	B_CAS_ID           id;
	int32_t            id_max;
	
} B_CAS_CARD_PRIVATE_DATA;

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 constant values
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static const uint8_t INITIAL_SETTING_CONDITIONS_CMD[] = {
	0x90, 0x30, 0x00, 0x00, 0x00,
};

static const uint8_t CARD_ID_INFORMATION_ACQUIRE_CMD[] = {
	0x90, 0x32, 0x00, 0x00, 0x00,
};

static const uint8_t ECM_RECEIVE_CMD_HEADER[] = {
	0x90, 0x34, 0x00, 0x00,
};

static const uint8_t EMM_RECEIVE_CMD_HEADER[] = {
	0x90, 0x36, 0x00, 0x00,
};

#define B_CAS_BUFFER_MAX (4*1024)

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 function prottypes (interface method)
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static void release_b_cas_card(void *bcas);
static int init_b_cas_card(void *bcas);
static int get_init_status_b_cas_card(void *bcas, B_CAS_INIT_STATUS *stat);
static int get_id_b_cas_card(void *bcas, B_CAS_ID *dst);
static int proc_ecm_b_cas_card(void *bcas, B_CAS_ECM_RESULT *dst, uint8_t *src, int len);
static int proc_emm_b_cas_card(void *bcas, uint8_t *src, int len);

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
	r->get_id = get_id_b_cas_card;
	r->proc_ecm = proc_ecm_b_cas_card;
	r->proc_emm = proc_emm_b_cas_card;

	return r;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 function prottypes (private method)
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static B_CAS_CARD_PRIVATE_DATA *private_data(void *bcas);
static void teardown(B_CAS_CARD_PRIVATE_DATA *prv);
static int change_id_max(B_CAS_CARD_PRIVATE_DATA *prv, int max);
static int connect_card(B_CAS_CARD_PRIVATE_DATA *prv, const char *reader_name);
static int setup_ecm_receive_command(uint8_t *dst, uint8_t *src, int len);
static int setup_emm_receive_command(uint8_t *dst, uint8_t *src, int len);
static int32_t load_be_uint16(uint8_t *p);
static int64_t load_be_uint48(uint8_t *p);

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
	
	m = len + (2*B_CAS_BUFFER_MAX) + (sizeof(int64_t)*16);
	prv->pool = (uint8_t *)malloc(m);
	if(prv->pool == NULL){
		return B_CAS_CARD_ERROR_NO_ENOUGH_MEMORY;
	}

	prv->reader = (char *)(prv->pool);
	prv->sbuf = prv->pool + len;
	prv->rbuf = prv->sbuf + B_CAS_BUFFER_MAX;
	prv->id.data = (int64_t *)(prv->rbuf + B_CAS_BUFFER_MAX);
	prv->id_max = 16;

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

static int get_id_b_cas_card(void *bcas, B_CAS_ID *dst)
{
	LONG ret;
	
	DWORD slen;
	DWORD rlen;

	int i,num;

	uint8_t *p;
	uint8_t *tail;
	
	B_CAS_CARD_PRIVATE_DATA *prv;
	SCARD_IO_REQUEST sir;

	prv = private_data(bcas);
	if( (prv == NULL) || (dst == NULL) ){
		return B_CAS_CARD_ERROR_INVALID_PARAMETER;
	}

	if(prv->card == 0){
		return B_CAS_CARD_ERROR_NOT_INITIALIZED;
	}

	slen = sizeof(CARD_ID_INFORMATION_ACQUIRE_CMD);
	memcpy(prv->sbuf, CARD_ID_INFORMATION_ACQUIRE_CMD, slen);
	memcpy(&sir, SCARD_PCI_T1, sizeof(sir));
	rlen = B_CAS_BUFFER_MAX;

	ret = SCardTransmit(prv->card, SCARD_PCI_T1, prv->sbuf, slen, &sir, prv->rbuf, &rlen);
	if( (ret != SCARD_S_SUCCESS) || (rlen < 19) ){
		return B_CAS_CARD_ERROR_TRANSMIT_FAILED;
	}

	p = prv->rbuf + 6;
	tail = prv->rbuf + rlen;
	if( p+1 > tail ){
		return B_CAS_CARD_ERROR_TRANSMIT_FAILED;
	}

	num = p[0];
	if(num > prv->id_max){
		if(change_id_max(prv, num+4) < 0){
			return B_CAS_CARD_ERROR_NO_ENOUGH_MEMORY;
		}
	}
	
	p += 1;
	for(i=0;i<num;i++){
		if( p+10 > tail ){
			return B_CAS_CARD_ERROR_TRANSMIT_FAILED;
		}
		
		{
			int maker_id;
			int version;
			int check_code;
			
			maker_id = p[0];
			version = p[1];
			prv->id.data[i] = load_be_uint48(p+2);
			check_code = load_be_uint16(p+8);
		}
		
		p += 10;
	}

	prv->id.count = num;

	memcpy(dst, &(prv->id), sizeof(B_CAS_ID));

	return 0;
}

static int proc_ecm_b_cas_card(void *bcas, B_CAS_ECM_RESULT *dst, uint8_t *src, int len)
{
	int retry_count;
	
	LONG ret;
	DWORD slen;
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

	slen = setup_ecm_receive_command(prv->sbuf, src, len);
	memcpy(&sir, SCARD_PCI_T1, sizeof(sir));
	rlen = B_CAS_BUFFER_MAX;

	retry_count = 0;
	ret = SCardTransmit(prv->card, SCARD_PCI_T1, prv->sbuf, slen, &sir, prv->rbuf, &rlen);
	while( ((ret != SCARD_S_SUCCESS) || (rlen < 25)) && (retry_count < 10) ){
		retry_count += 1;
		if(!connect_card(prv, prv->reader)){
			continue;
		}
		slen = setup_ecm_receive_command(prv->sbuf, src, len);
		memcpy(&sir, SCARD_PCI_T1, sizeof(sir));
		rlen = B_CAS_BUFFER_MAX;

		ret = SCardTransmit(prv->card, SCARD_PCI_T1, prv->sbuf, slen, &sir, prv->rbuf, &rlen);
	}

	if( (ret != SCARD_S_SUCCESS) || (rlen < 25) ){
		return B_CAS_CARD_ERROR_TRANSMIT_FAILED;
	}

	memcpy(dst->scramble_key, prv->rbuf+6, 16);
	dst->return_code = load_be_uint16(prv->rbuf+4);

	return 0;
}

static int proc_emm_b_cas_card(void *bcas, uint8_t *src, int len)
{
	int retry_count;
	
	LONG ret;
	DWORD slen;
	DWORD rlen;
	
	B_CAS_CARD_PRIVATE_DATA *prv;

	SCARD_IO_REQUEST sir;

	prv = private_data(bcas);
	if( (prv == NULL) ||
	    (src == NULL) ||
	    (len < 1) ){
		return B_CAS_CARD_ERROR_INVALID_PARAMETER;
	}

	if(prv->card == 0){
		return B_CAS_CARD_ERROR_NOT_INITIALIZED;
	}

	slen = setup_emm_receive_command(prv->sbuf, src, len);
	memcpy(&sir, SCARD_PCI_T1, sizeof(sir));
	rlen = B_CAS_BUFFER_MAX;

	retry_count = 0;
	ret = SCardTransmit(prv->card, SCARD_PCI_T1, prv->sbuf, slen, &sir, prv->rbuf, &rlen);
	while( ((ret != SCARD_S_SUCCESS) || (rlen < 6)) && (retry_count < 2) ){
		retry_count += 1;
		if(!connect_card(prv, prv->reader)){
			continue;
		}
		slen = setup_emm_receive_command(prv->sbuf, src, len);
		memcpy(&sir, SCARD_PCI_T1, sizeof(sir));
		rlen = B_CAS_BUFFER_MAX;

		ret = SCardTransmit(prv->card, SCARD_PCI_T1, prv->sbuf, slen, &sir, prv->rbuf, &rlen);
	}

	if( (ret != SCARD_S_SUCCESS) || (rlen < 6) ){
		return B_CAS_CARD_ERROR_TRANSMIT_FAILED;
	}

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
	prv->id.data = NULL;
	prv->id_max = 0;
}

static int change_id_max(B_CAS_CARD_PRIVATE_DATA *prv, int max)
{
	int m,n;
	uint8_t *p;

	n = prv->sbuf - prv->pool;
	m = n + (2*B_CAS_BUFFER_MAX) + (sizeof(int64_t)*max);
	p = (uint8_t *)malloc(m);
	if(p == NULL){
		return B_CAS_CARD_ERROR_NO_ENOUGH_MEMORY;
	}

	memcpy(p, prv->reader, n);
	free(prv->pool);
	prv->pool = p;
	prv->reader = (char *)p;
	prv->sbuf = prv->pool + n;
	prv->rbuf = prv->sbuf + B_CAS_BUFFER_MAX;
	prv->id.data = (int64_t *)(prv->rbuf + B_CAS_BUFFER_MAX);
	prv->id_max = max;

	return 0;
}

static int connect_card(B_CAS_CARD_PRIVATE_DATA *prv, const char *reader_name)
{
	int m,n;
	
	LONG ret;
	DWORD rlen,protocol;

	uint8_t *p;
	
	SCARD_IO_REQUEST sir;

	if(prv->card != 0){
		SCardDisconnect(prv->card, SCARD_RESET_CARD);
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

static int setup_ecm_receive_command(uint8_t *dst, uint8_t *src, int len)
{
	int r;
	
	r  = sizeof(ECM_RECEIVE_CMD_HEADER);
	memcpy(dst+0, ECM_RECEIVE_CMD_HEADER, r);
	dst[r] = (uint8_t)(len & 0xff);
	r += 1;
	memcpy(dst+r, src, len);
	r += len;
	dst[r] = 0;
	r += 1;

	return r;
}

static int setup_emm_receive_command(uint8_t *dst, uint8_t *src, int len)
{
	int r;

	r  = sizeof(EMM_RECEIVE_CMD_HEADER);
	memcpy(dst+0, EMM_RECEIVE_CMD_HEADER, r);
	dst[r] = (uint8_t)(len & 0xff);
	r += 1;
	memcpy(dst+r, src, len);
	r += len;
	dst[r] = 0;
	r += 1;

	return r;
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

