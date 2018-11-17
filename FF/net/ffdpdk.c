/**
Copyright (c) 2018 Simon Zolin
*/

#include <FF/net/dpdk.h>
#include <FF/string.h>
#include <FFOS/cpu.h>

#include <rte_launch.h>


enum {
	LOG_ERR = 1,
	LOG_INFO,
	LOG_DBG,
};

#define log_chk(c, lev)  ((c)->loglevel >= lev)
#ifdef _DEBUG
#define dbglog_chk(c, lev)  log_chk(c, lev)
#else
#define dbglog_chk(c, lev)  (0)
#endif

#define dbglog(c, fmt, ...) \
	if (dbglog_chk(c, LOG_DBG)) \
		(c)->logger(LOG_DBG, "DBG: " fmt "\n", __VA_ARGS__)

#define infolog(c, fmt, ...) \
	if (log_chk(c, LOG_INFO)) \
		(c)->logger(LOG_INFO, "INF: " fmt "\n", __VA_ARGS__)

#define errlog(c, fmt, ...) \
	if (log_chk(c, LOG_ERR)) \
		(c)->logger(LOG_ERR, "ERR: " fmt "\n", __VA_ARGS__)


typedef struct dpdk {
	uint loglevel;
	void (*logger)(uint level, const char *fmt, ...);
	int kni_init :1;
} dpdk;

static dpdk *gdpdk;

int ffdpdk_init(int *argc, char ***argv, uint loglevel, ffdpdk_logger_t logger)
{
	int r;
	if (0 > (r = rte_eal_init(*argc, *argv)))
		return r;

	if (NULL == (gdpdk = ffdpdk_callocT(1, dpdk)))
		return -1;
	gdpdk->loglevel = loglevel;
	gdpdk->logger = logger;

	dbglog(gdpdk, "rte_eal_init() ok", 0);
	*argc -= r;
	*argv += r;
	return 0;
}


int ffdpdk_core_next(int i)
{
	for (i = i + 1;  (uint)i != RTE_MAX_LCORE;  i++) {
		if (rte_lcore_is_enabled(i)) {
			return i;
		}
	}
	return -1;
}

int ffdpdk_cores_call(int (*func)(void *), void *arg, uint flags)
{
	if (0 != rte_eal_mp_remote_launch(func, arg, flags)) {
		errlog(gdpdk, "rte_eal_mp_remote_launch()", 0);
		return -1;
	}
	return 0;
}


int ffdpdk_clk_expired(ffdpdk_clock *c)
{
	if (c->spin-- != 0)
		return 0;
	c->spin = c->spin_resol;

	uint64 cur = ffcpu_rdtsc();
	if (cur > c->nextfire) {
		c->nextfire = cur + c->resol;
		return 1;
	}
	return 0;
}


static uint mpool_cnt;

ffdpdk_mpool* ffdpdk_mpool_new(uint n)
{
	void *mpool;
	char name[32];
	ffs_fmt2(name, sizeof(name), "mbuf_pool%u", mpool_cnt++);
	if (NULL == (mpool = rte_pktmbuf_pool_create(name, n, 256, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id()))) {
		errlog(gdpdk, "rte_pktmbuf_pool_create(): (%d) %s", rte_errno, strerror(rte_errno));
		return NULL;
	}
	dbglog(gdpdk, "rte_pktmbuf_pool_create(%u) ok", n);
	return mpool;
}


int ffdpdk_port_cnt(void)
{
	int r;
	r = rte_eth_dev_count();
	dbglog(gdpdk, "rte_eth_dev_count(): %u", r);
	return r;
}

static struct rte_eth_conf vmdq_conf_default = {
	.rxmode = {
		.mq_mode        = ETH_MQ_RX_VMDQ_ONLY,
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 0, /**< IP checksum offload disabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		/*
		 * It is necessary for 1G NIC such as I350,
		 * this fixes bug of ipv4 forwarding in guest can't
		 * forward pakets from one virtio dev to another virtio dev.
		 */
		.hw_vlan_strip  = 1, /**< VLAN strip enabled. */
		.jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 1, /**< CRC stripped by hardware */
	},

	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
	.rx_adv_conf = {
		/*
		 * should be overridden separately in code with
		 * appropriate values
		 */
		.vmdq_rx_conf = {
			.nb_queue_pools = ETH_8_POOLS,
			.enable_default_pool = 0,
			.default_pool = 0,
			.nb_pool_maps = 0,
			.pool_map = {{0, 0},},
		},
	},
};

