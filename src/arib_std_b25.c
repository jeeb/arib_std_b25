#include <stdlib.h>
#include <string.h>

#include "arib_std_b25.h"
#include "arib_std_b25_error_code.h"
#include "multi2.h"

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 inner structures
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
typedef struct {
	int32_t           sync;                           /*  0- 7 :  8 bits */
	int32_t           transport_error_indicator;      /*  8- 8 :  1 bit  */
	int32_t           payload_unit_start_indicator;   /*  9- 9 :  1 bit  */
	int32_t           transport_priority;             /* 10-10 :  1 bits */
	int32_t           pid;                            /* 11-23 : 13 bits */
	int32_t           transport_scrambling_control;   /* 24-25 :  2 bits */
	int32_t           adaptation_field_control;       /* 26-27 :  2 bits */
	int32_t           continuity_counter;             /* 28-31 :  4 bits */
} TS_HEADER;

typedef struct {
	int32_t           pid;
	int32_t           type;
} TS_STREAM_INFO;

typedef struct {
	
	uint8_t          *pool;
	uint8_t          *head;
	uint8_t          *tail;
	int32_t           max;
	
} TS_WORK_BUFFER;

typedef struct {
	
	int32_t           table_id;                       /*  0- 7 :  8 bits */
	int32_t           section_syntax_indicator;       /*  8- 8 :  1 bit  */
	int32_t           private_indicator;              /*  9- 9 :  1 bit  */
	int32_t           section_length;                 /* 12-23 : 12 bits */
	int32_t           transport_stream_id;            /* 24-47 : 16 bits */
	int32_t           version_number;                 /* 50-54 :  5 bits */
	int32_t           current_next_indicator;         /* 55-55 :  1 bit  */
	int32_t           section_number;                 /* 56-63 :  8 bits */
	int32_t           last_section_number;            /* 64-71 :  8 bits */
	
} TS_SECTION_HEADER;

typedef struct {

	TS_WORK_BUFFER    buf; /* for raw data      */
	TS_SECTION_HEADER hdr; /* for parsed header */

} TS_SECTION;

typedef struct {

	uint32_t          pmt_pid;
	TS_SECTION        pmt_curr;
	TS_SECTION        pmt_next;

	uint32_t          pcr_pid;
	
	uint32_t          ecm_pid;
	TS_SECTION        ecm_curr;
	TS_SECTION        ecm_next;

	TS_STREAM_INFO   *streams;
	int32_t           s_count;

	MULTI2           *m2;

} TS_PROGRAM;

typedef struct {

	int32_t           multi2_round;
	
	int32_t           unit_size;

	int32_t           sbuf_offset;

	TS_SECTION        pat_curr;
	TS_SECTION        pat_next;
	
	int32_t           p_count;
	TS_PROGRAM       *program;

	TS_PROGRAM       *map[0x2000];

	B_CAS_CARD       *bcas;

	TS_WORK_BUFFER    sbuf;
	TS_WORK_BUFFER    dbuf;
	
} ARIB_STD_B25_PRIVATE_DATA;

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 constant values
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
enum TS_STREAM_TYPE {
	TS_STREAM_TYPE_11172_2_VIDEO                = 0x01,
	TS_STREAM_TYPE_13818_2_VIDEO                = 0x02,
	TS_STREAM_TYPE_11172_3_AUDIO                = 0x03,
	TS_STREAM_TYPE_13818_3_AUDIO                = 0x04,
	TS_STREAM_TYPE_13818_1_PRIVATE_SECTIONS     = 0x05,
	TS_STREAM_TYPE_13818_1_PES_PRIVATE_DATA     = 0x06,
	TS_STREAM_TYPE_13522_MHEG                   = 0x07,
	TS_STREAM_TYPE_13818_1_DSM_CC               = 0x08,
	TS_STREAM_TYPE_H_222_1                      = 0x09,
	TS_STREAM_TYPE_13818_6_TYPE_A               = 0x0a,
	TS_STREAM_TYPE_13818_6_TYPE_B               = 0x0b,
	TS_STREAM_TYPE_13818_6_TYPE_C               = 0x0c,
	TS_STREAM_TYPE_13818_6_TYPE_D               = 0x0d,
	TS_STREAM_TYPE_13818_1_AUX                  = 0x0e,
	TS_STREAM_TYPE_13818_7_AUDIO_ADTS           = 0x0f,
	TS_STREAM_TYPE_14496_2_VISUAL               = 0x10,
	TS_STREAM_TYPE_14496_3_AUDIO_LATM           = 0x11,
	TS_STREAM_TYPE_14496_1_PES_SL_PACKET        = 0x12,
	TS_STREAM_TYPE_14496_1_SECTIONS_SL_PACKET   = 0x13,
	TS_STREAM_TYPE_13818_6_SYNC_DWLOAD_PROTCOL  = 0x14,
};

enum TS_SECTION_ID {
	TS_SECTION_ID_PROGRAM_ASSOCATION            = 0x00,
	TS_SECTION_ID_CONDITIONAL_ACCESS            = 0x01,
	TS_SECTION_ID_PROGRAM_MAP                   = 0x02,
	TS_SECTION_ID_DESCRIPTION                   = 0x03,
	TS_SECTION_ID_14496_SCENE_DESCRIPTION       = 0x04,
	TS_SECTION_ID_14496_OBJECT_DESCRIPTION      = 0x05,

	/* ARIB STD-B10 stuff */
	TS_SECTION_ID_DSM_CC_HEAD                   = 0x3a,
	TS_SECTION_ID_DSM_CC_TAIL                   = 0x3f,
	TS_SECTION_ID_NIT_ACTUAL                    = 0x40,
	TS_SECTION_ID_NIT_OTHER                     = 0x41,
	TS_SECTION_ID_SDT_ACTUAL                    = 0x42,
	TS_SECTION_ID_SDT_OTHER                     = 0x46,
	TS_SECTION_ID_BAT                           = 0x4a,
	TS_SECTION_ID_EIT_ACTUAL_CURRENT            = 0x4e,
	TS_SECTION_ID_EIT_OTHER_CURRENT             = 0x4f,
	TS_SECTION_ID_EIT_ACTUAL_SCHEDULE_HEAD      = 0x50,
	TS_SECTION_ID_EIT_ACTUAL_SCHEDULE_TAIL      = 0x5f,
	TS_SECTION_ID_EIT_OTHER_SCHEDULE_HEAD       = 0x60,
	TS_SECTION_ID_EIT_OTHER_SCHEDULE_TAIL       = 0x6f,
	TS_SECTION_ID_TDT                           = 0x70,
	TS_SECTION_ID_RST                           = 0x71,
	TS_SECTION_ID_ST                            = 0x72,
	TS_SECTION_ID_TOT                           = 0x73,
	TS_SECTION_ID_AIT                           = 0x74,
	TS_SECTION_ID_DIT                           = 0x7e,
	TS_SECTION_ID_SIT                           = 0x7f,
	TS_SECTION_ID_ECM_S                         = 0x82,
	TS_SECTION_ID_ECM                           = 0x83,
	TS_SECTION_ID_EMM_S                         = 0x84,
	TS_SECTION_ID_EMM                           = 0x85,
	TS_SECTION_ID_DCT                           = 0xc0,
	TS_SECTION_ID_DLT                           = 0xc1,
	TS_SECTION_ID_PCAT                          = 0xc2,
	TS_SECTION_ID_SDTT                          = 0xc3,
	TS_SECTION_ID_BIT                           = 0xc4,
	TS_SECTION_ID_NBIT_BODY                     = 0xc5,
	TS_SECTION_ID_NBIT_REFERENCE                = 0xc6,
	TS_SECTION_ID_LDT                           = 0xc7,
	TS_SECTION_ID_CDT                           = 0xc8,
	TS_SECTION_ID_LIT                           = 0xd0,
	TS_SECTION_ID_ERT                           = 0xd1,
	TS_SECTION_ID_ITT                           = 0xd2,
};

