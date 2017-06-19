/***************************************************************************
* Network Read code for GC via Broadband Adapter
* Extrems 2017
***************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../reservedarea.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define MIN_FRAME_SIZE 60

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_VLAN 0x8100

#define HW_ETHERNET 1

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17

enum {
	ARP_REQUEST = 1,
	ARP_REPLY,
};

enum {
	CC_VERSION = 0x10,
	CC_ERR     = 0x40,
	CC_GET_DIR,
	CC_GET_FILE,
};

struct eth_addr {
	uint64_t addr : 48;
} __attribute((packed));

struct ipv4_addr {
	uint32_t addr;
} __attribute((packed));

typedef struct {
	struct eth_addr dst_addr;
	struct eth_addr src_addr;
	uint16_t type;
	uint8_t data[];
} __attribute((packed)) eth_header_t;

typedef struct {
	uint16_t pcp : 3;
	uint16_t dei : 1;
	uint16_t vid : 12;
	uint16_t type;
	uint8_t data[];
} __attribute((packed)) vlan_header_t;

typedef struct {
	uint16_t hardware_type;
	uint16_t protocol_type;
	uint8_t hardware_length;
	uint8_t protocol_length;
	uint16_t operation;

	struct eth_addr src_mac;
	struct ipv4_addr src_ip;
	struct eth_addr dst_mac;
	struct ipv4_addr dst_ip;
} __attribute((packed)) arp_packet_t;

typedef struct {
	uint8_t version : 4;
	uint8_t words   : 4;
	uint8_t dscp    : 6;
	uint8_t ecn     : 2;
	uint16_t length;
	uint16_t id;
	uint16_t flags  : 3;
	uint16_t offset : 13;
	uint8_t ttl;
	uint8_t protocol;
	uint16_t checksum;
	struct ipv4_addr src_addr;
	struct ipv4_addr dst_addr;
	uint8_t data[];
} __attribute((packed)) ipv4_header_t;

typedef struct {
	uint16_t src_port;
	uint16_t dst_port;
	uint16_t length;
	uint16_t checksum;
	uint8_t data[];
} __attribute((packed)) udp_header_t;

typedef struct {
	uint8_t command;
	uint8_t checksum;
	uint16_t key;
	uint16_t sequence;
	uint16_t data_length;
	uint32_t position;
	uint8_t data[];
} __attribute((packed)) fsp_header_t;

static uint16_t ipv4_checksum(ipv4_header_t *header)
{
	uint16_t *data = (uint16_t *)header;
	uint32_t sum[2] = {0};

	for (int i = 0; i < header->words; i++) {
		sum[0] += *data++;
		sum[1] += *data++;
	}

	sum[0] += sum[1];
	sum[0] += sum[0] >> 16;
	return ~sum[0];
}

static uint8_t fsp_checksum(fsp_header_t *header, uint16_t size)
{
	uint8_t *data = (uint8_t *)header;
	uint32_t sum = size;

	for (int i = 0; i < size; i++)
		sum += *data++;

	sum += sum >> 8;
	return sum;
}

void bba_transmit(void *buffer, int size);

void fsp_output(const char *file, uint8_t filelen, uint32_t offset, uint32_t size)
{
	uint8_t data[MIN_FRAME_SIZE + filelen];
	eth_header_t *eth = (eth_header_t *)data;
	ipv4_header_t *ipv4 = (ipv4_header_t *)eth->data;
	udp_header_t *udp = (udp_header_t *)ipv4->data;
	fsp_header_t *fsp = (fsp_header_t *)udp->data;

	fsp->command = CC_GET_FILE;
	fsp->checksum = 0x00;
	fsp->key = *(uint16_t *)VAR_FSP_KEY;
	fsp->sequence = 0;
	fsp->data_length = filelen;
	fsp->position = offset;
	*(uint16_t *)(memcpy(fsp->data, file, filelen) + fsp->data_length) = MIN(size, UINT16_MAX);
	fsp->checksum = fsp_checksum(fsp, sizeof(*fsp) + fsp->data_length + sizeof(uint16_t));

	udp->src_port = 21;
	udp->dst_port = 21;
	udp->length = sizeof(*udp) + sizeof(*fsp) + fsp->data_length + sizeof(uint16_t);
	udp->checksum = 0x0000;

	ipv4->version = 4;
	ipv4->words = sizeof(*ipv4) / 4;
	ipv4->dscp = 46;
	ipv4->ecn = 0b00;
	ipv4->length = sizeof(*ipv4) + udp->length;
	ipv4->id = 0;
	ipv4->flags = 0b000;
	ipv4->offset = 0;
	ipv4->ttl = 64;
	ipv4->protocol = IP_PROTO_UDP;
	ipv4->checksum = 0x0000;
	ipv4->src_addr.addr = (*(struct ipv4_addr *)VAR_CLIENT_IP).addr;
	ipv4->dst_addr.addr = (*(struct ipv4_addr *)VAR_SERVER_IP).addr;
	ipv4->checksum = ipv4_checksum(ipv4);

	eth->dst_addr.addr = (*(struct eth_addr *)VAR_SERVER_MAC).addr;
	eth->src_addr.addr = (*(struct eth_addr *)VAR_CLIENT_MAC).addr;
	eth->type = ETH_TYPE_IPV4;

	bba_transmit(eth, sizeof(*eth) + ipv4->length);
}

static void fsp_input(eth_header_t *eth, ipv4_header_t *ipv4, udp_header_t *udp, fsp_header_t *fsp, uint16_t size)
{
	if (size < sizeof(*fsp))
		return;
	if (udp->length < sizeof(*udp) + sizeof(*fsp) + fsp->data_length)
		return;

	size -= sizeof(*fsp);

	switch (fsp->command) {
		case CC_ERR:
			break;
		case CC_GET_FILE:
			*(uint16_t *)VAR_IPV4_ID         = ipv4->id;
			*(uint32_t *)VAR_FSP_POSITION    = fsp->position;
			*(uint16_t *)VAR_FSP_DATA_LENGTH = fsp->data_length;
			break;
	}

	*(uint16_t *)VAR_FSP_KEY = fsp->key;
}

static void udp_input(eth_header_t *eth, ipv4_header_t *ipv4, udp_header_t *udp, uint16_t size)
{
	if (ipv4->src_addr.addr == (*(struct ipv4_addr *)VAR_SERVER_IP).addr &&
		ipv4->dst_addr.addr == (*(struct ipv4_addr *)VAR_CLIENT_IP).addr) {

		(*(struct eth_addr *)VAR_SERVER_MAC).addr = eth->src_addr.addr;

		if (ipv4->offset == 0) {
			if (size < sizeof(*udp))
				return;
			if (udp->length < sizeof(*udp))
				return;

			size -= sizeof(*udp);

			if (udp->src_port == 21 &&
				udp->dst_port == 21)
				fsp_input(eth, ipv4, udp, (void *)udp->data, size);
		}

		if (ipv4->id == *(uint16_t *)VAR_IPV4_ID) {
			uint32_t position    = *(uint32_t *)VAR_FSP_POSITION;
			uint16_t data_length = *(uint16_t *)VAR_FSP_DATA_LENGTH;

			if (position != EOF) {
				uint32_t data = *(uint32_t *)VAR_TMP1;
				uint32_t left = *(uint32_t *)VAR_TMP2;

				int offset = ipv4->offset * 8 - sizeof(udp_header_t) - sizeof(fsp_header_t);
				int ipv4_offset = MIN(offset, 0);
				int data_offset = MAX(offset, 0);

				memcpy((void *)data + data_offset, ipv4->data - ipv4_offset, MIN(data_length - data_offset, size));

				if (!(ipv4->flags & 0b001)) {
					data += data_length;
					left -= data_length;

					*(uint32_t *)VAR_TMP1 = data;
					*(uint32_t *)VAR_TMP2 = left;

					if (left) fsp_output((const char *)VAR_FILENAME, *(uint8_t *)VAR_FILENAME_LEN, position + data_length, left);

					*(uint32_t *)VAR_FSP_POSITION    = EOF;
					*(uint16_t *)VAR_FSP_DATA_LENGTH = 0;
				}
			}
		}
	}
}

static void ipv4_input(eth_header_t *eth, ipv4_header_t *ipv4, uint16_t size)
{
	if (ipv4->version != 4)
		return;
	if (ipv4->words < 5 || ipv4->words * 4 > ipv4->length)
		return;
	if (size < ipv4->length)
		return;
	if (ipv4_checksum(ipv4))
		return;

	size = ipv4->length - ipv4->words * 4;

	switch (ipv4->protocol) {
		case IP_PROTO_UDP:
			udp_input(eth, ipv4, (void *)ipv4->data, size);
			break;
	}
}

static void arp_input(eth_header_t *eth, arp_packet_t *arp, uint16_t size)
{
	if (arp->hardware_type != HW_ETHERNET || arp->hardware_length != sizeof(struct eth_addr))
		return;
	if (arp->protocol_type != ETH_TYPE_IPV4 || arp->protocol_length != sizeof(struct ipv4_addr))
		return;

	switch (arp->operation) {
		case ARP_REQUEST:
			if ((!arp->dst_mac.addr ||
				arp->dst_mac.addr == (*(struct eth_addr *)VAR_CLIENT_MAC).addr) &&
				arp->dst_ip.addr == (*(struct ipv4_addr *)VAR_CLIENT_IP).addr) {

				arp->operation = ARP_REPLY;

				arp->dst_mac.addr = arp->src_mac.addr;
				arp->dst_ip.addr = arp->src_ip.addr;

				arp->src_mac.addr = (*(struct eth_addr *)VAR_CLIENT_MAC).addr;
				arp->src_ip.addr = (*(struct ipv4_addr *)VAR_CLIENT_IP).addr;

				eth->dst_addr.addr = arp->dst_mac.addr;
				eth->src_addr.addr = arp->src_mac.addr;

				bba_transmit(eth, MIN_FRAME_SIZE);
			}
			break;
		case ARP_REPLY:
			if (arp->dst_mac.addr == (*(struct eth_addr *)VAR_CLIENT_MAC).addr &&
				arp->dst_ip.addr == (*(struct ipv4_addr *)VAR_CLIENT_IP).addr &&
				arp->src_ip.addr == (*(struct ipv4_addr *)VAR_SERVER_IP).addr) {

				(*(struct eth_addr *)VAR_SERVER_MAC).addr = arp->src_mac.addr;
			}
			break;
	}
}

void eth_input(eth_header_t *eth, uint16_t size)
{
	if (size < MIN_FRAME_SIZE)
		return;

	size -= sizeof(*eth);

	switch (eth->type) {
		case ETH_TYPE_ARP:
			arp_input(eth, (void *)eth->data, size);
			break;
		case ETH_TYPE_IPV4:
			ipv4_input(eth, (void *)eth->data, size);
			break;
	}
}