static inline int
get_eth_conf(struct rte_eth_conf *eth_conf, uint32_t num_devices)
{
	struct rte_eth_vmdq_rx_conf conf;
	struct rte_eth_vmdq_rx_conf *def_conf =
		&vmdq_conf_default.rx_adv_conf.vmdq_rx_conf;

	memset(&conf, 0, sizeof(conf));
	conf.nb_queue_pools = (enum rte_eth_nb_pools)num_devices;
	conf.nb_pool_maps = num_devices;
	conf.enable_loop_back = def_conf->enable_loop_back;
	conf.rx_mode = def_conf->rx_mode;

	for (uint i = 0; i < conf.nb_pool_maps; i++) {
		conf.pool_map[i].vlan_id = 1000 + i;
		conf.pool_map[i].pools = (1UL << i);
	}

	rte_memcpy(eth_conf, &vmdq_conf_default, sizeof(*eth_conf));
	rte_memcpy(&eth_conf->rx_adv_conf.vmdq_rx_conf, &conf, sizeof(eth_conf->rx_adv_conf.vmdq_rx_conf));
	return 0;
}

int ffdpdk_port_init(ffdpdk_port *p, uint id)
{
	rte_eth_dev_info_get(id, &p->info);

	// p->conf.rx_adv_conf.rss_conf.rss_hf = ETH_RSS_PROTO_MASK;
	p->conf.rxmode.hw_ip_checksum = 1;

	get_eth_conf(&p->conf, p->info.max_vmdq_pools);
	p->info.default_txconf.txq_flags &= ~ETH_TXQ_FLAGS_NOVLANOFFL;
	p->info.default_rxconf.rx_drop_en = 1;

	p->rx_ndesc = 512;
	p->tx_ndesc = 512;
	p->id = id;
	return 0;
}

static int port_rx_setup(ffdpdk_port *p, ffdpdk_mpool *mpool, uint nqueues)
{
	for (uint i = 0;  i != nqueues;  i++) {

		if (0 != rte_eth_rx_queue_setup(p->id, i, p->rx_ndesc, p->skt_id, &p->info.default_rxconf, mpool)) {
			errlog(gdpdk, "port%u: rte_eth_rx_queue_setup()  queue:%u", p->id, i);
			return -1;
		}
	}

	dbglog(gdpdk, "port%u: rte_eth_rx_queue_setup() ok", p->id);
	return 0;
}

static int port_tx_setup(ffdpdk_port *p, uint nqueues)
{
	for (uint i = 0;  i != nqueues;  i++) {

		if (0 != rte_eth_tx_queue_setup(p->id, i, p->tx_ndesc, p->skt_id, &p->info.default_txconf)) {
			errlog(gdpdk, "port%u: rte_eth_tx_queue_setup()  queue:%u", p->id, i);
			return -1;
		}
	}

	dbglog(gdpdk, "port%u: rte_eth_tx_queue_setup() ok", p->id);
	return 0;
}

