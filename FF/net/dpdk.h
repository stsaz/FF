/** Intel DPDK wrapper.
Copyright (c) 2018 Simon Zolin
*/

#pragma once

#include <FF/net/proto.h>

#include <rte_malloc.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_kni.h>
#include <rte_vhost.h>
#include <rte_bus_pci.h>


typedef void (*ffdpdk_logger_t)(uint level, const char *fmt, ...);

/** Initialize.
Must be called once per process on the main thread. */
FF_EXTN int ffdpdk_init(int *argc, char ***argv, uint loglevel, ffdpdk_logger_t logger);


// Memory

#define ffdpdk_callocT(n, T) \
	((T*)ffdpdk_calloc(n, sizeof(T)))

static inline void* ffdpdk_calloc(size_t cnt, size_t sz)
{
	return rte_zmalloc("", cnt * sz, 0);
}

#define ffdpdk_allocT(n, T) \
	((T*)ffdpdk_alloc((n) * sizeof(T)))

static inline void* ffdpdk_alloc(size_t sz)
{
	return rte_malloc("", sz, 0);
}

#define ffdpdk_free(ptr)  rte_free(ptr)

typedef struct rte_mempool ffdpdk_mpool;

/** Create a new memory pool.
'n': number of buffers */
FF_EXTN ffdpdk_mpool* ffdpdk_mpool_new(uint n);

typedef struct rte_mbuf ffdpdk_mbuf;


typedef struct ffdpdk_clock {
	uint64 resol;
	uint64 nextfire;
	uint spin;
	uint spin_resol;
} ffdpdk_clock;

/** Initialize CPU timer.
'ts': timer interval */
static inline void ffdpdk_clk_init(ffdpdk_clock *clk, uint64 ts, uint spin)
{
	clk->resol = ts;
	clk->spin_resol = clk->spin = spin;
}

