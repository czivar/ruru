#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_hash.h>
#include <rte_errno.h>
#include <zmq.h>
	

static volatile bool force_quit;

/* Disabling forwarding */
static int forwarding = 0;

/* Debug mode */
static int debug = 0;

#define RTE_LOGTYPE_DPDKLATENCY RTE_LOGTYPE_USER1

#define NB_MBUF   8192

#define MAX_PKT_BURST 32
#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */
#define MEMPOOL_CACHE_SIZE 256
#define NB_SOCKETS 8

/*
 * Configurable number of RX/TX ring descriptors
 */
#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

/* mask of enabled ports */
static uint32_t dpdklatency_enabled_port_mask = 0;

struct mbuf_table {
	unsigned len;
	struct rte_mbuf *m_table[MAX_PKT_BURST];
};

#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_PORT 16

static struct rte_eth_dev_tx_buffer *tx_buffer[RTE_MAX_ETHPORTS];

/* Magic hash key for symmetric RSS */
#define RSS_HASH_KEY_LENGTH 40
static uint8_t hash_key[RSS_HASH_KEY_LENGTH] = { 0x6D, 0x5A, 0x6D, 0x5A, 0x6D,
	0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
	0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D,
	0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, };

/* Port configuration structure */
static const struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode    = ETH_MQ_RX_RSS, /**< RSS enables */
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 0, /**< IP checksum offload disabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		.jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 0, /**< CRC stripped by hardware */
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = hash_key,
			.rss_hf = ETH_RSS_PROTO_MASK,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

struct rte_mempool * dpdklatency_pktmbuf_pool = NULL;

/* Per-lcore (essentially queue) statistics struct */
struct dpdklatency_lcore_statistics {
	uint64_t tx;
	uint64_t rx;
	uint64_t dropped;
} __rte_cache_aligned;
struct dpdklatency_lcore_statistics lcore_statistics[RTE_MAX_LCORE];

#define MAX_TIMER_PERIOD 86400 /* 1 day max */
/* A tsc-based timer responsible for triggering statistics printout */
static uint64_t timer_period = 10; /* default period is 10 seconds */

#define TIMESTAMP_HASH_ENTRIES 99999

typedef struct rte_hash lookup_struct_t;
static lookup_struct_t *ipv4_timestamp_lookup_struct[NB_SOCKETS];


#ifdef RTE_MACHINE_CPUFLAG_SSE4_2
#include <rte_hash_crc.h>
#define DEFAULT_HASH_FUNC       rte_hash_crc
#else
#include <rte_jhash.h>
#define DEFAULT_HASH_FUNC       rte_jhash
#endif

#define CLOCK_PRECISION 1000000000L /* one billion */

struct lcore_rx_queue {
	uint8_t port_id;
	uint8_t queue_id;
} __rte_cache_aligned;

#define MAX_LCORE_PARAMS 1024
struct lcore_params {
	uint8_t port_id;
	uint8_t queue_id;
	uint8_t lcore_id;
} __rte_cache_aligned;

// Configure port-queue-lcore assigment here
static struct lcore_params lcore_params_array[MAX_LCORE_PARAMS];
static struct lcore_params lcore_params_array_default[] = {
	{0, 0, 1},
	{0, 1, 2},
	{0, 2, 3},
	{0, 3, 4},
};

static struct lcore_params * lcore_params = lcore_params_array_default;
static uint16_t nb_lcore_params = sizeof(lcore_params_array_default) /
				sizeof(lcore_params_array_default[0]);

struct lcore_conf {
	uint16_t n_rx_queue;
	struct lcore_rx_queue rx_queue_list[MAX_RX_QUEUE_PER_LCORE];
	uint16_t tx_queue_id[RTE_MAX_ETHPORTS];
	struct mbuf_table tx_mbufs[RTE_MAX_ETHPORTS];
	void * zmq_client;
	void * zmq_client_header;
	lookup_struct_t * ipv4_lookup_struct;
} __rte_cache_aligned;

static struct lcore_conf lcore_conf[RTE_MAX_LCORE] __rte_cache_aligned;

static const char* publishto;

