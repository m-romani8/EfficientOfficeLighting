#include "contiki.h"
#include "coap-engine.h"
#include "coap-blocking-api.h"
#include "sys/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "os/dev/leds.h"
#include "dev/button-hal.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uiplib.h"
#include "dimming_forecast.h"

#define LOG_MODULE "AutoBright"
#define LOG_LEVEL LOG_LEVEL_INFO


/*IPV6 Local-Link Addresses to send PUT requests*/
#define SMART_LAMP_SERVER_EP_1 "coap://[fe80::f6ce:366c:f0fd:f7e5]"
#define SMART_LAMP_SERVER_EP_2 "coap://[fe80::f6ce:3636:5325:98f8]"

PROCESS(auto_brightness_process, "Auto Brightness Server");
AUTOSTART_PROCESSES(&auto_brightness_process);

// Keeps track of the command for putting all lights off or on
static bool alloff = false;

static void set_last_brightness(int threshold)
{
	leds_off(LEDS_RED|LEDS_GREEN|LEDS_BLUE);
	if(threshold == 0)
	{
		leds_off(LEDS_RED|LEDS_GREEN|LEDS_BLUE);
	}
	else if((threshold > 0) & (threshold <= 20 ))
	{
		leds_on(LEDS_GREEN);
	}
	else if((threshold > 20) & (threshold <= 50))
	{
		leds_on(LEDS_BLUE);
	}
	else if((threshold > 50) & (threshold <= 80))
	{
		leds_on(LEDS_RED|LEDS_BLUE);
	}
	else if((threshold > 80) & (threshold <= 100))
	{
		leds_on(LEDS_RED);
	} 
}
/*------------------------------------------------------------------------------------------------------------------------*/
/*																								/autobright Resource																										*/
/*------------------------------------------------------------------------------------------------------------------------*/
static void client_chunk_handler_put(coap_message_t *response)
{
  if(response == NULL) {
    LOG_WARN("PUT request failed (timeout)\n");
    return;
  }
  LOG_INFO("PUT response receievd. Code: %d\n", response->code);
}

static void res_autobright_get_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);

// Handler for the brightness prediction resource
RESOURCE(res_autobright,
         "title=\"Auto Brightness Calculator\";rt=\"Function\"",
         res_autobright_get_handler,
         NULL, NULL, NULL);

static void
res_autobright_get_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  const char *lux_str = NULL;
  const char *dlux_str = NULL;
  int lux_val = 0;
  int dlux_val = 0;

  int len = coap_get_query_variable(request, "lux", &lux_str);
  int dlen = coap_get_query_variable(request, "des_lux", &dlux_str);
  
  if ((len > 0) && (dlen > 0)) {
  
      char lux_buffer[len + 1];
      char dlux_buffer[dlen + 1];

      memcpy(lux_buffer, lux_str, len);
      lux_buffer[len] = '\0';
      
      memcpy(dlux_buffer, dlux_str, dlen);
      dlux_buffer[dlen] = '\0';

      lux_val = atoi(lux_buffer);
      dlux_val = atoi(dlux_buffer);
      
      LOG_INFO("GET request received for /autobright with lux = %d and des_lux = %d\n", lux_val, dlux_val);
  } else {
      LOG_INFO("GET request received for /autobright with uncorrect payload\n");
  }

  int16_t features[] = {lux_val, dlux_val};
  float brightness_prediction = dimming_forecast_predict(features,2);
  int brightness_suggestion = (int)brightness_prediction;
  set_last_brightness(brightness_suggestion);
  LOG_INFO("Respond with predicted brightness %d%%\n", brightness_suggestion);

  int response_len = snprintf((char *)buffer, preferred_size, "%d", brightness_suggestion);

  coap_set_header_content_format(response, TEXT_PLAIN);
  coap_set_header_etag(response, (uint8_t *)&response_len, 1);
  coap_set_payload(response, buffer, response_len);
}

/*------------------------------------------------------------------------------------------------------------------------*/
/*																								Main Process																														*/
/*------------------------------------------------------------------------------------------------------------------------*/


PROCESS_THREAD(auto_brightness_process, ev, data)
{
  PROCESS_BEGIN();
  leds_single_on(LEDS_YELLOW);
  LOG_INFO("Server Auto-Brightness is on\n");
  
  coap_engine_init();
  coap_activate_resource(&res_autobright, "autobright");

  while(1)
  {
    PROCESS_WAIT_EVENT();
    if(ev == button_hal_press_event)
    {
      
      LOG_INFO("Sending command to the lamps.\n");

			static coap_endpoint_t server_ep;
			static coap_message_t request; 
			static char payload[8]; 

			if(alloff == false) {

				LOG_INFO("Sending OFF\n");
				leds_single_off(LEDS_YELLOW);
				snprintf(payload, sizeof(payload), "0");
			} else {
				LOG_INFO("Sending ON\n");
				leds_single_on(LEDS_YELLOW);
				snprintf(payload, sizeof(payload), "20");
			}

			coap_endpoint_parse(SMART_LAMP_SERVER_EP_1, strlen(SMART_LAMP_SERVER_EP_1), &server_ep);

			coap_init_message(&request, COAP_TYPE_CON, COAP_PUT, coap_get_mid());
			
			coap_set_header_uri_path(&request, "actuators/brightness");

			coap_set_payload(&request, (uint8_t *)payload, strlen(payload));

			COAP_BLOCKING_REQUEST(&server_ep, &request, client_chunk_handler_put);
			coap_endpoint_parse(SMART_LAMP_SERVER_EP_2, strlen(SMART_LAMP_SERVER_EP_2), &server_ep);
			COAP_BLOCKING_REQUEST(&server_ep, &request, client_chunk_handler_put);
      alloff = !alloff;
      LOG_INFO("'alloff' state updated to: %s\n", alloff ? "true" : "false");
      uip_ds6_addr_t *lladdr = uip_ds6_get_link_local(ADDR_PREFERRED);
      // debug function to print own ipv6 address
      if(lladdr != NULL) {
        LOG_INFO("My Link-Local Address is: ");
        LOG_INFO_6ADDR(&lladdr->ipaddr);
        LOG_INFO_("\n");
      } else {
        LOG_WARN("Link-Local Address not available yet.\n");
      }
    }
  }
  PROCESS_END();
}