int ffdpdk_port_open(ffdpdk_port *p, uint rx_queues, uint tx_queues, ffdpdk_mpool *mpool)
{
	int r;

	if (0 != rte_eth_dev_configure(p->id, rx_queues, tx_queues, &p->conf)) {
		errlog(gdpdk, "port%u: rte_eth_dev_configure()", p->id);
		return -1;
	}
	p->open = 1;
	dbglog(gdpdk, "port%u: rte_eth_dev_configure(%u, %u) ok", p->id, rx_queues, tx_queues);

	if (p->mtu != 0) {
		if (0 != (r = rte_eth_dev_set_mtu(p->id, p->mtu))) {
			errlog(gdpdk, "port%u: rte_eth_dev_set_mtu(): (%d) %s", p->id, -r, strerror(-r));
			return -1;
		}
		dbglog(gdpdk, "port%u: rte_eth_dev_set_mtu(%u)", p->id, p->mtu);
	}

	p->skt_id = rte_eth_dev_socket_id(p->id);

	if (p->promisc)
		rte_eth_promiscuous_enable(p->id);

	if (0 != port_rx_setup(p, mpool, rx_queues))
		return -1;

	if (0 != port_tx_setup(p, tx_queues))
		return -1;

	rte_eth_macaddr_get(p->id, (void*)&p->addr);
	infolog(gdpdk, "Opened port%u:  %s  MAC: %h  RX/TX queues: %u(%u)/%u(%u)  RX/TX desc: %u(%u)/%u(%u)"
		, p->id, ffdpdk_port_name(p), &p->addr
		, rx_queues, ffdpdk_port_rx_lim_queues(p), tx_queues, ffdpdk_port_tx_lim_queues(p)
		, p->rx_ndesc, ffdpdk_port_rx_lim_desc(p), p->tx_ndesc, ffdpdk_port_tx_lim_desc(p));
	return 0;
}

void ffdpdk_port_close(ffdpdk_port *p)
{
	ffdpdk_port_stop(p);

	if (!p->open)
		return;
	rte_eth_dev_close(p->id);
	p->open = 0;
	dbglog(gdpdk, "port%u: rte_eth_dev_close() ok", p->id);
}

void ffdpdk_queue_close(ffdpdk_queue *q)
{
	rte_free(q->txbuf);
	q->txbuf = NULL;
}

int ffdpdk_queue_initrx(ffdpdk_queue *q, ffdpdk_port *p, uint que)
{
	q->p = p;
	q->que = que;
	return 0;
}

int ffdpdk_queue_inittx(ffdpdk_queue *q, ffdpdk_port *p, uint que, uint burst_size)
{
	q->p = p;
	q->que = que;

	if (NULL == (q->txbuf = rte_zmalloc_socket("tx_buffer", sizeof(void*) * burst_size, 0, p->skt_id))) {
		errlog(gdpdk, "port%u: rte_zmalloc_socket()", p->id);
		return -1;
	}
	q->txsize = burst_size;
	return 0;
}

int ffdpdk_port_read(ffdpdk_queue *q, ffdpdk_mbuf **pkts, uint npkts)
{
	ffdpdk_port *p = q->p;
	uint r;
	r = rte_eth_rx_burst(p->id, q->que, pkts, npkts);
	if (r != 0) {
		dbglog(gdpdk, "port%u: que#%u rte_eth_rx_burst(): %u", p->id, q->que, r);
		q->stat.recvd += r;
	}
	return r;
}

void ffdpdk_port_pktdrop(ffdpdk_queue *q, ffdpdk_mbuf *pkt)
{
	rte_pktmbuf_free(pkt);
	q->stat.dropped++;
}

int ffdpdk_port_write(ffdpdk_queue *q, ffdpdk_mbuf *pkt)
{
	q->txbuf[q->txlen++] = pkt;
	if (q->txlen != q->txsize)
		return 0;

	return ffdpdk_port_flush(q);
}

uint ffdpdk_port_flush(ffdpdk_queue *q)
{
	ffdpdk_port *p = q->p;
	uint r;

	if (q->txlen == 0)
		return 0;

	r = rte_eth_tx_burst(p->id, q->que, q->txbuf, q->txlen);
	q->stat.sent += r;
	dbglog(gdpdk, "port%u: que#%u rte_eth_tx_burst(): %u/%u", p->id, q->que, r, q->txlen);

	if (r != q->txlen) {
		for (uint i = r;  i != q->txlen;  i++) {
			rte_pktmbuf_free(q->txbuf[i]);
		}
		q->stat.dropped += q->txlen - r;
	}

	q->txlen = 0;
	return r;
}

void ffdpdk_port_lacp_process(ffdpdk_queue *q)
{
	rte_eth_tx_burst(q->p->id, q->que, NULL, 0);
}