static void
send_header_zmq_ipv4(uint8_t * alldata, uint32_t length)
{
	unsigned lcore_id = rte_lcore_id();
	struct lcore_conf *qconf;
	qconf = &lcore_conf[lcore_id];
	void *zmq_client = qconf->zmq_client_header;

	if (zmq_client != NULL){
		zmq_send (zmq_client, alldata, length, 0);
	}
}

static void
send_to_zmq_ipv4(uint32_t sourceip, uint32_t destip, unsigned long long int timestamp_ext, unsigned long long int timestamp_int)
{
	unsigned lcore_id = rte_lcore_id();
	struct lcore_conf *qconf;
	qconf = &lcore_conf[lcore_id];
	void *zmq_client = qconf->zmq_client;
	//message length is 28 bytes!
	char message[3+1+8+1+8+1+10+1+10+2];

	snprintf(message, sizeof(message), "LAT-%08x-%08x-%010llu-%010llu-", 
		(unsigned) sourceip, (unsigned) destip, timestamp_ext, timestamp_int);

	if (debug){
		printf("%s\n", message);
		fflush(stdout);
	}
	if (zmq_client != NULL){
		zmq_send (zmq_client, message, sizeof(message), 0);
	}
}


static void
track_latency_syn_v4(uint64_t key, uint64_t *ipv4_timestamp_syn)
{
	int ret = 0;
	unsigned lcore_id;
	struct timespec timestamp;

	lcore_id = rte_lcore_id();

	ret = rte_hash_add_key (ipv4_timestamp_lookup_struct[lcore_id], (void *) &key);
	//printf("SYN lcore %u, ret: %d \n", lcore_id, ret);
	if (ret < 0) {
		RTE_LOG(INFO, DPDKLATENCY, "Hash table full for lcore %u - clearing it\n", lcore_id);
		rte_hash_reset(ipv4_timestamp_lookup_struct[lcore_id]);
		ret = rte_hash_add_key (ipv4_timestamp_lookup_struct[lcore_id], (void *) &key);
		if (ret < 0) {
			rte_exit(EXIT_FAILURE, "Unable to add SYN timestamp to hash after cleaning it");
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &timestamp);
	ipv4_timestamp_syn[ret] = CLOCK_PRECISION * timestamp.tv_sec + timestamp.tv_nsec;
}

static void
track_latency_synack_v4(uint64_t key, uint64_t *ipv4_timestamp_synack)
{
	int ret = 0;
	unsigned lcore_id;
	struct timespec timestamp;

	lcore_id = rte_lcore_id();

	ret = rte_hash_lookup(ipv4_timestamp_lookup_struct[lcore_id], (const void *) &key);
	//printf("SYNACK lcore %u, ret: %d \n", lcore_id, ret);
	if (ret >= 0 ) {
		clock_gettime(CLOCK_MONOTONIC, &timestamp);
		ipv4_timestamp_synack[ret] = CLOCK_PRECISION * timestamp.tv_sec + timestamp.tv_nsec;
	}
}

static void
track_latency_ack_v4(uint64_t key, uint32_t sourceip, uint32_t destip, uint64_t *ipv4_timestamp_syn, uint64_t *ipv4_timestamp_synack)
{
	unsigned lcore_id;
	struct timespec timestamp;
	double elapsed_internal;
	double elapsed_external;
	int ret = 0;

	lcore_id = rte_lcore_id();

	ret = rte_hash_lookup(ipv4_timestamp_lookup_struct[lcore_id], (const void *) &key);
	if (ret >= 0) {
		clock_gettime(CLOCK_MONOTONIC, &timestamp);
		elapsed_external = ipv4_timestamp_synack[ret] - ipv4_timestamp_syn[ret];
		elapsed_internal = (CLOCK_PRECISION * timestamp.tv_sec + timestamp.tv_nsec) - ipv4_timestamp_synack[ret];
		//printf("SYN-ACK %d %llu microsec \n", ret, (unsigned long long int) elapsed_external / 1000);
		// If elapsed ms is more than 9999, we do not send it 
		if ( ((elapsed_internal / 1000000) < 9999) && ((elapsed_external / 1000000) < 9999)){
			send_to_zmq_ipv4(destip, sourceip, 
					(unsigned long long int) elapsed_external / 1000, 
					(unsigned long long int) elapsed_internal / 1000);
		}
		rte_hash_del_key (ipv4_timestamp_lookup_struct[lcore_id], (void *) &key);
	}
}


/* For vlan tagged packets, we need to find the offset in order to remove tagging */
static inline size_t
get_vlan_offset(struct ether_hdr *eth_hdr, uint16_t *proto)
{
        size_t vlan_offset = 0;
        if (rte_cpu_to_be_16(ETHER_TYPE_VLAN) == *proto) {
                struct vlan_hdr *vlan_hdr = (struct vlan_hdr *)(eth_hdr + 1);
                vlan_offset = sizeof(struct vlan_hdr);
                *proto = vlan_hdr->eth_proto;
                if (rte_cpu_to_be_16(ETHER_TYPE_VLAN) == *proto) {
                        vlan_hdr = vlan_hdr + 1;
                        *proto = vlan_hdr->eth_proto;
                        vlan_offset += sizeof(struct vlan_hdr);
                }
        }
        return vlan_offset;
}


/* This function is streaming TCP headers (with options) on ZMQ sockets */
static int
send_tcpoptions(struct tcp_hdr *tcp_hdr)
{
	uint32_t length = (tcp_hdr->data_off & 0xf0) >> 2;
	uint8_t alldata[length];

	memcpy(alldata, tcp_hdr, length); 
	send_header_zmq_ipv4(alldata, length);	

	//printf("len: %u\n", length);
	//fflush(stdout);
	return 0;
}

static void
track_latency(struct rte_mbuf *m, uint64_t *ipv4_timestamp_syn, uint64_t *ipv4_timestamp_synack)
{
	struct ether_hdr *eth_hdr;
	struct tcp_hdr *tcp_hdr = NULL;
	struct ipv4_hdr* ipv4_hdr;
	uint16_t offset = 0;
	enum { URG_FLAG = 0x20, ACK_FLAG = 0x10, PSH_FLAG = 0x08, RST_FLAG = 0x04, SYN_FLAG = 0x02, FIN_FLAG = 0x01 };
	uint64_t key;

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);

	//VLAN tagged frame
	if (eth_hdr->ether_type == rte_cpu_to_be_16(ETHER_TYPE_VLAN)){
        	offset = get_vlan_offset(eth_hdr, &eth_hdr->ether_type);
	}
	
	// IPv4	
	ipv4_hdr = rte_pktmbuf_mtod_offset(m, struct ipv4_hdr *, sizeof(struct ether_hdr)+offset);
	if (ipv4_hdr->next_proto_id == IPPROTO_TCP){
		tcp_hdr = rte_pktmbuf_mtod_offset(m, struct tcp_hdr *, 
			sizeof(struct ipv4_hdr) + sizeof(struct ether_hdr) + offset);
		switch (tcp_hdr->tcp_flags){ 
			case SYN_FLAG:
				key = (long long) m->hash.rss << 32 | rte_be_to_cpu_32(tcp_hdr->sent_seq);
				track_latency_syn_v4( key, ipv4_timestamp_syn);
				break;
			case SYN_FLAG | ACK_FLAG:
				key = (long long) m->hash.rss << 32 | (rte_be_to_cpu_32(tcp_hdr->recv_ack)- 1);
				track_latency_synack_v4( key, ipv4_timestamp_synack);
				break;	
			case ACK_FLAG:
				key = (long long) m->hash.rss << 32 | (rte_be_to_cpu_32(tcp_hdr->sent_seq) - 1 );
				track_latency_ack_v4( key,
					rte_be_to_cpu_32(ipv4_hdr->dst_addr),
					rte_be_to_cpu_32(ipv4_hdr->src_addr),
					ipv4_timestamp_syn,
					ipv4_timestamp_synack);
		}	
	}
}


