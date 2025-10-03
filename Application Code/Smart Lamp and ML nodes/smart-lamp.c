#include "contiki.h"
#include "sys/etimer.h"
#include "dev/leds.h"
#include "os/sys/log.h"
#include "coap-engine.h"
#include "coap-blocking-api.h" 
#include "dev/button-hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MODULE "SmartLamp"
#define LOG_LEVEL LOG_LEVEL_INFO

/* IPv6 address of auto-brightness node*/
#define AUTO_BRIGHTNESS_SERVER_EP "coap://[fe80::f6ce:36fa:60d3:ad0]"

#define SENSOR_SIM_INTERVAL (1 * CLOCK_SECOND)
#define COAP_REQUEST_INTERVAL (1 * CLOCK_SECOND)
#define PRESENCE_INTERVAL (10 * CLOCK_SECOND)
#define ABSENCE_INTERVAL (5 * CLOCK_SECOND)
#define MAX_LAMP_LUX 1000 //1000 lux is the maximum lux the lamp che provide
#define MIN_LUX 156
#define MAX_LUX 502
/*---------------------------------------------------------------------------*/
static bool long_press_active = false;
static bool absent = false;
static int threshold = 50;
static int old_thr = 50;
static int current_lux = 400;
static int desired_lux = 400;
static int i_desired_lux = 400; // used to prevent from double conversion of the desired_lux variable
static int current_brightness = 0;
static bool manual_mode = false;
/*---------------------------------------------------------------------------*/
PROCESS(smart_lamp_process, "Smart Lamp Process");
PROCESS(leds_control, "Leds Control");
AUTOSTART_PROCESSES(&smart_lamp_process,&leds_control);
/*---------------------------------------------------------------------------*/

/******************************************************************************
 *                            Support Functions					                        *
 ******************************************************************************/

 static void update_leds()
 {
 		if(manual_mode){
			leds_single_on(LEDS_YELLOW);
		}
		else{
			leds_single_off(LEDS_YELLOW);
		}
		leds_off(LEDS_RED|LEDS_GREEN|LEDS_BLUE);
		if(threshold == 0)
		{
			leds_off(LEDS_RED|LEDS_GREEN|LEDS_BLUE);
			current_brightness = threshold;
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
 
static void update_threshold()
 {
		if(threshold == 0)
		{
			threshold = 20;
		}
		else if((threshold > 0) & (threshold < 20 ))
		{
			threshold = 20;
		}
		else if((threshold >= 20) & (threshold < 50))
		{
			threshold = 50;
		}
		else if((threshold >= 50) & (threshold < 80))
		{
			threshold = 80;
		}
		else if(threshold >= 80)
		{
			threshold = 0;
		}
 }
/******************************************************************************
 *                          CoAP Resources (SERVER)                           *
 ******************************************************************************/


/*----- /actuators/brightness (PUT) -----*/
static void res_brightness_put_post_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);

RESOURCE(res_led_brightness,
         "title=\"LED Brightness Control\";rt=\"Actuator\"",
         NULL, res_brightness_put_post_handler, res_brightness_put_post_handler, NULL);

static void res_brightness_put_post_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
  const uint8_t *payload = NULL;
  int len = coap_get_payload(request, &payload);

  if(len > 0) {
    int brightness = atoi((char*)payload);
    
    if (brightness >= 0 && brightness <= 100) {
      current_brightness = brightness;
      threshold = brightness;
      i_desired_lux = ((threshold*MAX_LAMP_LUX)/100)+current_lux;
      desired_lux = (int)i_desired_lux;
     	update_leds();
      coap_set_status_code(response, CHANGED_2_04);
    } else {
      coap_set_status_code(response, BAD_REQUEST_4_00);
    }
  } else {
    coap_set_status_code(response, BAD_REQUEST_4_00);
  }
}

//*----- /status (GET) -----*/
static void res_status_get_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
RESOURCE(res_status, "title=\"Device Status\";rt=\"Status\"", res_status_get_handler, NULL, NULL, NULL);

static void res_status_get_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {

    int len = snprintf((char *)buffer, preferred_size,
                       "{\"lux_perceived\": %d, \"lux_desired\": %d, \"brightness_percent\": %d}",
                       current_lux, desired_lux, current_brightness);
    coap_set_header_content_format(response, APPLICATION_JSON);
    coap_set_header_etag(response, (uint8_t *)&len, 1);
    coap_set_payload(response, buffer, len);
}

/******************************************************************************
 *                          CoAP Client Logic                                *
 ******************************************************************************/