int ffdpdk_port_start(ffdpdk_port *p)
{
	FF_ASSERT(p->open);
	if (0 != rte_eth_dev_start(p->id)) {
		errlog(gdpdk, "port%u: rte_eth_dev_start()", p->id);
		return -1;
	}
	p->started = 1;
	dbglog(gdpdk, "port%u: rte_eth_dev_start()", p->id);
	return 0;
}

void ffdpdk_port_stop(ffdpdk_port *p)
{
	if (!p->started)
		return;
	rte_eth_dev_stop(p->id);
	p->started = 0;
	dbglog(gdpdk, "port%u: rte_eth_dev_stop()", p->id);
}

int ffdpdk_port_state(ffdpdk_port *p)
{
	struct rte_eth_link link = {};
	rte_eth_link_get_nowait(p->id, &link);
	return link.link_status;
}

int ffdpdk_ports_online(ffdpdk_port *ports, uint nports)
{
	for (uint i = 0;  i != nports;  i++) {

		ffdpdk_port *p = &ports[i];
		if (!p->open)
			continue;
		if (!ffdpdk_port_state(p)) {
			dbglog(gdpdk, "port%u DOWN", i);
			return 0;
		}
		dbglog(gdpdk, "port%u UP", i);
	}

	return 1;
}


static int kni_change_mtu(uint16_t port_id, unsigned new_mtu)
{
	errlog(gdpdk, "KNI@port%u: MTU change isn't supported", port_id);
	return -1;
}

static int kni_updown(uint16_t port_id, uint8_t up)
{
	if (up) {
		dbglog(gdpdk, "KNI@port%u: UP", port_id);

	} else {
		dbglog(gdpdk, "KNI@port%u: DOWN", port_id);
	}

	return 0;
}

void ffdpdk_kni_init(uint n)
{
	rte_kni_init(n);
	gdpdk->kni_init = 1;
}

int ffdpdk_kni_open(ffdpdk_kni *k, ffdpdk_port *p, struct rte_mempool *mpool)
{
	if (ffdpdk_port_isbond(p)) {
		errlog(gdpdk, "port%u: KNI can only be associated to a real port", p->id);
		return -1;
	}

	struct rte_kni_conf conf;
	ffmem_zero(&conf, sizeof(struct rte_kni_conf));
	ffs_fmt2(conf.name, sizeof(conf.name), "kni%u", p->id);
	strcpy(k->name, conf.name);
	conf.group_id = p->id;
	conf.mbuf_size = 2048;
	conf.addr = p->info.pci_dev->addr;
	conf.id = p->info.pci_dev->id;

	struct rte_kni_ops ops = {};
	ops.port_id = p->id;
	ops.change_mtu = &kni_change_mtu;
	ops.config_network_if = &kni_updown;

	if (NULL == (k->kni = rte_kni_alloc(mpool, &conf, &ops))) {
		errlog(gdpdk, "port%u: rte_kni_alloc()", p->id);
		return -1;
	}
	dbglog(gdpdk, "port%u: rte_kni_alloc() ok", p->id);

	return 0;
}

void ffdpdk_kni_close(ffdpdk_kni *k)
{
	if (k->kni != NULL) {
		int r;
		if (0 != (r = rte_kni_release(k->kni))) {
			errlog(gdpdk, "rte_kni_release()", 0);
			return;
		}
		k->kni = NULL;
		dbglog(gdpdk, "rte_kni_release() ok", 0);
	}

	//rte_kni_close()
}

int ffdpdk_kni_read(ffdpdk_kni *k, ffdpdk_mbuf **pkts, uint npkts)
{
	uint r;
	r = rte_kni_rx_burst(k->kni, pkts, npkts);
	if (r != 0) {
		dbglog(gdpdk, "kni %s: rte_kni_rx_burst(): %u", k->name, r);
		k->stat.recvd += r;
	}
	return r;
}

void ffdpdk_kni_handle_events(ffdpdk_kni *k)
{
	int r;
	if (0 != (r = rte_kni_handle_request(k->kni)))
		errlog(gdpdk, "rte_kni_handle_request()", 0);
}