/* Send the burst of packets on an output interface */
static int
dpdklatency_send_burst(struct lcore_conf *qconf, unsigned n, uint8_t port)
{
	struct rte_mbuf **m_table;
	unsigned ret;
	unsigned queueid = 0;
	unsigned lcore_id = rte_lcore_id();

	m_table = (struct rte_mbuf **)qconf->tx_mbufs[port].m_table;

	// TODO: change here is more than one TX queue per port is required
	ret = rte_eth_tx_burst(port, (uint16_t) queueid, m_table, (uint16_t) n);
	lcore_statistics[lcore_id].tx += ret;
	if (unlikely(ret < n)) {
		lcore_statistics[lcore_id].dropped += (n - ret);
		do {
			rte_pktmbuf_free(m_table[ret]);
		} while (++ret < n);
	}

	return 0;
}

/* Enqueue packets for TX and prepare them to be sent */
static int
dpdklatency_send_packet(struct rte_mbuf *m, uint8_t port)
{
	unsigned lcore_id, len;
	struct lcore_conf *qconf;

	lcore_id = rte_lcore_id();

	qconf = &lcore_conf[lcore_id];
	len = qconf->tx_mbufs[port].len;
	qconf->tx_mbufs[port].m_table[len] = m;
	len++;

	/* enough pkts to be sent */
	if (unlikely(len == MAX_PKT_BURST)) {
		dpdklatency_send_burst(qconf, MAX_PKT_BURST, port);
		len = 0;
	}

	qconf->tx_mbufs[port].len = len;
	return 0;
}

