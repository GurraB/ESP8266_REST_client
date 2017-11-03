#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "ip_addr.h"
#include "espconn.h"
#include "user_interface.h"
#include "user_config.h"
#include "mem.h"

char rest_host[] = "http://ahellbe.se/";
char rest_path[] = "";
char json_data[ 256 ];
char buffer[ 2048 ];

struct espconn rest_conn;
ip_addr_t rest_ip;
esp_tcp rest_tcp;

LOCAL os_timer_t test_timer;
LOCAL os_timer_t connect_timer;
LOCAL struct espconn tcp_conn;
LOCAL struct _esp_tcp esptcp;
ip_addr_t tcp_host_ip;
#define host_dns_name "ahellbe.se"
#define packet_size 2048
#define pheadbuffer "GET / HTTP/1.1\r\nUser-Agent: curl/7.37.0\r\nHost: %s\r\nAccept: */*\r\n\r\n"
#define pheadbufferpost "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s"

void user_rf_pre_init( void )
{

}

LOCAL void ICACHE_FLASH_ATTR tcp_receive_cb(void *arg, char *pusrdata, unsigned short length)
{
	os_printf("Received data:\n%s \r\n", pusrdata);
	wifi_station_disconnect();
}

LOCAL void ICACHE_FLASH_ATTR tcp_sent_cb(void *arg)
{
	os_printf("Sent callback: data sent successfully.\r\n");
}

LOCAL void ICACHE_FLASH_ATTR tcp_disconnect_cb(void *arg)
{
	os_printf("Disconnected from server.\r\n");
	wifi_station_disconnect();
	deep_sleep_set_option( 0 );
        system_deep_sleep( 60 * 1000 * 1000 );  // 60 seconds
}

LOCAL void ICACHE_FLASH_ATTR send_data(struct espconn *pespconn)
{
	char *pbuf = (char *) os_zalloc(packet_size);

	//os_sprintf( json_data, "{\"temperature\": \"%d\" }", temperature );
        //os_sprintf( pbuf, pheadbufferpost, rest_path, rest_host, os_strlen( json_data ), json_data );

	os_sprintf(pbuf, pheadbuffer, "ahellbe.se");
	espconn_sent(pespconn, pbuf, os_strlen(pbuf));
	os_free(pbuf);
}

LOCAL void ICACHE_FLASH_ATTR tcp_connect_cb(void *arg)
{
	os_printf("Connected to server \r\n");

	struct espconn *pespconn = arg;

	espconn_regist_recvcb(pespconn, tcp_receive_cb);
	espconn_regist_sentcb(pespconn, tcp_sent_cb);
	espconn_regist_disconcb(pespconn, tcp_disconnect_cb);

	send_data(pespconn);
}

LOCAL void ICACHE_FLASH_ATTR tcp_reconnect_cb(void *arg, sint8 err)
{
	os_printf("Reconnect callback called, error code: %d !!! \r\n", err);
}

LOCAL void ICACHE_FLASH_ATTR dns_found_cb(const char *name, ip_addr_t *ipaddr, void *arg)
{

        struct espconn *pespconn = (struct espconn *)arg;
        if (ipaddr == NULL)
        {
                os_printf("user dns found NULL \r\n");
                return;
        }
        os_printf("user dns found %d.%d.%d.%d \r\n",
                *((uint8 *)&ipaddr->addr),
                *((uint8 *)&ipaddr->addr + 1),
                *((uint8 *)&ipaddr->addr + 2),
                *((uint8 *)&ipaddr->addr + 3));

        if (tcp_host_ip.addr == 0 && ipaddr->addr != 0)
        {
                os_timer_disarm(&test_timer);
                tcp_host_ip.addr = ipaddr->addr;
                os_memcpy(pespconn->proto.tcp->remote_ip, &ipaddr->addr, 4);
                pespconn->proto.tcp->remote_port = 80;
                pespconn->proto.tcp->local_port = espconn_port();
                espconn_regist_connectcb(pespconn, tcp_connect_cb);
                espconn_regist_reconcb(pespconn, tcp_reconnect_cb);
                espconn_connect(pespconn);
        }
}


void ICACHE_FLASH_ATTR connect_to_host(void)
{
	tcp_conn.proto.tcp = &esptcp;
	tcp_conn.type = ESPCONN_TCP;
	tcp_conn.state = ESPCONN_NONE;

	tcp_host_ip.addr = 0;
	espconn_gethostbyname(&tcp_conn, host_dns_name, &tcp_host_ip, dns_found_cb);
}

void ICACHE_FLASH_ATTR check_if_connected(void)
{
	struct ip_info ipconfig;
	os_timer_disarm( &test_timer );

	wifi_get_ip_info( STATION_IF, &ipconfig );
	if ( wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0 )
	{
		os_printf( "got ip !!  \r\n" );
		os_timer_disarm( &test_timer );
		os_timer_disarm( &connect_timer );
		connect_to_host();

	}
	else
	{
		if ( (wifi_station_get_connect_status() == STATION_WRONG_PASSWORD || wifi_station_get_connect_status() == STATION_NO_AP_FOUND || wifi_station_get_connect_status() == STATION_CONNECT_FAIL ))
		{
			os_printf("connect fail !! \r\n");
		}
		else
		{
			os_timer_setfn( &test_timer, (os_timer_func_t *) check_if_connected, NULL );
			os_timer_arm( &test_timer, 100, 0 );
		}
	}
}

void ICACHE_FLASH_ATTR connect_to_wifi_station(void)
{
	wifi_station_connect();
	os_timer_setfn( &connect_timer, (os_timer_func_t *) connect_to_wifi_station, NULL);
	os_timer_arm( &connect_timer, 5000, 0);
}

void user_init( void )
{
	uart_div_modify( 0, UART_CLK_FREQ / ( 9600 ) );
	os_printf( "INIT\r\n" );
	os_printf( "SDK version:%s\n", system_get_sdk_version() );
	static struct station_config config;

	bool resOPMODE = wifi_set_opmode( 0x01 );
	os_printf( "SET OP MODE: %s\r\n", resOPMODE ? "SUCCESS!" : "FAIL!" );

	char ssid[32] = SSID;
	char password[64] = PASS;

	os_memset( config.ssid, 0, 32 );
	os_memset( config.password, 0, 64 );
	config.bssid_set = 0;

	os_memcpy( &config.ssid, ssid, 32 );
    	os_memcpy( &config.password, password, 64 );
    	bool resCONFIG = wifi_station_set_config( &config );
	os_printf( "SET CONFIG: %s\r\n", resCONFIG ? "SUCCESS!" : "FAIL!" );

	os_timer_disarm( &connect_timer );
	os_timer_setfn( &connect_timer, (os_timer_func_t *) connect_to_wifi_station, NULL);
	os_timer_arm( &connect_timer, 5000, 0 );

	os_timer_disarm( &test_timer );
	os_timer_setfn( &test_timer, (os_timer_func_t *) check_if_connected, NULL );
	os_timer_arm( &test_timer, 100, 0 );
}
