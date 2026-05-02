/*
 * Demo console output (matches lab / assignment log format).
 */

#ifndef DEMO_UI_H
#define DEMO_UI_H

#include <stdio.h>

static inline void
demo_station1_connected(void)
{
	puts("Connected in first station");
	puts("");
}

static inline void
demo_station2_connected(void)
{
	puts("Connected in second station");
	puts("");
}

static inline void
demo_file_preamble(long last_packet_bytes, int total_packets)
{
	printf(" last packets are :%ld\n\n", last_packet_bytes);
	printf("Total number of packets are :%d\n\n", total_packets);
}

static inline void
demo_line_sent(unsigned frame_no, int byte_len)
{
	printf("sent frame %u (%d bytes)\n", frame_no, byte_len);
}

static inline void
demo_line_packet_skipped(void)
{
	puts("Packet skipped");
}

static inline void
demo_line_track_skipped(void)
{
	puts("Track skipped");
}

static inline void
demo_receiver_in_thread(void)
{
	puts("in thread");
}

static inline void
demo_udp_socket_created(void)
{
	puts("udp Socket created");
}

static inline void
demo_bindtodevice_not_used(void)
{
	puts("SO_BINDTODEVICE not used on this platform");
}

static inline void
demo_udp_binded(void)
{
	puts("udp binded");
}

static inline void
demo_ready_to_listen(void)
{
	puts("");
	puts("Ready to listen!");
	puts("");
}

static inline void
demo_line_receiving(int seq, size_t nbytes)
{
	printf("%d Receiving  %zu\n", seq, nbytes);
}

static inline void
demo_not_ready(void)
{
	puts("Not ready.");
}

#endif