/* Print out statistics on packets dropped */
static void
print_stats(void)
{
	uint64_t total_packets_dropped, total_packets_tx, total_packets_rx;
	unsigned lcore_id;

	// TODO: dopped TX packets are not counted properly
	total_packets_dropped = 0;
	total_packets_tx = 0;
	total_packets_rx = 0;

	const char clr[] = { 27, '[', '2', 'J', '\0' };
	const char topLeft[] = { 27, '[', '1', ';', '1', 'H','\0' };

	/* Clear screen and move to top left */
	printf("%s%s", clr, topLeft);

	printf("\nLcore statistics ====================================");

	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		printf("\nStatistics for lcore %u ------------------------------"
			   "\nPackets sent: %24"PRIu64
			   "\nPackets received: %20"PRIu64
			   "\nPackets dropped: %21"PRIu64,
			   lcore_id,
			   lcore_statistics[lcore_id].tx,
			   lcore_statistics[lcore_id].rx,
			   lcore_statistics[lcore_id].dropped);

		total_packets_dropped += lcore_statistics[lcore_id].dropped;
		total_packets_tx += lcore_statistics[lcore_id].tx;
		total_packets_rx += lcore_statistics[lcore_id].rx;
	}
	printf("\nAggregate statistics ==============================="
		   "\nTotal packets sent: %18"PRIu64
		   "\nTotal packets received: %14"PRIu64
		   "\nTotal packets dropped: %15"PRIu64,
		   total_packets_tx,
		   total_packets_rx,
		   total_packets_dropped);
	printf("\n====================================================\n");
}

static void
init_zmq_for_lcore(unsigned lcore_id){
	void *context = zmq_ctx_new ();
	void *requester = zmq_socket (context, ZMQ_PUB);
	void *requester_headers = zmq_socket (context, ZMQ_PUB);
	char hostname[28]; 
	int rc;

	if (lcore_id > 99){
		rte_exit(EXIT_FAILURE, "Lcore %u is out of range", lcore_id);
	}

	//Starting port: 5550, 5551, 5552, etc.
	if (publishto == NULL){
		snprintf(hostname, 21, "tcp://127.0.0.1:55%.2d", lcore_id);	
		printf("Setting up ZMQ from lcore %u on socket %s %lu \n", lcore_id, hostname, sizeof(hostname));
		rc = zmq_bind (requester, hostname);
	} else {
		snprintf(hostname, 28, "tcp://%s:55%.2d", publishto, lcore_id);	
		printf("Connecting ZMQ from lcore %u to publish to socket %s %lu \n", lcore_id, hostname, sizeof(hostname));
		rc = zmq_connect (requester, hostname);
	}
	
	if (rc != 0 || requester == NULL) {
		rte_exit(EXIT_FAILURE, "Unable to create zmq connection on lcore %u . Issue: %s", lcore_id, zmq_strerror (errno));
	}	
	
	lcore_conf[lcore_id].zmq_client = requester;
}