int ffdpdk_kni_queue_inittx(ffdpdk_kni *k, ffdpdk_kni_queue *q, uint burst_size)
{
	q->k = k;

	if (NULL == (q->txbuf = rte_zmalloc_socket("tx_buffer", sizeof(void*) * burst_size, 0, -1))) {
		errlog(gdpdk, "rte_zmalloc_socket()", 0);
		return -1;
	}
	q->txsize = 0;
	return 0;
}

void ffdpdk_kni_queue_close(ffdpdk_kni_queue *q)
{
	rte_free(q->txbuf);
	q->txbuf = NULL;
}

int ffdpdk_kni_write(ffdpdk_kni_queue *q, ffdpdk_mbuf *pkt)
{
	q->txbuf[q->txlen++] = pkt;
	if (q->txlen != q->txsize)
		return 0;

	return ffdpdk_kni_flush(q);
}

int ffdpdk_kni_flush(ffdpdk_kni_queue *q)
{
	if (q->txlen == 0)
		return 0;

	int r;
	r = rte_kni_tx_burst(q->k->kni, q->txbuf, q->txlen);
	q->k->stat.sent += r;
	dbglog(gdpdk, "kni %s: rte_kni_tx_burst(): %u/%u", q->k->name, r, 1);

	if ((uint)r != q->txlen) {
		for (uint i = r;  i != q->txlen;  i++) {
			rte_pktmbuf_free(q->txbuf[i]);
		}
		q->k->stat.dropped += q->txlen - r;
	}

	q->txlen = 0;
	return r;
}


int ffdpdk_vhost_open(ffdpdk_vhost *vh, const char *path, const struct vhost_device_ops *ops, ffdpdk_mpool *mpool, uint flags)
{
	if (NULL == (vh->path = ffsz_alcopyz(path)))
		goto err;

	if (0 != rte_vhost_driver_register(path, flags)) {
		errlog(gdpdk, "vhost: rte_vhost_driver_register()", 0);
		goto err;
	}
	vh->reg = 1;

	if (0 != rte_vhost_driver_callback_register(path, ops)) {
		errlog(gdpdk, "vhost: rte_vhost_driver_callback_register()", 0);
		goto err;
	}

	vh->mpool = mpool;
	return 0;

err:
	ffdpdk_vhost_destroy(vh);
	return -1;
}

void ffdpdk_vhost_destroy(ffdpdk_vhost *vh)
{
	if (vh->reg)
		rte_vhost_driver_unregister(vh->path);
	ffmem_safefree(vh->path);
}

int ffdpdk_vhost_start(ffdpdk_vhost *vh)
{
	if (0 != rte_vhost_driver_start(vh->path)) {
		errlog(gdpdk, "vhost: rte_vhost_driver_start()", 0);
		return -1;
	}
	return 0;
}

int ffdpdk_vhost_enable(ffdpdk_vhost *vh, uint id, uint nqueues)
{
	vh->id = id;
	for (uint i = 0;  i != nqueues;  i++) {
		rte_vhost_enable_guest_notification(vh->id, i, 0);
	}
	vh->active = 1;
	return 0;
}

int ffdpdk_vhost_write(ffdpdk_vhost *vh, uint que, ffdpdk_mbuf **pkts, uint cnt)
{
	FF_ASSERT(vh->active);
	int r;
	r = rte_vhost_enqueue_burst(vh->id, que, pkts, cnt);

	if (r != 0) {
		dbglog(gdpdk, "vhost%u: rte_vhost_enqueue_burst(): %u/%u"
			, vh->id, r, cnt);
	}
	for (uint i = r;  i != cnt;  i++) {
		rte_pktmbuf_free(pkts[i]);
	}
	return r;
}

int ffdpdk_vhost_read(ffdpdk_vhost *vh, uint que, ffdpdk_mbuf **pkts, uint cnt)
{
	FF_ASSERT(vh->active);
	int r;
	r = rte_vhost_dequeue_burst(vh->id, que, vh->mpool, pkts, cnt);
	if (r != 0) {
		dbglog(gdpdk, "vhost%u: rte_vhost_dequeue_burst(): %u"
			, vh->id, r);
	}
	return r;
}
