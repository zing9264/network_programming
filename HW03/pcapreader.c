#include <pcap.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>
/* default snap length (maximum bytes per packet to capture) */
#define SNAP_LEN 1518

/* ethernet headers are always exactly 14 bytes [1] */
#define SIZE_ETHERNET 14

/* Ethernet addresses are 6 bytes */
#define ETHER_ADDR_LEN	6

/* Ethernet header */
struct sniff_ethernet {
        u_char  ether_dhost[ETHER_ADDR_LEN];    /* destination host address */
        u_char  ether_shost[ETHER_ADDR_LEN];    /* source host address */
        u_short ether_type;                     /* IP? ARP? RARP? etc */
};

/* IP header */
struct sniff_ip {
        u_char  ip_vhl;                 /* version << 4 | header length >> 2 */
        u_char  ip_tos;                 /* type of service */
        u_short ip_len;                 /* total length */
        u_short ip_id;                  /* identification */
        u_short ip_off;                 /* fragment offset field */
        #define IP_RF 0x8000            /* reserved fragment flag */
        #define IP_DF 0x4000            /* dont fragment flag */
        #define IP_MF 0x2000            /* more fragments flag */
        #define IP_OFFMASK 0x1fff       /* mask for fragmenting bits */
        u_char  ip_ttl;                 /* time to live */
        u_char  ip_p;                   /* protocol */
        u_short ip_sum;                 /* checksum */
        struct  in_addr ip_src,ip_dst;  /* source and dest address */
};
#define IP_HL(ip)               (((ip)->ip_vhl) & 0x0f)
#define IP_V(ip)                (((ip)->ip_vhl) >> 4)

/* TCP header */
typedef u_int tcp_seq;

struct sniff_tcp {
        u_short th_sport;               /* source port */
        u_short th_dport;               /* destination port */
        tcp_seq th_seq;                 /* sequence number */
        tcp_seq th_ack;                 /* acknowledgement number */
        u_char  th_offx2;               /* data offset, rsvd */
#define TH_OFF(th)      (((th)->th_offx2 & 0xf0) >> 4)
        u_char  th_flags;
        #define TH_FIN  0x01
        #define TH_SYN  0x02
        #define TH_RST  0x04
        #define TH_PUSH 0x08
        #define TH_ACK  0x10
        #define TH_URG  0x20
        #define TH_ECE  0x40
        #define TH_CWR  0x80
        #define TH_FLAGS        (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
        u_short th_win;                 /* window */
        u_short th_sum;                 /* checksum */
        u_short th_urp;                 /* urgent pointer */
};

void
got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);

void
print_payload(const u_char *payload, int len);

void
print_hex_ascii_line(const u_char *payload, int len, int offset);


/*
 * print help text
 */
void
print_app_usage(void)
{

	printf("使用方法: sudo ./pcapreader.out [pcapfile.pcap] [\"filter\"]\n");
	printf("\n");

return;
}

/*
 * print data in rows of 16 bytes: offset   hex   ascii
 *
 * 00000   47 45 54 20 2f 20 48 54  54 50 2f 31 2e 31 0d 0a   GET / HTTP/1.1..
 */
void
print_hex_ascii_line(const u_char *payload, int len, int offset)
{

	int i;
	int gap;
	const u_char *ch;

	/* offset */
	printf("%05d   ", offset);
	
	/* hex */
	ch = payload;
	for(i = 0; i < len; i++) {
		printf("%02x ", *ch);
		ch++;
		/* print extra space after 8th byte for visual aid */
		if (i == 7)
			printf(" ");
	}
	/* print space to handle line less than 8 bytes */
	if (len < 8)
		printf(" ");
	
	/* fill hex gap with spaces if not full line */
	if (len < 16) {
		gap = 16 - len;
		for (i = 0; i < gap; i++) {
			printf("   ");
		}
	}
	printf("   ");
	
	/* ascii (if printable) */
	ch = payload;
	for(i = 0; i < len; i++) {
		if (isprint(*ch))
			printf("%c", *ch);
		else
			printf(".");
		ch++;
	}
	printf("\n");

return;
}

/*
 * print packet payload data (avoid printing binary data)
 */
void
print_payload(const u_char *payload, int len)
{

	int len_rem = len;
	int line_width = 16;			/* number of bytes per line */
	int line_len;
	int offset = 0;					/* zero-based offset counter */
	const u_char *ch = payload;

	if (len <= 0)
		return;

	/* data fits on one line */
	if (len <= line_width) {
		print_hex_ascii_line(ch, len, offset);
		return;
	}

	/* data spans multiple lines */
	for ( ;; ) {
		/* compute current line length */
		line_len = line_width % len_rem;
		/* print line */
		print_hex_ascii_line(ch, line_len, offset);
		/* compute total remaining */
		len_rem = len_rem - line_len;
		/* shift pointer to remaining bytes to print */
		ch = ch + line_len;
		/* add offset */
		offset = offset + line_width;
		/* check if we have line width chars or less */
		if (len_rem <= line_width) {
			/* print last line and get out */
			print_hex_ascii_line(ch, len_rem, offset);
			break;
		}
	}

return;
}

/*
 * dissect/print packet
 */