/* stats loop */
static void
dpdklatency_stats_loop(void)
{
	unsigned lcore_id;
	uint64_t prev_tsc, diff_tsc, cur_tsc, timer_tsc;
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;
	
	prev_tsc = 0;
	timer_tsc = 0;

	lcore_id = rte_lcore_id();

	RTE_LOG(INFO, DPDKLATENCY, "entering stats loop on lcore %u\n", lcore_id);

	while (!force_quit) {
		cur_tsc = rte_rdtsc();
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {
			/* if timer is enabled */
			if (timer_period > 0) {
				/* advance the timer */
				timer_tsc += diff_tsc;
				/* if timer has reached its timeout */
				if (unlikely(timer_tsc >= (uint64_t) timer_period)) {
					print_stats();
					/* reset the timer */
					timer_tsc = 0;
				}
			}
			prev_tsc = cur_tsc;
		}
	}
}

/* packet processing loop */
static void
dpdklatency_processing_loop(void)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	unsigned lcore_id;
	unsigned i, j, portid, queueid, nb_rx;
	struct lcore_conf *qconf;
	struct rte_mbuf *m;
	uint64_t prev_tsc, diff_tsc, cur_tsc;
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;
	uint64_t ipv4_timestamp_syn[TIMESTAMP_HASH_ENTRIES] __rte_cache_aligned;
	uint64_t ipv4_timestamp_synack[TIMESTAMP_HASH_ENTRIES] __rte_cache_aligned;

	prev_tsc = 0;

	lcore_id = rte_lcore_id();
	qconf = &lcore_conf[lcore_id];
	
	/* Init ZMQ */
	init_zmq_for_lcore(lcore_id);

	if (qconf->n_rx_queue == 0) {
		RTE_LOG(INFO, DPDKLATENCY, "lcore %u has nothing to do - no RX queue assigned\n", lcore_id);
		return;
	}

	for (i = 0; i < qconf->n_rx_queue; i++) {
		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		RTE_LOG(INFO, DPDKLATENCY, " -- lcoreid=%u portid=%hhu "
			"rxqueueid=%hhu\n", lcore_id, portid, queueid);
	}

	while (!force_quit) {
		cur_tsc = rte_rdtsc();

		/* TX burst queue drain	 */
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {

			for (portid = 0; portid < RTE_MAX_ETHPORTS; portid++) {
				if (qconf->tx_mbufs[portid].len == 0)
					continue;
				dpdklatency_send_burst(&lcore_conf[lcore_id],
						 qconf->tx_mbufs[portid].len,
						 (uint8_t) portid);
		
				qconf->tx_mbufs[portid].len = 0;
			}
			prev_tsc = cur_tsc;
		}
		
		for (i = 0; i < qconf->n_rx_queue; i++) {
			portid = qconf->rx_queue_list[i].port_id;
			queueid = qconf->rx_queue_list[i].queue_id;

			/* Reading from RX queue */
			nb_rx = rte_eth_rx_burst(portid, queueid, pkts_burst, MAX_PKT_BURST);
			//if (nb_rx > 0){
			//	printf("reading from portid %u, queueid %u\n", portid, queueid);
			//}
			lcore_statistics[lcore_id].rx += nb_rx;

			for (j = 0; j < nb_rx; j++) {
				m = pkts_burst[j];
				rte_prefetch0(rte_pktmbuf_mtod(m, void *));

				// Call the latency tracker function for every packet
				track_latency(m, ipv4_timestamp_syn, ipv4_timestamp_synack);

				/* Forward packets if forwarding is enabled */	
				if (forwarding){
					dpdklatency_send_packet(m, (uint8_t) !portid);
				} else {
					// drop it like it's hot
					rte_pktmbuf_free(m);	
				}
			}
		}
	}
}

/* display usage */
static void
dpdklatency_usage(const char *prgname)
{
	printf("%s [EAL options] -- -p PORTMASK [-q NQ] [-T PERIOD] [--config (port, queue, lcore)] [--publishto IP] [--debug] [--forwarding]\n"
	       "  -p PORTMASK: hexadecimal bitmask of ports to configure\n"
	       "  -q NQ: number of queue (=ports) per lcore (default is 1)\n"
	       "  -T PERIOD: statistics will be refreshed each PERIOD seconds (0 to disable, 10 default, 86400 maximum)\n"
	       " --config (port,queue,lcore)[,(port,queue,lcore)]\n"
	       " --publishto IP: publish to a specific IP (where analytics is running). If not specified, this program binds.\n"
	       " --debug: shows captured flows\n"
	       "  --[no-]forwarding: Enable or disable forwarding (disabled by default)\n"
               "      When enabled, the app forwards packets between port 0 and 1:\n",
	       prgname);
}