/** Get clock value from microseconds. */
static inline uint64 ffdpdk_clk_from_us(uint64 us)
{
	return (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * us;
}

/** Get clock value from milliseconds. */
#define ffdpdk_clk_from_ms(ms)  ffdpdk_clk_from_us(ms * 1000)

/** Return 1 if timer value has expired.  Restart the timer. */
FF_EXTN int ffdpdk_clk_expired(ffdpdk_clock *c);


/** Get the number of active CPU cores that this process is allowed to use. */
static inline uint ffdpdk_cores(void)
{
	return rte_lcore_count();
}

/** Get ID of the next CPU.
'i': -1: get the first CPU ID.
Return -1 if no more. */
FF_EXTN int ffdpdk_core_next(int i);

/** Walk through each CPU. */
#define FFDPDK_CORE_FOREACH(i) \
	for ((i) = ffdpdk_core_next(-1);  (int)(i) >= 0;  (i) = ffdpdk_core_next(i))

/** Call function on all CPUs.
'flags': CALL_MASTER: call the function on master CPU. */
FF_EXTN int ffdpdk_cores_call(int (*func)(void *), void *arg, uint flags);

/** Wait until all CPUs finish their jobs. */
static inline void ffdpdk_cores_wait(void)
{
	int i;
	RTE_LCORE_FOREACH_SLAVE(i) {
		rte_eal_wait_lcore(i);
	}
}

/** Get ID of the current CPU. */
#define ffdpdk_core()  rte_lcore_id()


typedef struct ffdpdk_port {
	uint id;
	struct rte_eth_conf conf;
	ffeth addr;
	struct rte_eth_dev_info info;
	int skt_id;
	uint mtu;
	uint rx_ndesc;
	uint tx_ndesc;

	uint open :1;
	uint started :1;
	uint promisc :1;
} ffdpdk_port;

typedef struct ffdpdk_stat {
	uint64 recvd;
	uint64 sent;
	uint64 dropped;
} ffdpdk_stat;

typedef struct ffdpdk_queue {
	ffdpdk_port *p;
	uint que;

	ffdpdk_mbuf **txbuf;
	uint txlen;
	uint txsize;

	ffdpdk_stat stat;
} ffdpdk_queue;

/** Get the number of configured Ethernet devices. */
FF_EXTN int ffdpdk_port_cnt(void);

/** Get device PCI name. */
#define ffdpdk_port_pciname(p)  (((p)->info.pci_dev != NULL) ? (p)->info.pci_dev->name : "unknown")

/** Return 1 if a bonding device. */
#define ffdpdk_port_isbond(p)  ((p)->info.pci_dev == NULL && !strcmp((p)->info.driver_name, "net_bonding"))

/** Get device name. */
#define ffdpdk_port_name(p)  (ffdpdk_port_isbond(p) ? "bonding" : ffdpdk_port_pciname(p))

#define ffdpdk_port_rx_lim_queues(p)  ((p)->info.max_rx_queues)
#define ffdpdk_port_tx_lim_queues(p)  ((p)->info.max_tx_queues)
#define ffdpdk_port_rx_lim_desc(p)  ((p)->info.rx_desc_lim.nb_max)
#define ffdpdk_port_tx_lim_desc(p)  ((p)->info.tx_desc_lim.nb_max)

/** Initialize device.
'id': 0 .. ffdpdk_port_cnt() */
FF_EXTN int ffdpdk_port_init(ffdpdk_port *p, uint id);

/** Open device, configure RX/TX queues.
User sets parameters in 'ffdpdk_port.conf' before calling this function. */
FF_EXTN int ffdpdk_port_open(ffdpdk_port *p, uint rx_queues, uint tx_queues, ffdpdk_mpool *mpool);

/** Close device. */
FF_EXTN void ffdpdk_port_close(ffdpdk_port *p);

/** Start device. */
FF_EXTN int ffdpdk_port_start(ffdpdk_port *p);

/** Stop device. */
FF_EXTN void ffdpdk_port_stop(ffdpdk_port *p);

/** Get link status.
Return 0 if link is down;  1 if up. */
FF_EXTN int ffdpdk_port_state(ffdpdk_port *p);

/** Get link status of all devices.
Return 0 if one link is down;  1 if all are up. */
FF_EXTN int ffdpdk_ports_online(ffdpdk_port *ports, uint nports);

typedef struct rte_eth_stats ffdpdk_portstats;

/** Get device statistics info. */
#define ffdpdk_port_stat_get(p, stat)  rte_eth_stats_get((p)->id, stat)

/** Reset device statistics info. */
#define ffdpdk_port_stat_reset(p)  rte_eth_stats_reset((p)->id)


/** Assign RX queue to device.
'que': queue index, up to ffdpdk_port_open(rx_queues) */
FF_EXTN int ffdpdk_queue_initrx(ffdpdk_queue *q, ffdpdk_port *p, uint que);

/** Assign TX queue to device.
'que': queue index, up to ffdpdk_port_open(tx_queues) */
FF_EXTN int ffdpdk_queue_inittx(ffdpdk_queue *q, ffdpdk_port *p, uint que, uint burst_size);

/** Close queue. */
FF_EXTN void ffdpdk_queue_close(ffdpdk_queue *q);

/** Read from RX queue.
'npkts': max packets to read.
Return packets read. */
FF_EXTN int ffdpdk_port_read(ffdpdk_queue *q, ffdpdk_mbuf **pkts, uint npkts);

FF_EXTN void ffdpdk_port_pktdrop(ffdpdk_queue *q, ffdpdk_mbuf *pkt);

/** Add a packet to TX queue.
Packets are bufferred as per ffdpdk_queue_inittx(burst_size). */
FF_EXTN int ffdpdk_port_write(ffdpdk_queue *q, ffdpdk_mbuf *pkt);

/** Write the bufferred packets to TX queue. */
FF_EXTN uint ffdpdk_port_flush(ffdpdk_queue *q);

/** Process LACP packets.
Must be called at least every 100ms. */
FF_EXTN void ffdpdk_port_lacp_process(ffdpdk_queue *q);


/** Get pointer to Ethernet header from mbuf. */
#define ffdpdk_pkt_ptr(m, T)  rte_pktmbuf_mtod_offset(m, T, 0)

/** Get pointer with offset from mbuf. */
#define ffdpdk_pkt_ptroff(m, T, off)  rte_pktmbuf_mtod_offset(m, T, off)


/* KNI */

typedef struct ffdpdk_kni {
	struct rte_kni *kni;
	char name[32];

	ffdpdk_stat stat;
} ffdpdk_kni;

typedef struct ffdpdk_kni_queue {
	ffdpdk_kni *k;

	ffdpdk_mbuf **txbuf;
	uint txlen;
	uint txsize;
} ffdpdk_kni_queue;

/** Initialize KNI.
'n': number of interfaces */
FF_EXTN void ffdpdk_kni_init(uint n);

/** Associate device with a newly created KNI interface (named as "kniN"). */
FF_EXTN int ffdpdk_kni_open(ffdpdk_kni *k, ffdpdk_port *p, struct rte_mempool *mpool);

/** Close interface. */
FF_EXTN void ffdpdk_kni_close(ffdpdk_kni *k);

/** Handle interface events.
Must be called periodically. */
FF_EXTN void ffdpdk_kni_handle_events(ffdpdk_kni *k);

/** Read packets from interface. */
FF_EXTN int ffdpdk_kni_read(ffdpdk_kni *k, ffdpdk_mbuf **pkts, uint npkts);

/** Create TX queue. */
FF_EXTN int ffdpdk_kni_queue_inittx(ffdpdk_kni *k, ffdpdk_kni_queue *q, uint burst_size);

/** Close TX queue. */
FF_EXTN void ffdpdk_kni_queue_close(ffdpdk_kni_queue *q);

/** Add packets to interface TX queue.
Packets are bufferred. */
FF_EXTN int ffdpdk_kni_write(ffdpdk_kni_queue *q, ffdpdk_mbuf *pkt);

/** Write the bufferred packets to interface. */
FF_EXTN int ffdpdk_kni_flush(ffdpdk_kni_queue *q);


/* VHost */

typedef struct ffdpdk_vhost {
	char *path;
	int id;
	ffdpdk_mpool *mpool;
	uint reg :1;
	uint active :1;
} ffdpdk_vhost;

/** Open vhost-user driver.
'path': file path to a UNIX socket
'ops': callback functions
 .new_device: Called when device is added
  User calls ffdpdk_vhost_enable().
 .destroy_device: Called when device is removed
 The functions are called from another thread.
'flags': RTE_VHOST_USER_CLIENT: socket acts as a client.
*/
FF_EXTN int ffdpdk_vhost_open(ffdpdk_vhost *vh, const char *path, const struct vhost_device_ops *ops, ffdpdk_mpool *mpool, uint flags);

FF_EXTN void ffdpdk_vhost_destroy(ffdpdk_vhost *vh);

FF_EXTN int ffdpdk_vhost_start(ffdpdk_vhost *vh);

/** Assign ID to vhost object.
'nqueues': 2 */
FF_EXTN int ffdpdk_vhost_enable(ffdpdk_vhost *vh, uint id, uint nqueues);

/** Get socket path by vhost ID. */
#define ffdpdk_vhost_path(vid, buf, len) \
	rte_vhost_get_ifname(vid, buf, len)

/** Write packets to vhost TX queue.
'que': queue index
Return the number of packets. */
FF_EXTN int ffdpdk_vhost_write(ffdpdk_vhost *vh, uint que, ffdpdk_mbuf **pkts, uint cnt);

/** Read packets from vhost RX queue.
'que': queue index
Return the number of packets. */
FF_EXTN int ffdpdk_vhost_read(ffdpdk_vhost *vh, uint que, ffdpdk_mbuf **pkts, uint cnt);
