#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <windows.h>
#include <crtdbg.h>

#include "arib_std_b25.h"
#include "b_cas_card.h"

typedef struct {
	int32_t round;
} OPTION;

static void show_usage();
static int parse_arg(OPTION *dst, int argc, char **argv);
static void test_arib_std_b25(const char *src, const char *dst, OPTION *opt);

int main(int argc, char **argv)
{
	int n;
	OPTION opt;
	
	_CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE );
	_CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDOUT );
	_CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE );
	_CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDOUT );
	_CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE );
	_CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDOUT );
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF|_CRTDBG_DELAY_FREE_MEM_DF|_CRTDBG_CHECK_ALWAYS_DF|_CRTDBG_LEAK_CHECK_DF);

	n = parse_arg(&opt, argc, argv);
	if(n+2 > argc){
		show_usage();
		exit(EXIT_FAILURE);
	}

	test_arib_std_b25(argv[n+0], argv[n+1], &opt);

	_CrtDumpMemoryLeaks();

	return EXIT_SUCCESS;
}

static void show_usage()
{
	fprintf(stderr, "b25 - ARIB STD-B25 test program ver. 0.1.5 (2008, 2/12)\n");
	fprintf(stderr, "usage: b25 [options] src.m2t dst.m2t\n");
	fprintf(stderr, "options:\n");
	fprintf(stderr, "  -r round (integer, default=4)\n");
	fprintf(stderr, "\n");
}

static int parse_arg(OPTION *dst, int argc, char **argv)
{
	int i;
	
	dst->round = 4;

	for(i=1;i<argc;i++){
		if(argv[i][0] != '-'){
			break;
		}
		switch(argv[i][1]){
		case 'r':
			if(argv[i][2]){
				dst->round = atoi(argv[i]+2);
			}else{
				dst->round = atoi(argv[i+1]);
				i += 1;
			}
			break;
		default:
			fprintf(stderr, "error - unknown option '-%c'\n", argv[i][1]);
			return argc;
		}
	}

	return i;
}

static void test_arib_std_b25(const char *src, const char *dst, OPTION *opt)
{
	int code,i,n;
	int sfd,dfd;

	ARIB_STD_B25 *b25;
	B_CAS_CARD   *bcas;

	ARIB_STD_B25_PROGRAM_INFO pgrm;

	uint8_t data[8*1024];

	ARIB_STD_B25_BUFFER sbuf;
	ARIB_STD_B25_BUFFER dbuf;

	sfd = -1;
	dfd = -1;
	b25 = NULL;
	bcas = NULL;

	sfd = _open(src, _O_BINARY|_O_RDONLY|_O_SEQUENTIAL);
	if(sfd < 0){
		fprintf(stderr, "error - failed on _open(%s) [src]\n", src);
		goto LAST;
	}

	b25 = create_arib_std_b25();
	if(b25 == NULL){
		fprintf(stderr, "error - failed on create_arib_std_b25()\n");
		goto LAST;
	}

	code = b25->set_multi2_round(b25, opt->round);
	if(code < 0){
		fprintf(stderr, "error - failed on ARIB_STD_B25::set_multi2_round() : code=%d\n", code);
		goto LAST;
	}

	bcas = create_b_cas_card();
	if(bcas == NULL){
		fprintf(stderr, "error - failed on create_b_cas_card()\n");
		goto LAST;
	}

	code = bcas->init(bcas);
	if(code < 0){
		fprintf(stderr, "error - failed on B_CAS_CARD::init() : code=%d\n", code);
		goto LAST;
	}

	code = b25->set_b_cas_card(b25, bcas);
	if(code < 0){
		fprintf(stderr, "error - failed on ARIB_STD_B25::set_b_cas_card() : code=%d\n", code);
		goto LAST;
	}

	dfd = _open(dst, _O_BINARY|_O_WRONLY|_O_SEQUENTIAL|_O_CREAT|_O_TRUNC, _S_IREAD|_S_IWRITE);
	if(dfd < 0){
		fprintf(stderr, "error - failed on _open(%s) [dst]\n", dst);
		goto LAST;
	}

	while( (n = _read(sfd, data, sizeof(data))) > 0 ){
		sbuf.data = data;
		sbuf.size = n;

		code = b25->put(b25, &sbuf);
		if(code < 0){
			fprintf(stderr, "error - failed on ARIB_STD_B25::put() : code=%d\n", code);
			goto LAST;
		}

		code = b25->get(b25, &dbuf);
		if(code < 0){
			fprintf(stderr, "error - failed on ARIB_STD_B25::get() : code=%d\n", code);
			goto LAST;
		}

		if(dbuf.size > 0){
			n = _write(dfd, dbuf.data, dbuf.size);
			if(n != dbuf.size){
				fprintf(stderr, "error failed on _write(%d)\n", dbuf.size);
				goto LAST;
			}
		}
	}

	code = b25->flush(b25);
	if(code < 0){
		fprintf(stderr, "error - failed on ARIB_STD_B25::flush() : code=%d\n", code);
		goto LAST;
	}
	
	code = b25->get(b25, &dbuf);
	if(code < 0){
		fprintf(stderr, "error - failed on ARIB_STD_B25::get() : code=%d\n", code);
		goto LAST;
	}

	if(dbuf.size > 0){
		n = _write(dfd, dbuf.data, dbuf.size);
		if(n != dbuf.size){
			fprintf(stderr, "error - failed on _write(%d)\n", dbuf.size);
			goto LAST;
		}
	}

	n = b25->get_program_count(b25);
	if(n < 0){
		fprintf(stderr, "error - failed on ARIB_STD_B25::get_program_count() : code=%d\n", code);
		goto LAST;
	}
	for(i=0;i<n;i++){
		code = b25->get_program_info(b25, &pgrm, i);
		if(code < 0){
			fprintf(stderr, "error - failed on ARIB_STD_B25::get_program_info(%d) : code=%d\n", i, code);
			goto LAST;
		}
		if(pgrm.ecm_unpurchased_count > 0){
			fprintf(stderr, "warning - unpurchased ECM is detected\n");
			fprintf(stderr, "  channel:               %d\n", pgrm.program_number);
			fprintf(stderr, "  unpurchased ECM count: %d\n", pgrm.ecm_unpurchased_count);
			fprintf(stderr, "  last ECM error code:   %04x\n", pgrm.last_ecm_error_code);
			fprintf(stderr, "  undecrypted TS packet: %d\n", pgrm.undecrypted_packet_count);
			fprintf(stderr, "  total TS packet:       %d\n", pgrm.total_packet_count);
		}
	}
	
LAST:

	if(sfd >= 0){
		_close(sfd);
		sfd = -1;
	}

	if(dfd >= 0){
		_close(dfd);
		dfd = -1;
	}

	if(b25 != NULL){
		b25->release(b25);
		b25 = NULL;
	}

	if(bcas != NULL){
		bcas->release(bcas);
		bcas = NULL;
	}
}