static int
dpdklatency_parse_ip(const char *q_arg)
{
	int i;
	publishto = q_arg;
	// very simple checks: parameter is not null and it contains a . (e.g., 10.0.0.1)
	if (q_arg == NULL){
		publishto = NULL;
		return -1;
	}
	for (i=0; q_arg[i]; q_arg[i]=='.' ? i++ : *q_arg++);
	if (i != 3){
		publishto = NULL;
		return -1;
	}
	return 0;
}

static int
dpdklatency_parse_config(const char *q_arg)
{
	char s[256];
	const char *p, *p0 = q_arg;
	char *end;
	enum fieldnames {
		FLD_PORT = 0,
		FLD_QUEUE,
		FLD_LCORE,
		_NUM_FLD
	};
	unsigned long int_fld[_NUM_FLD];
	char *str_fld[_NUM_FLD];
	int i;
	unsigned size;

	nb_lcore_params = 0;

	while ((p = strchr(p0,'(')) != NULL) {
		++p;
		if((p0 = strchr(p,')')) == NULL)
			return -1;

		size = p0 - p;
		if(size >= sizeof(s))
			return -1;

		snprintf(s, sizeof(s), "%.*s", size, p);
		if (rte_strsplit(s, sizeof(s), str_fld, _NUM_FLD, ',') !=
								_NUM_FLD)
			return -1;
		for (i = 0; i < _NUM_FLD; i++){
			errno = 0;
			int_fld[i] = strtoul(str_fld[i], &end, 0);
			if (errno != 0 || end == str_fld[i] || int_fld[i] >
									255)
				return -1;
		}
		if (nb_lcore_params >= MAX_LCORE_PARAMS) {
			printf("exceeded max number of lcore params: %hu\n",
				nb_lcore_params);
			return -1;
		}
		lcore_params_array[nb_lcore_params].port_id =
				(uint8_t)int_fld[FLD_PORT];
		lcore_params_array[nb_lcore_params].queue_id =
				(uint8_t)int_fld[FLD_QUEUE];
		lcore_params_array[nb_lcore_params].lcore_id =
				(uint8_t)int_fld[FLD_LCORE];
		++nb_lcore_params;
	}
	lcore_params = lcore_params_array;

	return 0;
}

static int
dpdklatency_parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	/* parse hexadecimal string */
	pm = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	return pm;
}

static int
dpdklatency_parse_timer_period(const char *q_arg)
{
	char *end = NULL;
	int n;

	/* parse number string */
	n = strtol(q_arg, &end, 10);
	if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;
	if (n >= MAX_TIMER_PERIOD)
		return -1;

	return n;
}