void
got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet)
{

	static int count = 1;                   /* packet counter */
	
	/* declare pointers to packet headers */
	const struct sniff_ethernet *ethernet;  /* The ethernet header [1] */
	const struct sniff_ip *ip;              /* The IP header */
	const struct sniff_tcp *tcp;            /* The TCP header */
	const char *payload;                    /* Packet payload */

	int size_ip;
	int size_tcp;
	int size_payload;
	
    printf("\n--------------------\n資料筆數:  %d\n",count);
    count++;
	/* define ethernet header */
	ethernet = (struct sniff_ethernet*)(packet);
	
	/* define/compute ip header offset */
	ip = (struct sniff_ip*)(packet + SIZE_ETHERNET);
	size_ip = IP_HL(ip)*4;
	if (size_ip < 20) {
		printf("   * Invalid IP header length: %u bytes\n", size_ip);
		return;
	}

	/* print source and destination IP addresses */
	printf("    來源地址: %s\n", inet_ntoa(ip->ip_src));
	printf("    目的地址: %s\n", inet_ntoa(ip->ip_dst));
	char timestr[80];
    struct tm* ltime;
    ltime = localtime(&header->ts.tv_sec);
    strftime(timestr, sizeof(timestr), "%Y年 %m月 %d日 %H時 %M分 %S秒", ltime);
    printf("應有封包長度: %d\n", header->len);//封包長度、
    printf("獲取封包長度: %d\n",header->caplen);//封包長度、
    
    printf("    封包日期: %s \n", timestr);//時間(以年、月、日、時、分、秒等單位顯示)。

	/* determine protocol */	
	switch(ip->ip_p) {
		case IPPROTO_TCP:
			printf("    Protocol: TCP\n");
			break;
		case IPPROTO_UDP:
			printf("    Protocol: UDP\n");
			break;;
		case IPPROTO_ICMP:
			printf("    Protocol: ICMP\n");
			return;
        case IPPROTO_IGMP:
			printf("    Protocol: IGMP\n");
			return;
		case IPPROTO_IP:
			printf("    Protocol: IP\n");
			return;
		default:
			printf("    Protocol: unknown\n");
			return;
	}

	tcp = (struct sniff_tcp*)(packet + SIZE_ETHERNET + size_ip);
	size_tcp = TH_OFF(tcp)*4;	
	printf("   來源 port: %d\n", ntohs(tcp->th_sport));
	printf("   目的 port: %d\n", ntohs(tcp->th_dport));

	if (size_tcp < 20) {
		printf("   非TCP封包,不顯示資料內容\n");
		return;
	}
    

	
	/* define/compute tcp payload (segment) offset */
	payload = (u_char *)(packet + SIZE_ETHERNET + size_ip + size_tcp);
	
	/* compute tcp payload (segment) size */
	size_payload = ntohs(ip->ip_len) - (size_ip + size_tcp);
	
	/*
	 * Print payload data; it might be binary, so don't just
	 * treat it as a string.
	 */
	if (size_payload > 0) {
		printf("\n   顯示資料 (%d bytes):\n", size_payload);
		print_payload(payload, size_payload);
	}

return;
}

int main(int argc, char **argv)
{
	char errbuf[PCAP_ERRBUF_SIZE];		/* error buffer */
	pcap_t *handle;				/* packet capture handle */
    char *dev;			/* The device to sniff on */
	char *filter_exp;		/* filter expression [3] */
	struct bpf_program fp;			/* compiled filter program (expression) */
	bpf_u_int32 mask;			/* subnet mask */
	bpf_u_int32 net;			/* ip */
	int num_packets = -1;			/* number of packets to capture */
    bool isfile=false;

	/* check for capture device name on command-line */
	if (argc > 3) {
		fprintf(stderr, "error: 無法識別的命令列指令選項 \n\n");
		print_app_usage();
		exit(EXIT_FAILURE);
	}

    if(argc>1 && strstr(argv[1],"pcap")!=0){
        isfile=true;
    }

    if(isfile){
        if( argc < 3){
            filter_exp="ip";
        }
        else if(argc == 3){
            filter_exp=argv[2];
        }
    }
    else{
        /* find a capture device if not specified on command-line */
		dev = pcap_lookupdev(errbuf);
		if (dev == NULL) {
			fprintf(stderr, "Couldn't find default device: %s\n",
			    errbuf);
			exit(EXIT_FAILURE);
		}
        /* Find the properties for the device */
	    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
			fprintf(stderr, "Couldn't get netmask for device %s: %s\n", dev, errbuf);
			net = 0;
			mask = 0;
	    }
        if( argc <2){
            filter_exp="ip";
        }
        else{
            filter_exp=argv[1];
        }
    }
	/* print capture info */
	printf("過濾器表達式： %s\n", filter_exp);

	/* open capture device */
    char filename[120];

    if(isfile==false){
        handle = pcap_open_live(dev, BUFSIZ, 1, 1500, errbuf);
		if (handle == NULL) {
			fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
			return(2);
		}
    }
    else{
        memset(filename,'\0',120);
        strcpy(filename,argv[1]);
        handle = pcap_open_offline(filename, errbuf);
        if (handle == NULL) {
            fprintf(stderr, "Couldn't open file %s: %s\n", filename, errbuf);
            print_app_usage();
            exit(EXIT_FAILURE);
        }
    }

	/* compile the filter expression */
	if (pcap_compile(handle, &fp, filter_exp, 0, net) == -1) {
		fprintf(stderr, "Couldn't parse filter %s: %s\n",
		    filter_exp, pcap_geterr(handle));
		exit(EXIT_FAILURE);
	}

	/* apply the compiled filter */
	if (pcap_setfilter(handle, &fp) == -1) {
		fprintf(stderr, "Couldn't install filter %s: %s\n",
		    filter_exp, pcap_geterr(handle));
		exit(EXIT_FAILURE);
	}

	/* now we can set our callback function */
    pcap_dispatch(handle, num_packets,got_packet,NULL);
	/* cleanup */
	pcap_freecode(&fp);
	pcap_close(handle);

	printf("\n--------------------\n捕獲完成.\n");

return 0;
}