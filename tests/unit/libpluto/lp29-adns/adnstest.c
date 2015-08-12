#define LEAK_DETECTIVE
#define AGGRESSIVE 1
#define XAUTH
#define MODECFG
#define DEBUG 1
#define PRINT_SA_DEBUG 1
#define USE_KEYRR 1

#include <stdlib.h>
#include <sys/resource.h>
#include <signal.h>
#include <errno.h>
#include <arpa/nameser.h>
#include <poll.h>
#include "sysdep.h"
#include "efencedef.h"
#include "constants.h"
#include "openswan.h"
#include "oswtime.h"
#include "oswalloc.h"
#include "whack.h"
#include "../../programs/pluto/rcv_whack.h"

#include "dnskey.h"
#include "pluto/defs.h"
#include "demux.h"
#include "pluto/log.h"

/* seams */
#include "whackmsgtestlib.c"
#include "seam_log.c"
#include "seam_whack.c"
#include "seam_exitlog.c"
#include "seam_hostpair.c"

const char *progname=NULL;
int verbose=0;
int warningsarefatal = 0;

#define TESTNAME "adnstest"

/* perform wait4() on all children */
static void
reapchildren(void)
{
    pid_t child;
    int status;
    struct rusage r;

    errno=0;

    while((child = wait3(&status, WNOHANG, &r)) > 0) {
	/* got a child to reap */
	if(adns_reapchild(child, status)) continue;

	openswan_log("child pid=%d (status=%d) is not my child!", child, status);
    }

    if(child == -1) {
	openswan_log("reapchild failed with errno=%d %s",
		     errno, strerror(errno));
    }
}

static void
childhandler(int sig UNUSED)
{
    reapchildren();
}

void moon_continue(struct adns_continuation *cr, err_t ugh)
{
    DBG_log("moon continue with: %s", ugh ? ugh : "no-error");
}

void cassidy_continue(struct adns_continuation *cr, err_t ugh)
{
    if(ugh) {
        DBG_log("cassidy error: %s", ugh);
        /* continuation is freed by dnskey */
        return;
    }
}

void cassidy_host_continue(struct adns_continuation *cr, err_t ugh)
{
    if(ugh) {
        DBG_log("cassidy error: %s", ugh);
        /* continuation is freed by dnskey */
        return;
    }
    struct addrinfo *ai = sort_addr_info(cr->ipanswers);
    dump_addr_info(ai);
}


main(int argc, char *argv[])
{
    bool  recalculate = FALSE;
    int   len;
    err_t e;
    err_t err = NULL;
    char *infile;
    char *conn_name;
    int  lineno=0;
    struct connection *c1 = NULL;
    struct id moon, cassidy;
    struct adns_continuation *cr1 = NULL;

#ifdef HAVE_EFENCE
    EF_PROTECT_FREE=1;
#endif

    initproctitle(argc, argv);
    progname = argv[0];
    leak_detective = 1;

    if(argc != 3 && argc!=4) {
	fprintf(stderr, "Usage: %s [-r] <whackrecord> <conn-name>\n", progname);
	exit(10);
    }
    /* skip argv0 */
    argc--; argv++;

    if(strcmp(argv[0], "-r")==0) {
        recalculate = 1;    /* do all crypto */
        argc--; argv++;
    }

    tool_init_log();
    cur_debugging |= DBG_DNS;
    init_adns();

    {
    	int r;
	struct sigaction act;

	act.sa_handler = &childhandler;
	act.sa_flags   = SA_RESTART;
	r = sigaction(SIGCHLD, &act, NULL);
	passert(r == 0);
    }

    /* setup a query */
    cr1 = alloc_thing(struct adns_continuation, "moon lookup");
    moon.kind = ID_FQDN;
    strtochunk(moon.name, "moon.testing.openswan.org", "dns name");
    e = start_adns_query(&moon, NULL, ns_t_key,
                         moon_continue, cr1);

    cr1 = alloc_thing(struct adns_continuation, "cassidy lookup");
    cassidy.kind = ID_FQDN;
    strtochunk(cassidy.name, "cassidy.sandelman.ca", "dns name 2");
    e = start_adns_query(&cassidy, NULL, ns_t_key,
                         cassidy_continue, cr1);

    /* re-use cassidy */
    cr1 = alloc_thing(struct adns_continuation, "cassidy A lookup");
    e = start_adns_hostname("cassidy.sandelman.ca", cassidy_host_continue, cr1);

    reset_globals();
    send_unsent_ADNS_queries();

    while(adns_any_in_flight()) {
        struct pollfd one;
        struct timespec waiting;
        int n;

        one.fd = adns_afd;
        one.events = POLLIN;
        waiting.tv_sec = 30;
        waiting.tv_nsec= 0;
        n = ppoll(&one, 1, &waiting, NULL);
        if(n==1 && one.revents & POLLIN) {
            handle_adns_answer();
        } else {
            DBG_log("poll failed with: %d", n);
            exit(5);
        }

        send_unsent_ADNS_queries();
    }

    report_leaks();

    tool_close_log();
    exit(0);
}


 /*
 * Local Variables:
 * c-style: pluto
 * c-basic-offset: 4
 * compile-command: "make check"
 * End:
 */