/* Parse the argument given in the command line of the application */
static int
dpdklatency_parse_args(int argc, char **argv)
{
	int opt, ret, timer_secs;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];
	static struct option lgopts[] = {
		{ "config", 1, 0, 0},
		{ "publishto", 1, 0, 0},
		{ "debug", no_argument, &debug, 1},
		{ "forwarding", no_argument, &forwarding, 1},
		{ "no-forwarding", no_argument, &forwarding, 0},
		{NULL, 0, 0, 0}
	};

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, "p:q:T:",
				  lgopts, &option_index)) != EOF) {

		switch (opt) {
		/* portmask */
		case 'p':
			dpdklatency_enabled_port_mask = dpdklatency_parse_portmask(optarg);
			if (dpdklatency_enabled_port_mask == 0) {
				printf("invalid portmask\n");
				dpdklatency_usage(prgname);
				return -1;
			}
			break;

		/* timer period */
		case 'T':
			timer_secs = dpdklatency_parse_timer_period(optarg);
			if (timer_secs < 0) {
				printf("invalid timer period\n");
				dpdklatency_usage(prgname);
				return -1;
			}
			timer_period = timer_secs;
			break;

		/* long options */
		case 0:
			if (!strncmp(lgopts[option_index].name, "config", 6)) {
				ret = dpdklatency_parse_config(optarg);
				if (ret) {
					printf("invalid config\n");
					dpdklatency_usage(prgname);
					return -1;
				}
			}
			if (!strncmp(lgopts[option_index].name, "publishto", 9)) {
				ret = dpdklatency_parse_ip(optarg);
				if (ret) {
					printf("invalid publishto\n");
					dpdklatency_usage(prgname);
					return -1;
				}
			}
			break;

		default:
			dpdklatency_usage(prgname);
			return -1;
		}
	}

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind-1;
	optind = 0; /* reset getopt lib */
	return ret;
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void
check_all_ports_link_status(uint8_t port_num, uint32_t port_mask)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint8_t portid, count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		if (force_quit)
			return;
		all_ports_up = 1;
		for (portid = 0; portid < port_num; portid++) {
			if (force_quit)
				return;
			if ((port_mask & (1 << portid)) == 0)
				continue;
			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(portid, &link);
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					printf("Port %d Link Up - speed %u "
						"Mbps - %s\n", (uint8_t)portid,
						(unsigned)link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n",
						(uint8_t)portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == ETH_LINK_DOWN) {
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
			printf("done\n");
		}
	}
}

static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n",
				signum);
		force_quit = true;
	}
}

static void
setup_hash(int lcoreid)
{
	struct rte_hash_parameters ipv4_timestamp_hash_params = {
		.name = NULL,
		.entries = TIMESTAMP_HASH_ENTRIES,
		.key_len = sizeof(uint64_t),
		.hash_func = DEFAULT_HASH_FUNC,
		.hash_func_init_val = 0,
	};

	char s[64];

	/* create ipv4 hash */
	snprintf(s, sizeof(s), "ipv4_timestamp_hash_%d", lcoreid);
	ipv4_timestamp_hash_params.name = s;
	ipv4_timestamp_hash_params.socket_id = 0;
	ipv4_timestamp_lookup_struct[lcoreid] =
		rte_hash_create(&ipv4_timestamp_hash_params);
	if (ipv4_timestamp_lookup_struct[lcoreid] == NULL)
		rte_exit(EXIT_FAILURE, "Unable to create the timestamp hash on "
				"socket %d\n", 0);
}

static int
init_hash(void)
{
	struct lcore_conf *qconf;
	int socketid = 0;
	unsigned lcore_id;

	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;

		if (socketid >= NB_SOCKETS) {
			rte_exit(EXIT_FAILURE, "Socket %d of lcore %u is "
					"out of range %d\n", socketid,
						lcore_id, NB_SOCKETS);
		}
		printf("Setting up hash table for lcore %u, on socket %u\n", lcore_id, socketid);
		setup_hash(lcore_id);

		qconf = &lcore_conf[lcore_id];
		qconf->ipv4_lookup_struct = ipv4_timestamp_lookup_struct[lcore_id];
	}
	return 0;
}


static uint8_t
get_port_n_rx_queues(const uint8_t port)
{
	int queue = -1;
	uint16_t i;

	for (i = 0; i < nb_lcore_params; ++i) {
		if (lcore_params[i].port_id == port &&
				lcore_params[i].queue_id > queue)
			queue = lcore_params[i].queue_id;
	}
	return (uint8_t)(++queue);
}

