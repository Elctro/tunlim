#pragma once

#ifndef NOKRES

#include <libknot/packet/pkt.h>

#include "lib/module.h"
#include "lib/layer.h"

#include "lib/resolve.h"
#include "lib/rplan.h"

int getip(kr_layer_t *ctx, char *address, struct ip_addr *req_addr);
int checkDomain(char * qname_str, int * r, kr_layer_t * ctx, struct ip_addr * userIpAddress, const char * userIpAddressString);
int parse_addr_str(struct sockaddr_storage *sa, const char *addr);
int redirect(kr_layer_t *ctx, int rrtype, const char * originaldomain);

int begin(kr_layer_t *ctx);
int consume(kr_layer_t *ctx, knot_pkt_t *pkt);
int produce(kr_layer_t *ctx, knot_pkt_t *pkt);
int finish(kr_layer_t *ctx);

#endif