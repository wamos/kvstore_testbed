/*
 * MIT License
 *
 * Copyright (c) 2019-2021 Ecole Polytechnique Federale Lausanne (EPFL)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <linux/version.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <linux/errqueue.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <net/if.h>

#define CONTROL_LEN 1024

struct timestamp_info {
	struct timespec time;
	uint32_t optid;
};

int enable_nic_timestamping(char *if_name);
int disable_nic_timestamping(char *if_name);
int sock_enable_timestamping(int fd);
//int extract_timestamp(struct msghdr *hdr, struct timestamp_info *dest);
int udp_get_rx_timestamp(struct msghdr *hdr, struct timestamp_info *rx_timeinfo);
int udp_get_tx_timestamp(int sockfd, struct timestamp_info *tx_timeinfo);

#endif //TIMESTAMP_H