enum TS_DESCRIPTOR_TAG {
	TS_DESCRIPTOR_TAG_VIDEO_STREAM              = 0x02,
	TS_DESCRIPTOR_TAG_AUDIO_STREAM              = 0x03,
	TS_DESCRIPTOR_TAG_HIERARCHY                 = 0x04,
	TS_DESCRIPTOR_TAG_REGISTRATION              = 0x05,
	TS_DESCRIPTOR_TAG_DATA_STREAM_ALIGNMENT     = 0x06,
	TS_DESCRIPTOR_TAG_TARGET_BACKGROUND_GRID    = 0x07,
	TS_DESCRIPTOR_TAG_VIDEO_WINDOW              = 0x08,
	TS_DESCRIPTOR_TAG_CA                        = 0x09,
	TS_DESCRIPTOR_TAG_ISO_639_LANGUAGE          = 0x0a,
	TS_DESCRIPTOR_TAG_SYSTEM_CLOCK              = 0x0b,
	TS_DESCRIPTOR_TAG_MULTIPLEX_BUF_UTILIZ      = 0x0c,
	TS_DESCRIPTOR_TAG_COPYRIGHT                 = 0x0d,
	TS_DESCRIPTOR_TAG_MAXIMUM_BITRATE           = 0x0e,
	TS_DESCRIPTOR_TAG_PRIVATE_DATA_INDICATOR    = 0x0f,
	TS_DESCRIPTOR_TAG_SMOOTHING_BUFFER          = 0x10,
	TS_DESCRIPTOR_TAG_STD                       = 0x11,
	TS_DESCRIPTOR_TAG_IBP                       = 0x12,
	TS_DESCRIPTOR_TAG_MPEG4_VIDEO               = 0x1b,
	TS_DESCRIPTOR_TAG_MPEG4_AUDIO               = 0x1c,
	TS_DESCRIPTOR_TAG_IOD                       = 0x1d,
	TS_DESCRIPTOR_TAG_SL                        = 0x1e,
	TS_DESCRIPTOR_TAG_FMC                       = 0x1f,
	TS_DESCRIPTOR_TAG_EXTERNAL_ES_ID            = 0x20,
	TS_DESCRIPTOR_TAG_MUXCODE                   = 0x21,
	TS_DESCRIPTOR_TAG_FMX_BUFFER_SIZE           = 0x22,
	TS_DESCRIPTOR_TAG_MULTIPLEX_BUFFER          = 0x23,
	TS_DESCRIPTOR_TAG_AVC_VIDEO                 = 0x28,
	TS_DESCRIPTOR_TAG_AVC_TIMING_HRD            = 0x2a,