static int
init_lcore_rx_queues(void)
{
	uint16_t i, nb_rx_queue;
	uint8_t lcore;

	for (i = 0; i < nb_lcore_params; ++i) {
		lcore = lcore_params[i].lcore_id;
		nb_rx_queue = lcore_conf[lcore].n_rx_queue;
		if (nb_rx_queue >= MAX_RX_QUEUE_PER_LCORE) {
			printf("error: too many queues (%u) for lcore: %u\n",
				(unsigned)nb_rx_queue + 1, (unsigned)lcore);
			return -1;
		} else {
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].port_id =
				lcore_params[i].port_id;
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].queue_id =
				lcore_params[i].queue_id;
			lcore_conf[lcore].n_rx_queue++;
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct lcore_conf *qconf;
	int ret;
	uint8_t nb_ports, nb_rx_queue;
	uint8_t nb_ports_available;
	uint8_t portid, queueid, queue;
	char *publish_host;
	unsigned lcore_id;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
	argc -= ret;
	argv += ret;

	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* parse application arguments (after the EAL ones) */
	ret = dpdklatency_parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid DPDKLATENCY arguments\n");

	ret = init_lcore_rx_queues();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init_lcore_rx_queues failed\n");


	/* convert to number of cycles */
	timer_period *= rte_get_timer_hz();

	/* create the mbuf pool */
	dpdklatency_pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NB_MBUF,
		MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
		rte_socket_id());
	if (dpdklatency_pktmbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

	nb_ports = rte_eth_dev_count();
	if (nb_ports == 0)
		rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");

	qconf = NULL;
	
	nb_ports_available = nb_ports;

	/* Initialise each port */
	for (portid = 0; portid < nb_ports; portid++) {
		/* skip ports that are not enabled */
		if ((dpdklatency_enabled_port_mask & (1 << portid)) == 0) {
			printf("Skipping disabled port %u\n", (unsigned) portid);
			nb_ports_available--;
			continue;
		}

		/* init port */
		printf("Initializing port %d ... \n", portid );
		fflush(stdout);

		/* init port */
		nb_rx_queue = get_port_n_rx_queues(portid);
		ret = rte_eth_dev_configure(portid, nb_rx_queue, 1, &port_conf);
		if (ret < 0) 
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
				  ret, (unsigned) portid);
		
		/* init one TX queue (queue id is 0) on each port */
		ret = rte_eth_tx_queue_setup(portid, 0, nb_txd,
				rte_eth_dev_socket_id(portid),
				NULL);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
				ret, (unsigned) portid);
	

		/* Initialize TX buffers */
		tx_buffer[portid] = rte_zmalloc_socket("tx_buffer",
				RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST), 0,
				rte_eth_dev_socket_id(portid));
		if (tx_buffer[portid] == NULL)
			rte_exit(EXIT_FAILURE, "Cannot allocate buffer for tx on port %u\n",
					(unsigned) portid);

		rte_eth_tx_buffer_init(tx_buffer[portid], MAX_PKT_BURST);

	}

	// TODO: maybe use separate mbuf pool per lcore?	
	/* init hash */
	ret = init_hash();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init_hash failed\n");

	/* Init RX queues */
	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;
		qconf = &lcore_conf[lcore_id];	
		for(queue = 0; queue < qconf->n_rx_queue; ++queue) {
			portid = qconf->rx_queue_list[queue].port_id;
			queueid = qconf->rx_queue_list[queue].queue_id;

			printf("setting up rx queue on port %u, queue %u\n", portid, queueid);	
			ret = rte_eth_rx_queue_setup(portid, queueid, nb_rxd,
						     rte_eth_dev_socket_id(portid),
					     NULL,
					     dpdklatency_pktmbuf_pool);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
					  ret, (unsigned) portid);
		}
	}

	for (portid = 0; portid < nb_ports; portid++) {
		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
				  ret, (unsigned) portid);

		printf("done: \n");

		rte_eth_promiscuous_enable(portid);

		/* initialize port stats */
		memset(&lcore_statistics, 0, sizeof(lcore_statistics));
	}

	if (!nb_ports_available) {
		rte_exit(EXIT_FAILURE,
			"All available ports are disabled. Please set portmask.\n");
	}

	check_all_ports_link_status(nb_ports, dpdklatency_enabled_port_mask);

	ret = 0;

	/* launch stats on core 0 */
	rte_eal_remote_launch((lcore_function_t *) dpdklatency_stats_loop, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		rte_eal_remote_launch((lcore_function_t *) dpdklatency_processing_loop, NULL, lcore_id);
	}

	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0) {
                        return -1;
		}
		//TODO: free zmq things
	}

	for (portid = 0; portid < nb_ports; portid++) {
		if ((dpdklatency_enabled_port_mask & (1 << portid)) == 0)
			continue;
		printf("Closing port %d...", portid);
		rte_eth_dev_stop(portid);
		rte_eth_dev_close(portid);
		printf(" Done\n");
	}

	printf("Bye...\n");

	return ret;
}
