#ifndef AT_CMD_HEAD
#define AT_CMD_HEAD
#include <regex.h>
#include "uhash.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <esp_event_loop.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "app_wifi.h"


typedef struct {
    char command_name[100]; // Assuming the command name will not exceed 100 characters
    void (*func)(char *params);     // Function pointer to the function that will process the command
    UT_hash_handle hh;      // Makes this structure hashable
}command_entry;


#pragma pack(push, 1)
typedef struct {
    uint8_t * msg;
    int size;
} message_event_t;
#pragma pack(pop) 


typedef struct {
    char * response;  
    int length;           
} AT_Response;


extern SemaphoreHandle_t AT_response_semaphore;

extern QueueHandle_t AT_response_queue;





#define DEBUG_AT_CMD

#ifdef DEBUG_AT_CMD

#define TASK_STATS_BUFFER_SIZE 50

extern char * test_strings[];
extern command_entry *commands;
void vTaskMonitor(void *para);
#endif







void AT_command_reg();      //  Function to register the AT commands
void AT_command_free();     // Function to free the memory allocated for the commands

void add_command(command_entry ** commands,const char *name ,void (*func)(char *params));   // Function to add a command to the list of commands
void exec_command(command_entry **commands, const char *name, char *params, char query);            // Function to execute a command

void task_handle_AT_command(); // Function to handle the AT command select witch command to execute


void handle_type_1_command(char *params);   //test command
void handle_deviceinfo_command();   //device info
void handle_wifi_set(char *params);     //wifi command
void handle_wifi_query(char *params); 
void handle_token(char *params);            //token command
void handle_eui_command(char *params);      //eui command
void handle_wifi_table(char *params);       //wifi table command    
void handle_deviceinfo_cfg_command (char *params); //timezone command
void handle_taskflow_command(char *params);
//void handle_deviceinfo_command(char *params);  //handle deviceinfo at command




void init_event_loop_and_task(void);

void AT_cmd_init();





void pushWiFiStack(WiFiStack *stack, WiFiEntry entry);
void freeWiFiStack(WiFiStack *stack);
void initWiFiStack(WiFiStack *stack, int capacity);
void wifi_stack_semaphore_init();

extern esp_event_base_t const AT_EVENTS;
static const char * AT_EVENTS_TAG ="AT_EVENTS";
#define AT_EVENTS_COMMAND_ID 0x6F
#define AT_EVENTS_RESPONSE_ID 0x70
#define MEMORY_SIZE  (1024*100)

extern esp_event_loop_handle_t  at_event_loop_handle;
#endif