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
#include "timestamping.h"

static int set_timestamping_filter(int fd, char *if_name, int rx_filter,
								   int tx_type)
{
	struct ifreq ifr;
	struct hwtstamp_config config;

	config.flags = 0;
	config.tx_type = tx_type;
	config.rx_filter = rx_filter;

	strcpy(ifr.ifr_name, if_name);
	ifr.ifr_data = (caddr_t)&config;

	if (ioctl(fd, SIOCSHWTSTAMP, &ifr)) {
		perror("ERROR setting NIC timestamping: ioctl SIOCSHWTSTAMP");
		return -1;
	}
	return 0;
}

/*
 * Returns -1 if no new timestamp found
 * 1 if timestamp found
 */
static int udp_extract_timestamps(struct msghdr *hdr, struct timestamp_info *ts_info)
{
	struct cmsghdr *cmsg;
	struct scm_timestamping *ts;
	int found = -1;

	for (cmsg = CMSG_FIRSTHDR(hdr); cmsg != NULL;
		 cmsg = CMSG_NXTHDR(hdr, cmsg)) {
		if (cmsg->cmsg_type == SCM_TIMESTAMPING) {
			ts = (struct scm_timestamping *)CMSG_DATA(cmsg);
			if (ts->ts[2].tv_sec != 0) {
				// Make sure we don't get multiple timestamps for the same
				assert(found == -1);
				// printf("tx:\n");
				// printf("ts[0]: tv_nsec %ld, tv_sec %ld\n", ts->ts[0].tv_nsec, ts->ts[0].tv_sec);
				// printf("ts[1]: tv_nsec %ld, tv_sec %ld\n", ts->ts[1].tv_nsec, ts->ts[1].tv_sec);
				// printf("ts[2]: tv_nsec %ld, tv_sec %ld\n", ts->ts[2].tv_nsec, ts->ts[2].tv_sec);
				ts_info->time = ts->ts[2];
				//ts_info->time.tv_nsec = ts->ts[2].tv_nsec;
				found = 1;
			}
		}

		if (cmsg->cmsg_type == IP_RECVERR) {
			struct sock_extended_err *se =
				(struct sock_extended_err *)CMSG_DATA(cmsg);
			/*
			 * Make sure we got the timestamp for the right request
			 */
			if (se->ee_errno == ENOMSG &&
				se->ee_origin == SO_EE_ORIGIN_TIMESTAMPING){
				//printf("tx optid:%u\n", se->ee_data);
				ts_info->optid = se->ee_data;
			}
			else{
				printf("Received IP_RECVERR: errno = %d %s\n", se->ee_errno, strerror(se->ee_errno));
			}
		} 
	}
	return found;
}

int udp_get_rx_timestamp(struct msghdr *hdr, struct timestamp_info *dest)
{
	struct cmsghdr *cmsg;
	struct scm_timestamping *ts;
	int found = -1;

	for (cmsg = CMSG_FIRSTHDR(hdr); cmsg != NULL;
		 cmsg = CMSG_NXTHDR(hdr, cmsg)) {
		if (cmsg->cmsg_type == SCM_TIMESTAMPING) {
			ts = (struct scm_timestamping *)CMSG_DATA(cmsg);
			//if (ts->ts[2].tv_sec != 0) {
				// make sure we don't get multiple timestamps for the same
				//printf("cmsg_type == SCM_TIMESTAMPING\n");
				assert(found == -1);
				// printf("rx:\n");
				// printf("ts[0]: tv_nsec %ld, tv_sec %ld\n", ts->ts[0].tv_nsec, ts->ts[0].tv_sec);
				// printf("ts[1]: tv_nsec %ld, tv_sec %ld\n", ts->ts[1].tv_nsec, ts->ts[1].tv_sec);
				// printf("ts[2]: tv_nsec %ld, tv_sec %ld\n", ts->ts[2].tv_nsec, ts->ts[2].tv_sec);
				dest->time = ts->ts[2];
				found = 1;
			//}
		} else if (cmsg->cmsg_type == IP_RECVERR) {
			struct sock_extended_err *se =
				(struct sock_extended_err *)CMSG_DATA(cmsg);
			/*
			 * Make sure we got the timestamp for the right request
			 */
			if (se->ee_errno == ENOMSG && se->ee_origin == SO_EE_ORIGIN_TIMESTAMPING){
				dest->optid = se->ee_data;
				printf("optid:%u\n", se->ee_data);
			}
			else{
				printf("Received IP_RECVERR: errno = %d %s\n",
							   se->ee_errno, strerror(se->ee_errno));
			}
		} else
			assert(0);
	}
	return found;
}

int enable_nic_timestamping(char *if_name)
{
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int filter = HWTSTAMP_FILTER_ALL;
	//int filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
	int tx_type = HWTSTAMP_TX_ON;
	int ret;

	ret = set_timestamping_filter(fd, if_name, filter, tx_type);
	close(fd);
	return ret;
}

int disable_nic_timestamping(char *if_name)
{
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int ret = set_timestamping_filter(fd, if_name, HWTSTAMP_FILTER_NONE,
									  HWTSTAMP_TX_OFF);
	close(fd);
	return ret;
}

int sock_enable_timestamping(int fd)
{
	int ts_mode = 0;

	ts_mode |= SOF_TIMESTAMPING_OPT_TSONLY | SOF_TIMESTAMPING_OPT_ID;

	ts_mode |= SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE |
			   SOF_TIMESTAMPING_TX_HARDWARE;

	ts_mode |= SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE;

	if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &ts_mode, sizeof(ts_mode)) <
		0) {
		perror(
			"ERROR enabling socket timestamping: setsockopt SO_TIMESTAMPING.");
		return -1;
	}
	return 0;
}

/*
 * Used only for NIC timestamping with UDP
 * Returns -1 if no new timestamp found
 * 1 if timestamp found
 */
int udp_get_tx_timestamp(int sockfd, struct timestamp_info *tx_timeinfo)
{
	char tx_control[CONTROL_LEN] = {0};
	struct msghdr mhdr = {0};
	struct iovec junk_iov = {NULL, 0};

	mhdr.msg_iov = &junk_iov;
	mhdr.msg_iovlen = 1;
	mhdr.msg_control = tx_control;
	mhdr.msg_controllen = CONTROL_LEN;

	ssize_t bytes = recvmsg(sockfd, &mhdr, MSG_ERRQUEUE);
	if (bytes < 0) {
		return -1;
	}

	assert(bytes == 0);

	return udp_extract_timestamps(&mhdr, tx_timeinfo);
}