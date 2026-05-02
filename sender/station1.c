//sender_udp with gtk

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "multicast_tx.h"
#include "demo_ui.h"

#define MC_PORT 5433
#define BUF_SIZE 1200

static void
quiet_socket_setup(int s)
{
	unsigned char loop = 1;

	(void)setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP,
	    &loop, sizeof(loop));
}

//structure of song info
struct song_info
{
	  char song_name[ 50 ];
	  uint16_t remaining_time_in_sec;
	  char next_song_name[ 50 ];
};

int main(int argc, char * argv[])
{
	  int s;
	  struct sockaddr_in sin;
	  int len;
	  socklen_t sin_len;
	  sin_len = sizeof(sin);

	  char *mcast_addr;
	  char *iface_ip_opt = NULL;
	  char * video[5];

	  video[0] = "vid7.mp4";
	  video[1] = "vid4.mp4";
	  video[2] = "vid1.mp4";
	  video[3] = "vid3.mp4";

	  if (argc == 2 || argc == 3) {
		 	mcast_addr = argv[1];
		 	if (argc == 3)
		 		iface_ip_opt = argv[2];
	  }
	  else {
		  printf("%s <multicast_address> [optional_local_ip]\n", argv[0]);
		  return 1;
	  }

	  multicast_tx_set_demo_quiet(1);

	  if ((s = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		  demo_not_ready();
		  return 1;
	  }

	  if (multicast_tx_configure_ipv4(s, iface_ip_opt, mcast_addr,
		  MC_PORT) < 0) {
		  demo_not_ready();
		  close(s);
		  return 1;
	  }

	  quiet_socket_setup(s);

	  memset((char *)&sin, 0, sizeof(sin));
	  sin.sin_family = AF_INET;
	  sin.sin_addr.s_addr = inet_addr(mcast_addr);
	  sin.sin_port = htons(MC_PORT);

	  demo_station1_connected();

	  FILE *fp = NULL;
	  int vid;

	  while (1)
	  {
		  unsigned long lap_sent = 0;
		  unsigned fr;

		  for (vid = 0; vid < 4; vid++)
		  {
		  	fp = fopen(video[vid], "rb");

		  	if (fp == NULL)
		  	{
		  		demo_line_track_skipped();
		  	}
		  	else
		  	{
				  int tot_frame;
				  long fsize;
				  long p;

				  fseek(fp, 0, SEEK_END);
				  fsize = ftell(fp);
				  p = (fsize % BUF_SIZE);
				  if ((fsize % BUF_SIZE) != 0)
					  tot_frame = (int)((fsize / BUF_SIZE) + 1);
				  else
					  tot_frame = (int)(fsize / BUF_SIZE);

				  (void)p;

				  fseek(fp, 0, SEEK_SET);

				  if (tot_frame == 0) {
					  fclose(fp);
					  continue;
				  }

				  if (tot_frame == 1)
				  {
					  char *payload =
					      malloc((size_t)fsize + 1u);
					  size_t bytes_read;
					  long last_sz;

					  if (payload == NULL) {
						  fclose(fp);
						  continue;
					  }
					  last_sz = fsize;
					  demo_file_preamble(last_sz,
					      tot_frame);
					  (void)fseek(fp, 0, SEEK_SET);
					  bytes_read = fread(payload, 1,
					      (size_t)fsize, fp);
					  if (sendto(s, payload,
					      bytes_read, 0,
					      (struct sockaddr *)&sin,
					      sin_len) < 0)
						  demo_line_packet_skipped();
					  else {
						  lap_sent++;
						  demo_line_sent(1,
						      (int)bytes_read);
					  }
					  free(payload);
				  }
				  else
				  {
					  long last_sz;

					  last_sz = (fsize % BUF_SIZE) != 0 ?
					      (fsize % BUF_SIZE) : BUF_SIZE;
					  demo_file_preamble(last_sz,
					      tot_frame);
					  for (fr = 1; fr <= (unsigned)
					      tot_frame; fr++)
					  {
						  char *chunk = malloc(BUF_SIZE);

						  if (chunk == NULL)
							  break;
						  len =
						      (int)fread(chunk, 1,
						      BUF_SIZE, fp);
						  (void)fseek(fp, 0,
						      SEEK_CUR);
						  if (sendto(s, chunk,
							      len, 0,
							      (struct sockaddr
							      *)&sin,
							      sin_len) < 0) {
							  demo_line_packet_skipped();
							  free(chunk);
							  break;
						  }
						  lap_sent++;
						  demo_line_sent(fr, len);
						  free(chunk);
						  usleep(400000);
					  }
				  }
				  fclose(fp);
		  	}
		  }
		  (void)lap_sent;
	  }
	  return 0;
}
