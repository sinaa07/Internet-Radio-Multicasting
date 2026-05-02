//Receiver with gtk
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "../sender/demo_ui.h"

#define MC_PORT 5433
#define BUF_SIZE 64000

int done = 0; 
int r=1;

/* Function for Pause */
int func1(GtkWidget *widget,gpointer   data)
{
   (void)widget;
   (void)data;
   g_print ("video is pause...\n");
   done=1;
   return 0;
}

/* Function for Resume */
int func2(GtkWidget *widget,gpointer   data)
{
   (void)widget;
   (void)data;
   g_print ("Resumed\n");
  
   done=0;
	r=0;
	usleep(5000000);
   return 0;
}

/* Function for Change Station */
void func3(GtkWidget *widget,gpointer   data)
{
   (void)widget;
   (void)data;
   g_print ("Changing station\n");
   system("pkill ffplay");
   remove("live_data.mp4");
   exit(0);
}

/* Function for Terminate */
int func4(GtkWidget *widget,gpointer   data)
{
  (void)widget;
  (void)data;
  g_print ("Goodbye\n");
  system("pkill ffplay");
  remove("live_data.mp4");
  exit(0);
  return 0;
}

void* threadFunction(void* args)
{
  int s;
  struct sockaddr_in sin;
#if defined(__linux__) && defined(SO_BINDTODEVICE)
  const char *if_name = "wlan0";
  struct ifreq ifr;
#endif

  char buf[BUF_SIZE];
  ssize_t len;
  char *mcast_addr;
  struct ip_mreq mcast_req;
  struct sockaddr_in mcast_saddr;
  socklen_t mcast_saddr_len;
 
  mcast_addr = args;

  demo_receiver_in_thread();

  /* create socket */
  if ((s = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
  {
		demo_not_ready();
		exit(1);
  }
  demo_udp_socket_created();

  /* build address data structure */
  memset((char *)&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  sin.sin_port = htons(MC_PORT);
 
  /* Bind UDP to a specific interface (Linux only; not supported on macOS). */
#if defined(__linux__) && defined(SO_BINDTODEVICE)
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, if_name, IFNAMSIZ - 1);
  if ((setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr))) < 0)
  {
      close(s);
      demo_not_ready();
      exit(1);
  }
#else
  demo_bindtodevice_not_used();
#endif

  /* bind the socket */
  
  if ((bind(s, (struct sockaddr *) &sin, sizeof(sin))) < 0)
  {
    close(s);
    demo_not_ready();
    exit(1);
  }
  demo_udp_binded();
  /* Multicast specific code follows */
 
  /* build IGMP join message structure */
  mcast_req.imr_multiaddr.s_addr = inet_addr(mcast_addr);
  mcast_req.imr_interface.s_addr = htonl(INADDR_ANY);

  /* send multicast join message */
  if ((setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*) &mcast_req, sizeof(mcast_req))) < 0)
  {
    close(s);
    demo_not_ready();
    exit(1);
  }

  demo_ready_to_listen();

  int i=0;
  FILE *fp;
  fp=fopen("live_data.mp4","wb");
  if (fp == NULL) {
    close(s);
    demo_not_ready();
    exit(1);
  }

  /* reset sender struct */
  memset(&mcast_saddr, 0, sizeof(mcast_saddr));
  mcast_saddr_len = sizeof(mcast_saddr);
    
  while(1)
  {   
    	if(done==0)
      {
             memset(buf, 0, sizeof(buf));

             len = recvfrom(s, buf, sizeof(buf), 0,(struct sockaddr*)&mcast_saddr, &mcast_saddr_len);
            
             if (len < 0)
             {
                 /* silent */
             }
             else
             {
                 fwrite(buf,1,(size_t)len,fp);
                 demo_line_receiving(i, (size_t)len);
                 if(i==8)
            	  {
#if defined(__APPLE__)
							system("ffplay -loglevel error -i live_data.mp4 >/dev/null 2>&1 &");
#else
							system("gnome-terminal -- sh -c 'ffplay -i live_data.mp4;'");
#endif
					  }    
             }
             i++;
        }
  }
               
  fclose(fp);
  close(s);
   
}

int main(int argc, char *argv[])
{
    char *mcast_addr;

    if (argc != 2)
    {
	    printf("receiver <multicast_address>\n");
	    return 1;
    }
    mcast_addr = argv[1];

    pthread_t id;
    if (pthread_create(&id, NULL, &threadFunction, mcast_addr) != 0)
    {
	    demo_not_ready();
	    return 1;
    }

    gtk_init (&argc, &argv);
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *grid;
    GtkWidget *button;
    GtkCssProvider *css;

    gtk_window_set_title (GTK_WINDOW (window), "Control!");
    gtk_window_set_default_size (GTK_WINDOW (window), 200, 200);		//Set window size
    gtk_widget_set_name (window, "receiver_root");
    css = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (
        css,
        "#receiver_root { background-color: lightyellow; }\n",
        -1,
        NULL);
    gtk_style_context_add_provider (
        gtk_widget_get_style_context (window),
        GTK_STYLE_PROVIDER (css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (css);

    g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
      
    grid = gtk_grid_new ();
      
    gtk_container_add (GTK_CONTAINER (window), grid);
  
  
    button = gtk_button_new_with_label ("Pause");

    g_signal_connect (button, "clicked", G_CALLBACK (func1), NULL);	//call function 1 for pause
  
    gtk_grid_attach (GTK_GRID (grid), button, 5, 5, 5, 5);

    button = gtk_button_new_with_label ("Resume");
    g_signal_connect (button, "clicked", G_CALLBACK (func2), NULL);	//call function 2 for resume

    gtk_grid_attach (GTK_GRID (grid), button, 5, 10, 5, 5);

    button = gtk_button_new_with_label ("Change station");
	 g_signal_connect (button, "clicked", G_CALLBACK (func3), NULL);	//call function 3 for change station

    gtk_grid_attach (GTK_GRID (grid), button, 5, 15, 5, 5);

    button = gtk_button_new_with_label ("Terminate");
    g_signal_connect (button, "clicked", G_CALLBACK (func4), NULL);	//call function 4 for teminate

    gtk_grid_attach (GTK_GRID (grid), button, 5, 20, 5, 5);
 
     
    gtk_widget_show_all (window);
    gtk_main ();

    return 0;
}