	/* ARIB STD-B10 stuff */
	TS_DESCRIPTOR_TAG_NETWORK_NAME              = 0x40,
	TS_DESCRIPTOR_TAG_SERVICE_LIST              = 0x41,
	TS_DESCRIPTOR_TAG_STUFFING                  = 0x42,
	TS_DESCRIPTOR_TAG_SATELLITE_DELIVERY_SYS    = 0x43,
	TS_DESCRIPTOR_TAG_CABLE_DISTRIBUTION        = 0x44,
	TS_DESCRIPTOR_TAG_BOUNQUET_NAME             = 0x47,
	TS_DESCRIPTOR_TAG_SERVICE                   = 0x48,
	TS_DESCRIPTOR_TAG_COUNTRY_AVAILABILITY      = 0x49,
	TS_DESCRIPTOR_TAG_LINKAGE                   = 0x4a,
	TS_DESCRIPTOR_TAG_NVOD_REFERENCE            = 0x4b,
	TS_DESCRIPTOR_TAG_TIME_SHIFTED_SERVICE      = 0x4c,
	TS_DESCRIPTOR_TAG_SHORT_EVENT               = 0x4d,
	TS_DESCRIPTOR_TAG_EXTENDED_EVENT            = 0x4e,
	TS_DESCRIPTOR_TAG_TIME_SHIFTED_EVENT        = 0x4f,
	TS_DESCRIPTOR_TAG_COMPONENT                 = 0x50,
	TS_DESCRIPTOR_TAG_MOSAIC                    = 0x51,
	TS_DESCRIPTOR_TAG_STREAM_IDENTIFIER         = 0x52,
	TS_DESCRIPTOR_TAG_CA_IDENTIFIER             = 0x53,
	TS_DESCRIPTOR_TAG_CONTENT                   = 0x54,
	TS_DESCRIPTOR_TAG_PARENTAL_RATING           = 0x55,
	TS_DESCRIPTOR_TAG_LOCAL_TIME_OFFSET         = 0x58,
	TS_DESCRIPTOR_TAG_PARTIAL_TRANSPORT_STREAM  = 0x63,
	TS_DESCRIPTOR_TAG_HIERARCHICAL_TRANSMISSION = 0xc0,
	TS_DESCRIPTOR_TAG_DIGITAL_COPY_CONTROL      = 0xc1,
	TS_DESCRIPTOR_TAG_NETWORK_IDENTIFICATION    = 0xc2,
	TS_DESCRIPTOR_TAG_PARTIAL_TRANSPORT_TIME    = 0xc3,
	TS_DESCRIPTOR_TAG_AUDIO_COMPONENT           = 0xc4,
	TS_DESCRIPTOR_TAG_HYPERLINK                 = 0xc5,
	TS_DESCRIPTOR_TAG_TARGET_REGION             = 0xc6,
	TS_DESCRIPTOR_TAG_DATA_COTENT               = 0xc7,
	TS_DESCRIPTOR_TAG_VIDEO_DECODE_CONTROL      = 0xc8,
	TS_DESCRIPTOR_TAG_DOWNLOAD_CONTENT          = 0xc9,
	TS_DESCRIPTOR_TAG_CA_EMM_TS                 = 0xca,
	TS_DESCRIPTOR_TAG_CA_CONTRACT_INFORMATION   = 0xcb,
	TS_DESCRIPTOR_TAG_CA_SERVICE                = 0xcc,
	TS_DESCRIPTOR_TAG_TS_INFORMATION            = 0xcd,
	TS_DESCRIPTOR_TAG_EXTENDED_BROADCASTER      = 0xce,
	TS_DESCRIPTOR_TAG_LOGO_TRANSMISSION         = 0xcf,
	TS_DESCRIPTOR_TAG_BASIC_LOCAL_EVENT         = 0xd0,
	TS_DESCRIPTOR_TAG_REFERENCE                 = 0xd1,
	TS_DESCRIPTOR_TAG_NODE_RELATION             = 0xd2,
	TS_DESCRIPTOR_TAG_SHORT_NODE_INFORMATION    = 0xd3,
	TS_DESCRIPTOR_TAG_STC_REFERENCE             = 0xd4,
	TS_DESCRIPTOR_TAG_SERIES                    = 0xd5,
	TS_DESCRIPTOR_TAG_EVENT_GROUP               = 0xd6,
	TS_DESCRIPTOR_TAG_SI_PARAMETER              = 0xd7,
	TS_DESCRIPTOR_TAG_BROADCASTER_NAME          = 0xd8,
	TS_DESCRIPTOR_TAG_COMPONENT_GROUP           = 0xd9,
	TS_DESCRIPTOR_TAG_SI_PRIME_TS               = 0xda,
	TS_DESCRIPTOR_TAG_BOARD_INFORMATION         = 0xdb,
	TS_DESCRIPTOR_TAG_LDT_LINKAGE               = 0xdc,
	TS_DESCRIPTOR_TAG_CONNECTED_TRANSMISSION    = 0xdd,
	TS_DESCRIPTOR_TAG_CONTENT_AVAILABILITY      = 0xde,
	TS_DESCRIPTOR_TAG_VALUE_EXTENSION           = 0xdf,
	TS_DESCRIPTOR_TAG_SERVICE_GROUP             = 0xe0,
	TS_DESCRIPTOR_TAG_CARUSEL_COMPOSITE         = 0xf7,
	TS_DESCRIPTOR_TAG_CONDITIONAL_PLAYBACK      = 0xf8,
	TS_DESCRIPTOR_TAG_CABLE_TS_DIVISSION        = 0xf9,
	TS_DESCRIPTOR_TAG_TERRESTRIAL_DELIVERY_SYS  = 0xfa,
	TS_DESCRIPTOR_TAG_PARTIAL_RECEPTION         = 0xfb,
	TS_DESCRIPTOR_TAG_EMERGENCY_INFOMATION      = 0xfc,
	TS_DESCRIPTOR_TAG_DATA_COMPONENT            = 0xfd,
	TS_DESCRIPTOR_TAG_SYSTEM_MANAGEMENT         = 0xfe,
};

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 function prottypes (interface method)
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static void release_arib_std_b25(void *std_b25);
static int set_multi2_round_arib_std_b25(void *std_b25, int32_t round);
static int set_b_cas_card_arib_std_b25(void *std_b25, B_CAS_CARD *bcas);
static int reset_arib_std_b25(void *std_b25);
static int flush_arib_std_b25(void *std_b25);
static int put_arib_std_b25(void *std_b25, ARIB_STD_B25_BUFFER *buf);
static int get_arib_std_b25(void *std_b25, ARIB_STD_B25_BUFFER *buf);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 global function implementation
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
ARIB_STD_B25 *create_arib_std_b25()
{
	int n;
	
	ARIB_STD_B25 *r;
	ARIB_STD_B25_PRIVATE_DATA *prv;

	n  = sizeof(ARIB_STD_B25_PRIVATE_DATA);
	n += sizeof(ARIB_STD_B25);
	
	prv = (ARIB_STD_B25_PRIVATE_DATA *)calloc(1, n);
	if(prv == NULL){
		return NULL;
	}

	prv->multi2_round = 4;

	r = (ARIB_STD_B25 *)(prv+1);
	r->private_data = prv;

	r->release = release_arib_std_b25;
	r->set_multi2_round = set_multi2_round_arib_std_b25;
	r->set_b_cas_card = set_b_cas_card_arib_std_b25;
	r->reset = reset_arib_std_b25;
	r->flush = flush_arib_std_b25;
	r->put = put_arib_std_b25;
	r->get = get_arib_std_b25;

	return r;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 function prottypes (private method)
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static ARIB_STD_B25_PRIVATE_DATA *private_data(void *std_b25);
static void teardown(ARIB_STD_B25_PRIVATE_DATA *prv);
static int select_unit_size(ARIB_STD_B25_PRIVATE_DATA *prv);
static int find_pat(ARIB_STD_B25_PRIVATE_DATA *prv);
static int proc_pat(ARIB_STD_B25_PRIVATE_DATA *prv);
static int check_pmt_complete(ARIB_STD_B25_PRIVATE_DATA *prv);
static int find_pmt(ARIB_STD_B25_PRIVATE_DATA *prv);
static int proc_pmt(TS_PROGRAM *pgrm);
static uint8_t *proc_pmt_descriptor(TS_PROGRAM *pgrm, uint8_t *head, uint8_t *tail);
static int32_t count_pmt_stream(uint8_t *head, uint8_t *tail);
static void setup_pid_map(ARIB_STD_B25_PRIVATE_DATA *prv);
static int check_ecm_complete(ARIB_STD_B25_PRIVATE_DATA *prv);
static int find_ecm(ARIB_STD_B25_PRIVATE_DATA *prv);
static int setup_multi2(ARIB_STD_B25_PRIVATE_DATA *prv);
static int proc_ecm(TS_PROGRAM *pgrm, B_CAS_CARD *bcas);
static int proc_arib_std_b25(ARIB_STD_B25_PRIVATE_DATA *prv);

static void release_program(TS_PROGRAM *pgrm);

static int reserve_work_buffer(TS_WORK_BUFFER *buf, int32_t size);
static int append_work_buffer(TS_WORK_BUFFER *buf, uint8_t *data, int32_t size);
static void reset_work_buffer(TS_WORK_BUFFER *buf);
static void release_work_buffer(TS_WORK_BUFFER *buf);

static int set_ts_section_data(TS_SECTION *sect, uint8_t *data, int32_t size);
static int check_ts_section(TS_SECTION *sect);
static int check_ts_section_crc(TS_SECTION *sect);
static void reset_ts_section(TS_SECTION *sect);
static void swap_ts_section(TS_SECTION *curr, TS_SECTION *next);
static int compare_ts_section(TS_SECTION *curr, TS_SECTION *next);
static void release_ts_section(TS_SECTION *sect);

static void extract_ts_header(TS_HEADER *dst, uint8_t *src);
static void extract_ts_section_header(TS_SECTION_HEADER *dst, uint8_t *head);

static int check_unit_invert(unsigned char *head, unsigned char *tail);

static uint8_t *resync(uint8_t *head, uint8_t *tail, int32_t unit);
static uint8_t *resync_force(uint8_t *head, uint8_t *tail, int32_t unit);

static uint32_t crc32(uint8_t *head, uint8_t *tail);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 interface method implementation
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static void release_arib_std_b25(void *std_b25)
{
	ARIB_STD_B25_PRIVATE_DATA *prv;

	prv = private_data(std_b25);
	if(prv == NULL){
		return;
	}

	teardown(prv);
	free(prv);
}

static int set_multi2_round_arib_std_b25(void *std_b25, int32_t round)
{
	ARIB_STD_B25_PRIVATE_DATA *prv;

	prv = private_data(std_b25);
	if(prv == NULL){
		return ARIB_STD_B25_ERROR_INVALID_PARAM;
	}

	prv->multi2_round = round;

	return 0;
}

static int set_b_cas_card_arib_std_b25(void *std_b25, B_CAS_CARD *bcas)
{
	ARIB_STD_B25_PRIVATE_DATA *prv;

	prv = private_data(std_b25);
	if(prv == NULL){
		return ARIB_STD_B25_ERROR_INVALID_PARAM;
	}

	prv->bcas = bcas;

	return 0;
}

static int reset_arib_std_b25(void *std_b25)
{
	ARIB_STD_B25_PRIVATE_DATA *prv;

	prv = private_data(std_b25);
	if(prv == NULL){
		return ARIB_STD_B25_ERROR_INVALID_PARAM;
	}

	teardown(prv);

	return 0;
}

static int flush_arib_std_b25(void *std_b25)
{
	int r;
	int m,n;

	int32_t crypt;
	int32_t unit;
	uint32_t pid;

	uint8_t *p;
	uint8_t *curr;
	uint8_t *tail;
	
	ARIB_STD_B25_PRIVATE_DATA *prv;

	TS_PROGRAM *pgrm;
	TS_HEADER hdr;

	prv = private_data(std_b25);
	if(prv == NULL){
		return ARIB_STD_B25_ERROR_INVALID_PARAM;
	}

	r = proc_arib_std_b25(prv);
	if(r < 0){
		return r;
	}

	if(prv->unit_size < 188){
		r = select_unit_size(prv);
		if(r < 0){
			return r;
		}
	}

	unit = prv->unit_size;
	curr = prv->sbuf.head;
	tail = prv->sbuf.tail;

	m = prv->dbuf.tail - prv->dbuf.head;
	n = tail - curr;
	if(!reserve_work_buffer(&(prv->dbuf), m+n)){
		return ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
	}

	r = 0;

	while( (curr+188) <= tail ){
		if(curr[0] != 0x47){
			p = resync_force(curr, tail, unit);
			if(p == NULL){
				goto LAST;
			}
			curr = p;
		}
		extract_ts_header(&hdr, curr);
		crypt = hdr.transport_scrambling_control;
		pid = hdr.pid;
		pgrm = prv->map[pid];
		if(pgrm == NULL){
			pgrm = prv->program + 0;
		}

		p = curr+4;
		
		if(hdr.adaptation_field_control & 0x01){
			if(hdr.adaptation_field_control & 0x02){
				p += (p[0]+1);
			}
			n = 188 - (p-curr);
		
			if( (crypt != 0) && (pgrm->m2 != NULL) ){
				m = pgrm->m2->decrypt(pgrm->m2, crypt, p, n);
				if(m < 0){
					r = ARIB_STD_B25_ERROR_DECRYPT_FAILURE;
					goto LAST;
				}
				curr[3] &= 0x3f;
			}
		}

		if(!append_work_buffer(&(prv->dbuf), curr, 188)){
			r = ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
			goto LAST;
		}

		if(pid == pgrm->ecm_pid){
			r = set_ts_section_data(&(pgrm->ecm_next), p, n);
			if(r < 0){
				goto LAST;
			}
			if(check_ts_section(&(pgrm->ecm_next))){
				if( (!check_ts_section_crc(&(pgrm->ecm_next))) ||
				    (pgrm->ecm_next.hdr.current_next_indicator == 0) ||
				    (compare_ts_section(&(pgrm->ecm_curr), &(pgrm->ecm_next)) == 0) ){
					reset_ts_section(&(pgrm->ecm_next));
					curr += unit;
					continue;
				}
				swap_ts_section(&(pgrm->ecm_curr), &(pgrm->ecm_next));
				reset_ts_section(&(pgrm->ecm_next));
				r = proc_ecm(pgrm, prv->bcas);
				if(r < 0){
					goto LAST;
				}
			}
		}else if(pid == pgrm->pmt_pid){
			r = set_ts_section_data(&(pgrm->pmt_next), p, n);
			if(r < 0){
				goto LAST;
			}
			if(check_ts_section(&(pgrm->pmt_next))){
				if( (!check_ts_section_crc(&(pgrm->pmt_next))) ||
				    (pgrm->pmt_next.hdr.current_next_indicator == 0) ||
				    (compare_ts_section(&(pgrm->pmt_curr), &(pgrm->pmt_next)) == 0) ){
					reset_ts_section(&(pgrm->pmt_next));
					curr += unit;
					continue;
				}
				swap_ts_section(&(pgrm->pmt_curr), &(pgrm->pmt_next));
				reset_ts_section(&(pgrm->pmt_next));
				r = proc_pmt(pgrm);
				if(r < 0){
					goto LAST;
				}
				setup_pid_map(prv);
				curr += unit;
				goto LAST;
			}
		}else if(pid == 0x0000){
			r = set_ts_section_data(&(prv->pat_next), p, n);
			if(r < 0){
				goto LAST;
			}
			if(check_ts_section(&(prv->pat_next))){
				if( (!check_ts_section_crc(&(prv->pat_next))) ||
				    (prv->pat_next.hdr.current_next_indicator == 0) ||
				    (compare_ts_section(&(prv->pat_curr), &(prv->pat_next)) == 0)){
					reset_ts_section(&(prv->pat_next));
					curr += unit;
					continue;
				}
				swap_ts_section(&(prv->pat_curr), &(prv->pat_next));
				reset_ts_section(&(prv->pat_next));
				r = proc_pat(prv);
				if(r < 0){
					goto LAST;
				}
				curr += unit;
				goto LAST;
			}
		}

		curr += unit;
	}

LAST:
	
	m = curr - prv->sbuf.head;
	n = tail - curr;
	if( (n < 1024) || (m > (prv->sbuf.max/2) ) ){
		p = prv->sbuf.pool;
		memcpy(p, curr, n);
		prv->sbuf.head = p;
		prv->sbuf.tail = p+n;
	}else{
		prv->sbuf.head = curr;
	}

	return r;
}

static int put_arib_std_b25(void *std_b25, ARIB_STD_B25_BUFFER *buf)
{
	int32_t n;
	
	ARIB_STD_B25_PRIVATE_DATA *prv;

	prv = private_data(std_b25);
	if( (prv == NULL) || (buf == NULL) ){
		return ARIB_STD_B25_ERROR_INVALID_PARAM;
	}

	if(!append_work_buffer(&(prv->sbuf), buf->data, buf->size)){
		return ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
	}

	if(prv->unit_size < 188){
		n = select_unit_size(prv);
		if(n < 0){
			return n;
		}
		if(prv->unit_size < 188){
			/* need more data */
			return 0;
		}
	}

	if(prv->p_count < 1){
		n = find_pat(prv);
		if(n < 0){
			return n;
		}
		if(prv->p_count < 1){
			if(prv->sbuf_offset < (16*1024*1024)){
				/* need more data */
				return 0;
			}else{
				/* exceed sbuf limit */
				return ARIB_STD_B25_ERROR_NO_PAT_IN_HEAD_16M;
			}
		}
		prv->sbuf_offset = 0;
	}

	if(!check_pmt_complete(prv)){
		n = find_pmt(prv);
		if(n < 0){
			return n;
		}
		if(!check_pmt_complete(prv)){
			if(prv->sbuf_offset < (32*1024*1024)){
				/* need more data */
				return 0;
			}else{
				/* exceed sbuf limit */
				return ARIB_STD_B25_ERROR_NO_PMT_IN_HEAD_32M;
			}
		}
		prv->sbuf_offset = 0;
		setup_pid_map(prv);
	}

	if(!check_ecm_complete(prv)){
		n = find_ecm(prv);
		if(n < 0){
			return n;
		}
		if(!check_ecm_complete(prv)){
			if(prv->sbuf_offset < (32*1024*1024)){
				/* need more data */
				return 0;
			}else{
				/* exceed sbuf limit */
				return ARIB_STD_B25_ERROR_NO_ECM_IN_HEAD_32M;
			}
		}
		prv->sbuf_offset = 0;
		n = setup_multi2(prv);
	}

	return proc_arib_std_b25(prv);
}

static int get_arib_std_b25(void *std_b25, ARIB_STD_B25_BUFFER *buf)
{
	ARIB_STD_B25_PRIVATE_DATA *prv;
	prv = private_data(std_b25);
	if( (prv == NULL) || (buf == NULL) ){
		return ARIB_STD_B25_ERROR_INVALID_PARAM;
	}

	buf->data = prv->dbuf.head;
	buf->size = prv->dbuf.tail - prv->dbuf.head;

	reset_work_buffer(&(prv->dbuf));

	return 0;
}
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 private method implementation
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static ARIB_STD_B25_PRIVATE_DATA *private_data(void *std_b25)
{
	ARIB_STD_B25 *p;
	ARIB_STD_B25_PRIVATE_DATA *r;

	p = (ARIB_STD_B25 *)std_b25;
	if(p == NULL){
		return NULL;
	}

	r = (ARIB_STD_B25_PRIVATE_DATA *)p->private_data;
	if( ((void *)(r+1)) != ((void *)p) ){
		return NULL;
	}

	return r; 
}

static void teardown(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int i;
	
	prv->unit_size = 0;
	prv->sbuf_offset = 0;

	release_ts_section(&(prv->pat_curr));
	release_ts_section(&(prv->pat_next));

	if(prv->program != NULL){
		for(i=0;i<prv->p_count;i++){
			release_program(prv->program+i);
		}
		free(prv->program);
		prv->program = NULL;
	}
	prv->p_count = 0;

	memset(prv->map, 0, sizeof(prv->map));

	release_work_buffer(&(prv->sbuf));
	release_work_buffer(&(prv->dbuf));
}

static int select_unit_size(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int i;
	int m,n,w;
	int count[320];

	unsigned char *head;
	unsigned char *pre;
	unsigned char *buf;
	unsigned char *tail;

	head = prv->sbuf.head;
	tail = prv->sbuf.tail;
	pre = NULL;
	buf = head;
	memset(count, 0, sizeof(count));

	// 1st step, find head 0x47
	while(buf < tail){
		if(buf[0] == 0x47){
			pre = buf;
			break;
		}
		buf += 1;
	}

	if(pre == NULL){
		return ARIB_STD_B25_ERROR_NON_TS_INPUT_STREAM;
	}

	// 2nd step, count up 0x47 interval
	buf = pre + 1;
	while( buf < tail ){
		if(buf[0] == 0x47){
			m = buf - pre;
			if(m < 188){
				n = check_unit_invert(head, buf);
				if( (n >= 188) && (n < 320) ){
					count[n] += 1;
					pre = buf;
				}
			}else if(m < 320){
				count[m] += 1;
				pre = buf;
			}else{
				pre = buf;
			}
		}
		buf += 1;
	}

	// 3rd step, select maximum appeared interval
	m = 0;
	n = 0;
	for(i=188;i<320;i++){
		if(m < count[i]){
			m = count[i];
			n = i;
		}
	}

	// 4th step, verify unit_size
	w = m*n;
	if( (m < 8) || (w < ((tail-head) - (w/8))) ){
		return ARIB_STD_B25_ERROR_NON_TS_INPUT_STREAM;
	}

	prv->unit_size = n;

	return 0;
}

static int find_pat(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int r;
	int n;
	
	int32_t unit;

	uint8_t *p;
	uint8_t *curr;
	uint8_t *tail;

	TS_HEADER hdr;

	r = 0;
	unit = prv->unit_size;
	curr = prv->sbuf.head + prv->sbuf_offset;
	tail = prv->sbuf.tail;

	while( (curr+unit) <= tail ){
		if(curr[0] != 0x47){
			p = resync(curr, tail, unit);
			if(p == NULL){
				goto LAST;
			}
			curr = p;
		}
		extract_ts_header(&hdr, curr);
		if(hdr.pid == 0x0000){
			p = curr+4;
			if(hdr.adaptation_field_control & 0x02){
				p += (p[0]+1);
			}
			n = 188 - (p-curr);
			r = set_ts_section_data(&(prv->pat_next), p, n);
			if(r < 0){
				goto LAST;
			}
			if(check_ts_section(&(prv->pat_next))){
				if( (!check_ts_section_crc(&(prv->pat_next))) ||
				    (prv->pat_next.hdr.current_next_indicator == 0) ){
					reset_ts_section(&(prv->pat_next));
					curr += unit;
					continue;
				}
				swap_ts_section(&(prv->pat_curr), &(prv->pat_next));
				reset_ts_section(&(prv->pat_next));
				curr += unit;
				goto LAST;
			}
		}
		curr += unit;
	}

LAST:
	prv->sbuf_offset = curr - prv->sbuf.head;

	if(check_ts_section(&(prv->pat_curr))){
		r = proc_pat(prv);
	}

	return r;
}

static int proc_pat(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int i;
	int ofst;
	int len;
	int count;
	
	int32_t program_number;
	int32_t pid;

	uint8_t *head;
	uint8_t *tail;
	
	TS_PROGRAM *work;

	ofst = 8;
	len = prv->pat_curr.hdr.section_length - (5+4);

	count = len / 4;
	work = (TS_PROGRAM *)calloc(count, sizeof(TS_PROGRAM));
	if(work == NULL){
		return ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
	}
	
	if(prv->program != NULL){
		for(i=0;i<prv->p_count;i++){
			release_program(prv->program+i);
		}
		free(prv->program);
		prv->program = NULL;
	}
	prv->p_count = 0;
	memset(&(prv->map), 0, sizeof(prv->map));

	head = prv->pat_curr.buf.head + ofst;
	tail = head + len;

	i = 0;
	while( (head+3) < tail ){
		program_number = ((head[0] << 8) | head[1]);
		pid = ((head[2] << 8) | head[3]) & 0x1fff;
		if(program_number != 0){
			work[i].pmt_pid = pid;
			prv->map[pid] = work+i;
			i += 1;
		}
		head += 4;
	}

	prv->program = work;
	prv->p_count = i;

	return 0;
}

static int check_pmt_complete(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int i;

	for(i=0;i<prv->p_count;i++){
		if(!check_ts_section(&(prv->program[i].pmt_curr))){
			return 0;
		}
	}

	return 1;
}

static int find_pmt(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int r;
	int i,n;
	
	int32_t unit;

	uint8_t *p;
	uint8_t *curr;
	uint8_t *tail;

	TS_HEADER hdr;
	TS_PROGRAM *pgrm;

	r = 0;
	unit = prv->unit_size;
	curr = prv->sbuf.head + prv->sbuf_offset;
	tail = prv->sbuf.tail;

	while( (curr+unit) <= tail ){
		if(curr[0] != 0x47){
			p = resync(curr, tail, unit);
			if(p == NULL){
				goto LAST;
			}
			curr = p;
		}
		extract_ts_header(&hdr, curr);
		pgrm = prv->map[hdr.pid];
		if( (pgrm != NULL) && (!check_ts_section(&(pgrm->pmt_curr))) ){
			p = curr + 4;
			if(hdr.adaptation_field_control & 0x02){
				p += (p[0]+1);
			}
			n = 188 - (p-curr);
			r = set_ts_section_data(&(pgrm->pmt_next), p, n);
			if(r < 0){
				goto LAST;
			}
			if(check_ts_section(&(pgrm->pmt_next))){
				if( (!check_ts_section_crc(&(pgrm->pmt_next))) ||
				    (pgrm->pmt_next.hdr.current_next_indicator == 0) ){
					reset_ts_section(&(pgrm->pmt_next));
					curr += unit;
					continue;
				}
				swap_ts_section(&(pgrm->pmt_curr), &(pgrm->pmt_next));
				reset_ts_section(&(pgrm->pmt_next));
				if(check_pmt_complete(prv)){
					curr += unit;
					goto LAST;
				}
			}
		}
		curr += unit;
	}

LAST:
	prv->sbuf_offset = curr - prv->sbuf.head;

	if(check_pmt_complete(prv)){
		for(i=0;i<prv->p_count;i++){
			r = proc_pmt(prv->program+i);
			if(r < 0){
				break;
			}
		}
	}

	return r;
}

static int proc_pmt(TS_PROGRAM *pgrm)
{
	int r;

	int i;
	int ofst;
	int len;

	uint8_t *head;
	uint8_t *tail;

	uint32_t program_info_length;
	
	uint32_t es_info_length;

	r = 0;
	ofst = 8;
	len = pgrm->pmt_curr.hdr.section_length - (5+4);

	head = pgrm->pmt_curr.buf.head + ofst;
	tail = head + len;

	pgrm->pcr_pid = ((head[0] << 8) | head[1]) & 0x1fff;
	program_info_length = ((head[2] << 8) | head[3]) & 0x0fff;
	head += 4;
	if(head+program_info_length > tail){
		/* broken PMT - ignore */
		return 0;
	}

	head = proc_pmt_descriptor(pgrm, head, head+program_info_length);

	pgrm->s_count = count_pmt_stream(head, tail);
	if(pgrm->streams != NULL){
		free(pgrm->streams);
		pgrm->streams = NULL;
	}

	pgrm->streams = (TS_STREAM_INFO *)calloc(pgrm->s_count, sizeof(TS_STREAM_INFO));
	if(pgrm->streams == NULL){
		return ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
	}

	i = 0;
	while( head+4 < tail ){
		pgrm->streams[i].type = head[0];
		pgrm->streams[i].pid = ((head[1] << 8) | head[2]) & 0x1fff;
		es_info_length = ((head[3] << 8) | head[4]) & 0x0fff;
		i += 1;
		/* ignore ES descriptors */
		head += (5 + es_info_length);
	}

	return 0;
}

static uint8_t *proc_pmt_descriptor(TS_PROGRAM *pgrm, uint8_t *head, uint8_t *tail)
{
	uint32_t ca_pid;
	uint32_t ca_sys_id;
	
	uint32_t tag;
	uint32_t len;

	while(head+1 < tail){
		tag = head[0];
		len = head[1];
		head += 2;
		if( (tag == 0x09) && /* CA_descriptor */
		    (len >= 4) &&
		    (head+len <= tail) ){
			ca_sys_id = ((head[0] << 8) | head[1]);
			ca_pid = ((head[2] << 8) | head[3]) & 0x1fff;
			if(ca_pid != pgrm->ecm_pid){
				reset_ts_section(&(pgrm->ecm_curr));
				reset_ts_section(&(pgrm->ecm_next));
			}
			pgrm->ecm_pid = ca_pid;
		}
		head += len;
	}

	return tail;
}

static int32_t count_pmt_stream(uint8_t *head, uint8_t *tail)
{
	int32_t r;

	int32_t stream_type;
	int32_t stream_pid;
	int32_t es_info_length;

	r = 0;

	while(head+4 < tail){
		stream_type = head[0];
		stream_pid = ((head[1] << 8) | head[2]) & 0x1fff;
		es_info_length = ((head[3] << 8) | head[4]) & 0x0fff;
		r += 1;
		head += (5 + es_info_length);
	}

	return r;
}

static void setup_pid_map(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int i,j;
	uint32_t pid;
	TS_PROGRAM *pgrm;

	memset(&(prv->map), 0, sizeof(prv->map));

	for(i=0;i<prv->p_count;i++){
		pgrm = prv->program + i; 
		pid = pgrm->pmt_pid;
		if( (pid > 0) && (prv->map[pid] == NULL) ){
			prv->map[pid] = pgrm;
		}
		pid = pgrm->pcr_pid;
		if( (pid > 0) && (prv->map[pid] == NULL) ){
			prv->map[pid] = pgrm;
		}
		pid = pgrm->ecm_pid;
		if( (pid > 0) && (prv->map[pid] == NULL) ){
			prv->map[pid] = pgrm;
		}
		for(j=0;j<pgrm->s_count;j++){
			pid = pgrm->streams[j].pid;
			if( (pid > 0) && (prv->map[pid] == NULL) ){
				prv->map[pid] = pgrm;
			}
		}
	}
}

static int check_ecm_complete(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int i;
	
	uint32_t pid;
	TS_PROGRAM *pgrm;

	for(i=0;i<prv->p_count;i++){
		
		pid = prv->program[i].ecm_pid;
		if(pid == 0){
			/* non scramble program*/
			continue;
		}
		pgrm = prv->map[pid];
		if( pgrm != (prv->program + i) ){
			/* slave program */
			continue;
		}
		
		if( !check_ts_section(&(prv->program[i].ecm_curr)) ){
			/* scramble program and ECM is not received */
			return 0;
		}
	}

	return 1;
}

static int find_ecm(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int r;
	int n;

	int32_t unit;

	uint8_t *p;
	uint8_t *curr;
	uint8_t *tail;

	TS_HEADER hdr;
	TS_PROGRAM *pgrm;

	r = 0;
	unit = prv->unit_size;
	curr = prv->sbuf.head + prv->sbuf_offset;
	tail = prv->sbuf.tail;

	while( (curr+unit) <= tail ){
		if(curr[0] != 0x47){
			p = resync(curr, tail, unit);
			if(p == NULL){
				goto LAST;
			}
			curr = p;
		}
		extract_ts_header(&hdr, curr);
		pgrm = prv->map[hdr.pid];
		if( (pgrm != NULL) &&
		    (((uint32_t)hdr.pid) == pgrm->ecm_pid) &&
		    (!check_ts_section(&(pgrm->ecm_curr))) ){
			p = curr + 4;
			if(hdr.adaptation_field_control & 0x02){
				p += (p[0]+1);
			}
			n = 188 - (p-curr);
			r = set_ts_section_data(&(pgrm->ecm_next), p, n);
			if(r < 0){
				goto LAST;
			}
			if(check_ts_section(&(pgrm->ecm_next))){
				if( (!check_ts_section_crc(&(pgrm->ecm_next))) ||
				    (pgrm->ecm_next.hdr.current_next_indicator == 0) ){ 
					reset_ts_section(&(pgrm->ecm_next));
					curr += unit;
					continue;
				}
				swap_ts_section(&(pgrm->ecm_curr), &(pgrm->ecm_next));
				reset_ts_section(&(pgrm->ecm_next));
				if(check_ecm_complete(prv)){
					curr += unit;
					goto LAST;
				}
			}
		}
		curr += unit;
	}

LAST:
	prv->sbuf_offset = curr - prv->sbuf.head;

	return r;
}

static int setup_multi2(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int r;
	int i;
	
	uint32_t pid;
	TS_PROGRAM *pgrm;

	r = 0;

	for(i=0;i<prv->p_count;i++){
		pid = prv->program[i].ecm_pid;
		if(pid == 0){
			continue;
		}
		pgrm = prv->map[pid];
		if(pgrm != (prv->program+i)){
			prv->program[i].m2 = pgrm->m2;
			pgrm->m2->add_ref(pgrm->m2);
			continue;
		}
		r = proc_ecm(pgrm, prv->bcas);
		if(r < 0){
			return r;
		}
		if(pgrm->m2 != NULL){
			pgrm->m2->set_round(pgrm->m2, prv->multi2_round);
		}
	}

	return r;
}

static int proc_ecm(TS_PROGRAM *pgrm, B_CAS_CARD *bcas)
{
	int r;
	int ofst;
	int len;

	uint8_t *p;
	
	B_CAS_INIT_STATUS is;
	B_CAS_ECM_RESULT res;

	if(bcas == NULL){
		return ARIB_STD_B25_ERROR_EMPTY_B_CAS_CARD;
	}

	r = 0;
	
	if(pgrm->m2 == NULL){
		pgrm->m2 = create_multi2();
		if(pgrm->m2 == NULL){
			return ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
		}
		r = bcas->get_init_status(bcas, &is);
		if(r < 0){
			return ARIB_STD_B25_ERROR_INVALID_B_CAS_STATUS;
		}
		pgrm->m2->set_system_key(pgrm->m2, is.system_key);
		pgrm->m2->set_init_cbc(pgrm->m2, is.init_cbc);
	}

	ofst = 8;
	len = pgrm->ecm_curr.hdr.section_length - (5+4);
	p = pgrm->ecm_curr.buf.head + ofst;

	r = bcas->proc_ecm(bcas, &res, p, len);
	if(r < 0){
		pgrm->m2->clear_scramble_key(pgrm->m2);
		return ARIB_STD_B25_ERROR_ECM_PROC_FAILURE;
	}

	pgrm->m2->set_scramble_key(pgrm->m2, res.scramble_key);

	return r;
}

static int proc_arib_std_b25(ARIB_STD_B25_PRIVATE_DATA *prv)
{
	int r;
	int m,n;

	int32_t crypt;
	int32_t unit;
	uint32_t pid;

	uint8_t *p;
	uint8_t *curr;
	uint8_t *tail;

	TS_PROGRAM *pgrm;
	TS_HEADER hdr;

	unit = prv->unit_size;
	curr = prv->sbuf.head;
	tail = prv->sbuf.tail;

	m = prv->dbuf.tail - prv->dbuf.head;
	n = tail - curr;
	if(!reserve_work_buffer(&(prv->dbuf), m+n)){
		return ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
	}

	r = 0;

	while( (curr+unit) <= tail ){
		if(curr[0] != 0x47){
			p = resync(curr, tail, unit);
			if(p == NULL){
				goto LAST;
			}
			curr = p;
		}
		extract_ts_header(&hdr, curr);
		crypt = hdr.transport_scrambling_control;
		pid = hdr.pid;
		pgrm = prv->map[pid];
		if(pgrm == NULL){
			pgrm = prv->program + 0;
		}
		
		p = curr+4;
		
		if(hdr.adaptation_field_control & 0x01){
			if(hdr.adaptation_field_control & 0x02){
				p += (p[0]+1);
			}
			n = 188 - (p-curr);
		
			if( (crypt != 0) && (pgrm->m2 != NULL) ){
				m = pgrm->m2->decrypt(pgrm->m2, crypt, p, n);
				if(m < 0){
					r = ARIB_STD_B25_ERROR_DECRYPT_FAILURE;
					goto LAST;
				}
				curr[3] &= 0x3f;
			}
		}

		if(!append_work_buffer(&(prv->dbuf), curr, 188)){
			r = ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
			goto LAST;
		}

		if(pid == pgrm->ecm_pid){
			r = set_ts_section_data(&(pgrm->ecm_next), p, n);
			if(r < 0){
				goto LAST;
			}
			if(check_ts_section(&(pgrm->ecm_next))){
				if( (!check_ts_section_crc(&(pgrm->ecm_next))) ||
				    (pgrm->ecm_next.hdr.current_next_indicator == 0) ||
				    (compare_ts_section(&(pgrm->ecm_curr), &(pgrm->ecm_next)) == 0) ){
					reset_ts_section(&(pgrm->ecm_next));
					curr += unit;
					continue;
				}
				swap_ts_section(&(pgrm->ecm_curr), &(pgrm->ecm_next));
				reset_ts_section(&(pgrm->ecm_next));
				r = proc_ecm(pgrm, prv->bcas);
				if(r < 0){
					goto LAST;
				}
			}
		}else if(pid == pgrm->pmt_pid){
			r = set_ts_section_data(&(pgrm->pmt_next), p, n);
			if(r < 0){
				goto LAST;
			}
			if(check_ts_section(&(pgrm->pmt_next))){
				if( (!check_ts_section_crc(&(pgrm->pmt_next))) ||
				    (pgrm->pmt_next.hdr.current_next_indicator == 0) ||
				    (compare_ts_section(&(pgrm->pmt_curr), &(pgrm->pmt_next)) == 0) ){
					reset_ts_section(&(pgrm->pmt_next));
					curr += unit;
					continue;
				}
				swap_ts_section(&(pgrm->pmt_curr), &(pgrm->pmt_next));
				reset_ts_section(&(pgrm->pmt_next));
				r = proc_pmt(pgrm);
				if(r < 0){
					goto LAST;
				}
				setup_pid_map(prv);
				curr += unit;
				goto LAST;
			}
		}else if(pid == 0x0000){
			r = set_ts_section_data(&(prv->pat_next), p, n);
			if(r < 0){
				goto LAST;
			}
			if(check_ts_section(&(prv->pat_next))){
				if( (!check_ts_section_crc(&(prv->pat_next))) ||
				    (prv->pat_next.hdr.current_next_indicator == 0) ||
				    (compare_ts_section(&(prv->pat_curr), &(prv->pat_next)) == 0)){
					reset_ts_section(&(prv->pat_next));
					curr += unit;
					continue;
				}
				swap_ts_section(&(prv->pat_curr), &(prv->pat_next));
				reset_ts_section(&(prv->pat_next));
				r = proc_pat(prv);
				if(r < 0){
					goto LAST;
				}
				curr += unit;
				goto LAST;
			}
		}

		curr += unit;
	}

LAST:
	m = curr - prv->sbuf.head;
	n = tail - curr;
	if( (n < 1024) || (m > (prv->sbuf.max/2) ) ){
		p = prv->sbuf.pool;
		memcpy(p, curr, n);
		prv->sbuf.head = p;
		prv->sbuf.tail = p+n;
	}else{
		prv->sbuf.head = curr;
	}

	return r;
}

static void release_program(TS_PROGRAM *pgrm)
{
	release_ts_section(&(pgrm->pmt_curr));
	release_ts_section(&(pgrm->pmt_next));

	release_ts_section(&(pgrm->ecm_curr));
	release_ts_section(&(pgrm->ecm_next));

	if(pgrm->streams != NULL){
		free(pgrm->streams);
		pgrm->streams = NULL;
	}

	if(pgrm->m2 != NULL){
		pgrm->m2->release(pgrm->m2);
		pgrm->m2 = NULL;
	}
}

static int reserve_work_buffer(TS_WORK_BUFFER *buf, int32_t size)
{
	int m,n;
	uint8_t *p;
	
	if(buf->max >= size){
		return 1;
	}

	if(buf->max < 512){
		n = 512;
	}else{
		n = buf->max * 2;
	}
	
	while(n < size){
		n += n;
	}

	p = (uint8_t *)malloc(n);
	if(p == NULL){
		return 0;
	}

	m = 0;
	if(buf->pool != NULL){
		m = buf->tail - buf->head;
		if(m > 0){
			memcpy(p, buf->head, m);
		}
		free(buf->pool);
		buf->pool = NULL;
	}

	buf->pool = p;
	buf->head = p;
	buf->tail = p+m;
	buf->max = n;

	return 1;
}

static int append_work_buffer(TS_WORK_BUFFER *buf, uint8_t *data, int32_t size)
{
	int m;

	m = buf->tail - buf->pool;

	if( (m+size) > buf->max ){
		if(!reserve_work_buffer(buf, m+size)){
			return 0;
		}
	}

	memcpy(buf->tail, data, size);
	buf->tail += size;

	return 1;
}

static void reset_work_buffer(TS_WORK_BUFFER *buf)
{
	buf->head = buf->pool;
	buf->tail = buf->pool;
}

static void release_work_buffer(TS_WORK_BUFFER *buf)
{
	if(buf->pool != NULL){
		free(buf->pool);
	}
	buf->pool = NULL;
	buf->head = NULL;
	buf->tail = NULL;
	buf->max = 0;
}

static int set_ts_section_data(TS_SECTION *sect, uint8_t *data, int32_t size)
{
	int m,n;
	uint8_t *p;

	n = sect->buf.tail - sect->buf.head;
	if(n == 0){ /* new section */
		m = data[0] + 1;
		p = data + m; /* increment pointer field */
		if(!append_work_buffer(&(sect->buf), p, size-m)){
			return ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
		}
	}else{ /* continuous section */
		if(!append_work_buffer(&(sect->buf), data, size)){
			return ARIB_STD_B25_ERROR_NO_ENOUGH_MEMORY;
		}
	}
		
	if(sect->hdr.section_length == 0){
		m = sect->buf.tail - sect->buf.head;
		if(m < 9){
			/* need more data */
			return 0;
		}
		extract_ts_section_header(&(sect->hdr), sect->buf.head);
	}

	return 0;
}

static int check_ts_section(TS_SECTION *sect)
{
	int m,n;

	if(sect->hdr.section_length < 1){
		/* not complete */
		return 0;
	}

	n = sect->hdr.section_length + 3;
	m = sect->buf.tail - sect->buf.head;
	if(m >= n){
		/* complete */
		return 1;
	}

	return 0;
}

static int check_ts_section_crc(TS_SECTION *sect)
{
	int m;
	uint32_t crc;

	if(sect->hdr.section_length < 1){
		return 0;
	}

	m = sect->hdr.section_length + 3;
	crc = crc32(sect->buf.head, sect->buf.head+m);
	if(crc != 0){
		/* crc32 missmatch */
		return 0;
	}

	return 1;
}

static void reset_ts_section(TS_SECTION *sect)
{
	sect->buf.head = sect->buf.pool;
	sect->buf.tail = sect->buf.pool;
	memset(&(sect->hdr), 0, sizeof(TS_SECTION_HEADER));
}

static void swap_ts_section(TS_SECTION *curr, TS_SECTION *next)
{
	TS_SECTION w;

	memcpy(&w, curr, sizeof(TS_SECTION));
	memcpy(curr, next, sizeof(TS_SECTION));
	memcpy(next, &w, sizeof(TS_SECTION));
}

static int compare_ts_section(TS_SECTION *curr, TS_SECTION *next)
{
	int m;
	
	if(curr->hdr.section_length != next->hdr.section_length){
		return 1;
	}

	m = curr->hdr.section_length + 3;
	return memcmp(curr->buf.head, next->buf.head, m);
}

static void release_ts_section(TS_SECTION *sect)
{
	release_work_buffer(&(sect->buf));
	memset(&(sect->hdr), 0, sizeof(TS_SECTION_HEADER));
}

static void extract_ts_header(TS_HEADER *dst, uint8_t *src)
{
	dst->sync                         =  src[0];
	dst->transport_error_indicator    = (src[1] >> 7) & 0x01;
	dst->payload_unit_start_indicator = (src[1] >> 6) & 0x01;
	dst->transport_priority           = (src[1] >> 5) & 0x01;
	dst->pid = ((src[1] & 0x1f) << 8) | src[2];
	dst->transport_scrambling_control = (src[3] >> 6) & 0x03;
	dst->adaptation_field_control     = (src[3] >> 4) & 0x03;
	dst->continuity_counter           =  src[3]       & 0x0f;
}

static void extract_ts_section_header(TS_SECTION_HEADER *dst, uint8_t *src)
{
	dst->table_id                     =  src[0];
	dst->section_syntax_indicator     = (src[1] >> 7) & 0x01;
	dst->private_indicator            = (src[1] >> 6) & 0x01;
	dst->section_length               =((src[1] << 8) | src[2]) & 0x0fff;
	if(dst->section_syntax_indicator){
		dst->transport_stream_id    =((src[3] << 8) | src[4]);
		dst->version_number         = (src[5] >> 1) & 0x1f;
		dst->current_next_indicator =  src[5]       & 0x01;
		dst->section_number         =  src[6];
		dst->last_section_number    =  src[7];
	}else{
		dst->transport_stream_id    = 0;
		dst->version_number         = 0;
		dst->current_next_indicator = 0;
		dst->section_number         = 0;
		dst->last_section_number    = 0;
	}
}

static int check_unit_invert(unsigned char *head, unsigned char *tail)
{
	unsigned char *buf;

	buf = tail-188;

	while(head <= buf){
		if(buf[0] == 0x47){
			return tail-buf;
		}
		buf -= 1;
	}

	return 0;
}

static uint8_t *resync(uint8_t *head, uint8_t *tail, int32_t unit_size)
{
	int i;
	unsigned char *buf;

	buf = head;
	tail -= unit_size * 8;
	while( buf <= tail ){
		if(buf[0] == 0x47){
			for(i=1;i<8;i++){
				if(buf[unit_size*i] != 0x47){
					break;
				}
			}
			if(i == 8){
				return buf;
			}
		}
		buf += 1;
	}

	return NULL;
}

static uint8_t *resync_force(uint8_t *head, uint8_t *tail, int32_t unit_size)
{
	int i,n;
	unsigned char *buf;

	buf = head;
	while( buf <= (tail-188) ){
		if(buf[0] == 0x47){
			n = (tail - buf) / unit_size;
			if(n == 0){
				return buf;
			}
			for(i=1;i<n;i++){
				if(buf[unit_size*i] != 0x47){
					break;
				}
			}
			if(i == n){
				return buf;
			}
		}
		buf += 1;
	}

	return NULL;
}

static uint32_t crc32(uint8_t *head, uint8_t *tail)
{
	uint32_t crc;
	uint8_t *p;

	static const uint32_t table[256] = {
		0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9,
		0x130476DC, 0x17C56B6B, 0x1A864DB2, 0x1E475005,
		0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61, 
		0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD,
		
		0x4C11DB70, 0x48D0C6C7, 0x4593E01E, 0x4152FDA9,
		0x5F15ADAC, 0x5BD4B01B, 0x569796C2, 0x52568B75, 
		0x6A1936C8, 0x6ED82B7F, 0x639B0DA6, 0x675A1011,
		0x791D4014, 0x7DDC5DA3, 0x709F7B7A, 0x745E66CD,
		
		0x9823B6E0, 0x9CE2AB57, 0x91A18D8E, 0x95609039,
		0x8B27C03C, 0x8FE6DD8B, 0x82A5FB52, 0x8664E6E5,
		0xBE2B5B58, 0xBAEA46EF, 0xB7A96036, 0xB3687D81, 
		0xAD2F2D84, 0xA9EE3033, 0xA4AD16EA, 0xA06C0B5D,
		
		0xD4326D90, 0xD0F37027, 0xDDB056FE, 0xD9714B49,
		0xC7361B4C, 0xC3F706FB, 0xCEB42022, 0xCA753D95,
		0xF23A8028, 0xF6FB9D9F, 0xFBB8BB46, 0xFF79A6F1, 
		0xE13EF6F4, 0xE5FFEB43, 0xE8BCCD9A, 0xEC7DD02D,

		0x34867077, 0x30476DC0, 0x3D044B19, 0x39C556AE,
		0x278206AB, 0x23431B1C, 0x2E003DC5, 0x2AC12072, 
		0x128E9DCF, 0x164F8078, 0x1B0CA6A1, 0x1FCDBB16,
		0x018AEB13, 0x054BF6A4, 0x0808D07D, 0x0CC9CDCA,

		0x7897AB07, 0x7C56B6B0, 0x71159069, 0x75D48DDE, 
		0x6B93DDDB, 0x6F52C06C, 0x6211E6B5, 0x66D0FB02,
		0x5E9F46BF, 0x5A5E5B08, 0x571D7DD1, 0x53DC6066,
		0x4D9B3063, 0x495A2DD4, 0x44190B0D, 0x40D816BA,
		
		0xACA5C697, 0xA864DB20, 0xA527FDF9, 0xA1E6E04E,
		0xBFA1B04B, 0xBB60ADFC, 0xB6238B25, 0xB2E29692,
		0x8AAD2B2F, 0x8E6C3698, 0x832F1041, 0x87EE0DF6, 
		0x99A95DF3, 0x9D684044, 0x902B669D, 0x94EA7B2A,

		0xE0B41DE7, 0xE4750050, 0xE9362689, 0xEDF73B3E,
		0xF3B06B3B, 0xF771768C, 0xFA325055, 0xFEF34DE2, 
		0xC6BCF05F, 0xC27DEDE8, 0xCF3ECB31, 0xCBFFD686,
		0xD5B88683, 0xD1799B34, 0xDC3ABDED, 0xD8FBA05A,

		0x690CE0EE, 0x6DCDFD59, 0x608EDB80, 0x644FC637, 
		0x7A089632, 0x7EC98B85, 0x738AAD5C, 0x774BB0EB,
		0x4F040D56, 0x4BC510E1, 0x46863638, 0x42472B8F,
		0x5C007B8A, 0x58C1663D, 0x558240E4, 0x51435D53, 
		
		0x251D3B9E, 0x21DC2629, 0x2C9F00F0, 0x285E1D47,
		0x36194D42, 0x32D850F5, 0x3F9B762C, 0x3B5A6B9B,
		0x0315D626, 0x07D4CB91, 0x0A97ED48, 0x0E56F0FF,
		0x1011A0FA, 0x14D0BD4D, 0x19939B94, 0x1D528623,

		0xF12F560E, 0xF5EE4BB9, 0xF8AD6D60, 0xFC6C70D7,
		0xE22B20D2, 0xE6EA3D65, 0xEBA91BBC, 0xEF68060B, 
		0xD727BBB6, 0xD3E6A601, 0xDEA580D8, 0xDA649D6F,
		0xC423CD6A, 0xC0E2D0DD, 0xCDA1F604, 0xC960EBB3,
		
		0xBD3E8D7E, 0xB9FF90C9, 0xB4BCB610, 0xB07DABA7, 
		0xAE3AFBA2, 0xAAFBE615, 0xA7B8C0CC, 0xA379DD7B,
		0x9B3660C6, 0x9FF77D71, 0x92B45BA8, 0x9675461F,
		0x8832161A, 0x8CF30BAD, 0x81B02D74, 0x857130C3, 
		
		0x5D8A9099, 0x594B8D2E, 0x5408ABF7, 0x50C9B640,
		0x4E8EE645, 0x4A4FFBF2, 0x470CDD2B, 0x43CDC09C,
		0x7B827D21, 0x7F436096, 0x7200464F, 0x76C15BF8,
		0x68860BFD, 0x6C47164A, 0x61043093, 0x65C52D24,

		0x119B4BE9, 0x155A565E, 0x18197087, 0x1CD86D30,
		0x029F3D35, 0x065E2082, 0x0B1D065B, 0x0FDC1BEC,
		0x3793A651, 0x3352BBE6, 0x3E119D3F, 0x3AD08088,
		0x2497D08D, 0x2056CD3A, 0x2D15EBE3, 0x29D4F654,

		0xC5A92679, 0xC1683BCE, 0xCC2B1D17, 0xC8EA00A0,
		0xD6AD50A5, 0xD26C4D12, 0xDF2F6BCB, 0xDBEE767C,
		0xE3A1CBC1, 0xE760D676, 0xEA23F0AF, 0xEEE2ED18,
		0xF0A5BD1D, 0xF464A0AA, 0xF9278673, 0xFDE69BC4, 

		0x89B8FD09, 0x8D79E0BE, 0x803AC667, 0x84FBDBD0,
		0x9ABC8BD5, 0x9E7D9662, 0x933EB0BB, 0x97FFAD0C,
		0xAFB010B1, 0xAB710D06, 0xA6322BDF, 0xA2F33668, 
		0xBCB4666D, 0xB8757BDA, 0xB5365D03, 0xB1F740B4,
	};
	
	crc = 0xffffffff;

	p = head;
	while(p < tail){
		crc = (crc << 8) ^ table[ ((crc >> 24) ^ p[0]) & 0xff ];
		p += 1;
	}

	return crc;
}