// Handler for /autobright's response
void client_chunk_handler(coap_message_t *response)
{
  const uint8_t *chunk;

  if(response == NULL) {
    printf("CoAP request timeout\n");
    return;
  }
  int len = coap_get_payload(response, &chunk);
  if (len > 0) {
      int brightness_percent = atoi((char*)chunk);
      current_brightness = (int)brightness_percent;
      if(threshold == 0){
      	current_brightness = 0;
      }
  } else {
      printf("this is the length: %d \n",len);
  }
}

/******************************************************************************
 *                    LED and BUTTONS Management Process                      *
 ******************************************************************************/
PROCESS_THREAD(leds_control, ev, data)
{
	PROCESS_BEGIN();
	while(1){
		PROCESS_WAIT_EVENT();
		// buttons events trigger the threshold change during release event,
		// since even long button presses have an initial button_hal_press_event 
		if(ev == button_hal_press_event) {
      long_press_active = false;
      manual_mode=false;
    }
    else if(ev == button_hal_periodic_event) {
			if(!long_press_active) {
				long_press_active = true;
				manual_mode = true;
			}
			threshold += 5;
			if (threshold > 100) {
				threshold = 0;
			}
    }
    else if(ev == button_hal_release_event) {
      if(long_press_active) {
				threshold = threshold; // Do nothing, the threshold is already set
			} else {
				update_threshold();
      }
      long_press_active = false;
      // the desired lux is set based upon the lamp brightness and the current ambient lux, because 
      // the user should adjust the light to have that amount of lux on the desk that makes the activities comfortable
      i_desired_lux = ((threshold*MAX_LAMP_LUX)/100)+current_lux;
      desired_lux = (int)i_desired_lux;
    }

   	update_leds();
  }
	PROCESS_END();
}
/******************************************************************************
 *                         Main Process                                       *
 ******************************************************************************/
PROCESS_THREAD(smart_lamp_process, ev, data)
{
  static struct etimer sensor_sim_timer;
  static struct etimer presence_timer;
  static struct etimer absence_timer;
  static struct etimer coap_request_timer;
  static coap_endpoint_t server_ep;
  
  PROCESS_BEGIN();

  coap_engine_init();

  coap_activate_resource(&res_led_brightness, "actuators/brightness");
	coap_activate_resource(&res_status, "status");

  etimer_set(&sensor_sim_timer, SENSOR_SIM_INTERVAL);
  etimer_set(&coap_request_timer, COAP_REQUEST_INTERVAL);
  etimer_set(&presence_timer, PRESENCE_INTERVAL);
  

  while(1) {
    PROCESS_WAIT_EVENT();
    if(etimer_expired(&absence_timer) & absent)
    {
    	//simulates that the user is back on the desk, the lamp must be turned back on
  		threshold = old_thr;
  		update_leds();
  		etimer_set(&presence_timer, PRESENCE_INTERVAL);
  		absent = false;
    }
    if(etimer_expired(&presence_timer))
    {
    	if((rand()%2==0) & (absent==false)) //simulates that the user got up from the desk, the lamp is useless and must be turned off
    	{
    		old_thr = threshold;
    		threshold = 0;
    		update_leds();
    		etimer_set(&absence_timer, ABSENCE_INTERVAL);

    		absent=true;
    	}
    	else
    	{
    		etimer_reset(&presence_timer);
    	}
    }
    if(etimer_expired(&sensor_sim_timer)) {

      // new lux value sampled by the sensor from the ambient
      current_lux = 350 + (rand() % 51); 
      
      etimer_reset(&sensor_sim_timer);
    }

    if(etimer_expired(&coap_request_timer)) {
      if(threshold > 0) {
		    
		    coap_endpoint_parse(AUTO_BRIGHTNESS_SERVER_EP, strlen(AUTO_BRIGHTNESS_SERVER_EP), &server_ep);
		    
				coap_endpoint_parse(AUTO_BRIGHTNESS_SERVER_EP, strlen(AUTO_BRIGHTNESS_SERVER_EP), &server_ep);

				coap_message_t request; 
				coap_init_message(&request, COAP_TYPE_CON, COAP_GET, coap_get_mid());

				coap_set_header_uri_path(&request, "autobright");

				static char query_params[32]; 
				snprintf(query_params, sizeof(query_params), "lux=%d&des_lux=%d", current_lux, desired_lux);
				coap_set_header_uri_query(&request, query_params);

				COAP_BLOCKING_REQUEST(&server_ep, &request, client_chunk_handler);
			}
      etimer_reset(&coap_request_timer);
    }
    printf("\r lux: %d, des_lux: %d, thr: %d, sugg_brightness: %d, user: %s    ",current_lux, desired_lux, threshold, current_brightness, absent ? "absent" : "present");
  }

  PROCESS_END();
}
